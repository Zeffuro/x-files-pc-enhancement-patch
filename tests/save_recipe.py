"""Synthetic save recipe regression checks. No game files are required."""

import contextlib
import copy
import io
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest import mock
import unittest

BUILD = "dvd_20000"
TOOLS = Path(__file__).resolve().parents[1] / "tools"
spec = importlib.util.spec_from_file_location("recipe_tool", TOOLS / "save-recipe.py")
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


class Fixture:
    def __init__(self):
        self.data = bytearray(32)
        self.records = {}

    def allocate(self, size):
        at = (len(self.data) + 7) & ~7
        self.data.extend(bytes(at + size - len(self.data)))
        return at

    def put(self, at, value, size=4):
        self.data[at:at + size] = value.to_bytes(size, "big")

    def record(self, cls, identity, size):
        at = self.allocate(size)
        self.put(at, 0x80, 1)
        self.put(at + 2, 1)
        self.records[cls, identity] = at
        return at

    def blob(self, descriptor, value):
        if value:
            at = self.allocate(len(value))
            self.data[at:at + len(value)] = value
            self.put(descriptor, at)
            self.put(descriptor + 4, len(value))

    def finish(self, highwater):
        classes = sorted({cls for cls, _ in self.records})
        indexes = {}
        for cls in classes:
            entries = sorted((i, at) for (c, i), at in self.records.items() if c == cls)
            index = self.allocate(8 + 8 * len(entries))
            self.put(index, 0xC0, 1)
            self.put(index + 2, 1)
            self.put(index + 6, len(entries), 2)
            for i, (identity, at) in enumerate(entries):
                self.put(index + 8 + 8 * i, at)
                self.put(index + 12 + 8 * i, identity)
            indexes[cls] = index, len(entries)
        identities = self.allocate(8 + 4 * len(classes))
        self.put(identities, 0xC0, 1)
        self.put(identities + 2, 1)
        self.put(identities + 6, len(classes), 2)
        for i, cls in enumerate(classes):
            self.put(identities + 8 + i * 4, cls)
        directory = self.allocate(8 + 43 * (len(classes) + 1) + 7)
        self.put(directory, 0xC0, 1)
        self.put(directory + 2, 1)
        self.put(directory + 6, len(classes) + 1, 2)
        for i, cls in enumerate([1] + classes):
            at = directory + 8 + 43 * i
            self.put(at + 10, 1, 1)
            self.put(at + 11, cls)
            self.put(at + 27, 7)
            self.put(at + 35, cls)
            self.put(at + 39, 0x6E756C6C)
            if cls == 1:
                self.put(at + 8, len(classes), 2)
                self.put(at + 19, identities)
            else:
                index, count = indexes[cls]
                self.put(at + 15, count)
                self.put(at + 31, index)
        for offset, value in ((0, 5), (8, directory), (12, len(classes) + 1),
                              (16, highwater), (20, 0x501), (24, 0x40000), (28, 256)):
            self.put(offset, value)
        return bytes(self.data)


def fixture(saved=False, variable_count=2, build=BUILD):
    f = Fixture()
    enabled = f.record(0x46, 20, 12)
    f.put(enabled + 6, 300)
    f.put(enabled + 10, int(saved), 1)
    for i in range(variable_count):
        at = f.record(0x53, 10 + i, 24)
        f.blob(at + 6, ("variable" + str(i)).encode() + b"\0")
        string = i == 1
        f.put(at + 14, (41 if saved else 40) if string else (0xFFFFFFFF if saved else 3))
        f.put(at + 23, 0x83 if string else 0x81, 1)
    at = f.record(0x50, 41 if saved else 40, 14)
    f.blob(at + 6, b"SAVED_SECRET_STRING\0" if saved else b"AUTHORED_BASELINE_STRING\0")
    if saved:
        ids = {cls: 150 + i for i, cls in enumerate(recipe.ID_ORDER)}
        for cls in recipe.AUXILIARY:
            at = f.record(cls, ids[cls], 132 if cls == 0x57 else 14)
            if cls == 0x57:
                f.put(at + 18, 765)
                for offset, target in recipe.SCENE_REFERENCES.items():
                    f.put(at + offset, ids[target])
                f.blob(at + 74, struct.pack("<II", 1234, 5678))
                f.blob(at + 66, b"OPAQUE_SECRET")
                f.blob(at + 98, struct.pack(recipe.BUILDS[build][1], 7, 57605, 0))
            else:
                f.blob(at + 6, struct.pack("<II", 1000 + cls, 2000 + cls))
        at = f.record(0x59, 190, 14)
        f.blob(at + 6, b"SAVED_PHOTO_SECRET")
    return f.finish(200 if saved else 100)


class Recipes(unittest.TestCase):
    def setUp(self):
        self.base, self.saved = fixture(), fixture(True)
        self.value = recipe.extract(self.base, self.saved, BUILD)

    def test_numeric_roundtrip_preserves_authored_data_and_omits_saved_payloads(self):
        text = json.dumps(self.value)
        for secret in ("SAVED_SECRET", "OPAQUE_SECRET", "SAVED_PHOTO", "AUTHORED_BASELINE"):
            self.assertNotIn(secret, text)
        output = recipe.reconstruct(self.base, self.value, BUILD)
        db = recipe.Database(output)
        self.assertEqual(db.variable(10)[2], 0xFFFFFFFF)
        self.assertEqual(db.variable(11)[2], 40)
        self.assertEqual(db.part(db.records[0x46, 20] + 10, 1), b"\1")
        self.assertIn(b"AUTHORED_BASELINE_STRING", output)
        for secret in (b"SAVED_SECRET", b"OPAQUE_SECRET", b"SAVED_PHOTO"):
            self.assertNotIn(secret, output)
        self.assertEqual(self.value["omitted"], dict(photo_records=1, string_variables=[11],
                                                      packed_fields=[66]))
        self.assertEqual(db.identities(0x53), {10, 11})
        self.assertEqual(db.u32(16), 106)
        scene = db.records[0x57, 100]
        self.assertEqual(db.u32(scene + 18), 765)
        self.assertEqual(db.words(scene + 74), [1234, 5678])
        for offset, cls in recipe.SCENE_REFERENCES.items():
            self.assertEqual(db.u32(scene + offset), recipe.dynamic_ids(recipe.Database(self.base))[cls])
        root = db.u32(8)
        self.assertGreaterEqual(len(output) - root - 8 - db.u32(12) * 43, 7)

    def test_different_baseline_variable_counts_are_preserved(self):
        for count in (2, 3):
            with self.subTest(count=count):
                base, saved = fixture(variable_count=count), fixture(True, count)
                value = recipe.extract(base, saved, BUILD)
                self.assertEqual(value["baseline_variable_count"], count)
                self.assertEqual(len(recipe.Database(recipe.reconstruct(base, value, BUILD)).identities(0x53)), count)
        with self.assertRaisesRegex(recipe.FormatError, "identities differ"):
            recipe.extract(fixture(), fixture(True, 3), BUILD)

    def test_word_alignment_does_not_promote_opaque_scene_fields(self):
        saved = bytearray(self.saved)
        db = recipe.Database(self.saved)
        scene = db.records[0x57, 150]
        saved[scene + 70:scene + 74] = (8).to_bytes(4, "big")
        value = recipe.extract(self.base, bytes(saved), BUILD)
        self.assertEqual(value["omitted"]["packed_fields"], [66])
        output = recipe.Database(recipe.reconstruct(self.base, value, BUILD))
        self.assertEqual(output.blob(output.records[0x57, 100] + 66), b"")

    def test_timers_use_explicit_build_layout_and_restart_fields(self):
        for build, width in (("cd_10012", 12), ("dvd_20000", 9),
                             ("cd_10019", 9), ("cd_10020", 9)):
            with self.subTest(build=build):
                value = recipe.extract(self.base, fixture(True, build=build), build)
                self.assertEqual(value["timers"], [dict(timer_id=7, duration_ms=57605, periodic=False)])
                value["timers"].append(dict(timer_id=255, duration_ms=0xFFFFFFFF, periodic=True))
                data = recipe.reconstruct(self.base, value, build)
                db = recipe.Database(data)
                blob = db.blob(db.records[0x57, 100] + 98)
                self.assertEqual(len(blob), 2 * width)
                self.assertEqual(list(struct.iter_unpack(recipe.BUILDS[build][1], blob)),
                                 [(7, 57605, 0), (255, 0xFFFFFFFF, 1)])
                reread = recipe.extract(self.base, data, build)
                self.assertEqual(reread["timers"], value["timers"])
        with self.assertRaises(recipe.FormatError):
            recipe.extract(self.base, self.saved, "cd_10012")
        with self.assertRaisesRegex(recipe.FormatError, "builds differ"):
            recipe.reconstruct(self.base, self.value, "cd_10012")

    def test_all_baseline_fields_are_checked_even_without_deltas(self):
        base = bytearray(self.base)
        db = recipe.Database(self.base)
        corruptions = [(db.records[0x53, 11] + 14, 999, 4),
                       (db.records[0x53, 11] + 23, 0x87, 1),
                       (db.records[0x46, 20] + 10, 2, 1),
                       (db.records[0x50, 40] + 6, 0xFFFFFFF0, 4)]
        for offset, value, width in corruptions:
            with self.subTest(offset=offset):
                damaged = bytearray(base)
                damaged[offset:offset + width] = value.to_bytes(width, "big")
                changed = copy.deepcopy(self.value)
                changed["baseline_sha256"] = recipe.digest(damaged)
                changed["omitted"]["string_variables"] = []
                with self.assertRaises(recipe.FormatError):
                    recipe.reconstruct(bytes(damaged), changed, BUILD)
                with self.assertRaises(recipe.FormatError):
                    recipe.extract(bytes(damaged), self.saved, BUILD)

    def test_timer_schema_rejects_duplicates_ranges_flags_and_build_identity(self):
        mutations = [lambda p: p["timers"].append(p["timers"][0]),
                     lambda p: p["timers"][0].update(timer_id=256),
                     lambda p: p["timers"][0].update(duration_ms=-1),
                     lambda p: p["timers"][0].update(periodic=1),
                     lambda p: p["timers"][0].update(raw_payload=[7]),
                     lambda p: p.update(executable_sha256="0" * 64),
                     lambda p: p.update(native_build="unknown")]
        for change in mutations:
            value = copy.deepcopy(self.value)
            change(value)
            with self.subTest(value=value), self.assertRaises(recipe.FormatError):
                recipe.reconstruct(self.base, value, BUILD)
        db = recipe.Database(self.saved)
        scene = db.records[0x57, 150]
        blob = db.u32(scene + 98)
        for offset, value, width in ((scene + 102, 8, 4), (blob, 256, 4), (blob + 8, 2, 1)):
            damaged = bytearray(self.saved)
            damaged[offset:offset + width] = value.to_bytes(width, "little" if offset == blob else "big")
            with self.subTest(offset=offset), self.assertRaises(recipe.FormatError):
                recipe.extract(self.base, bytes(damaged), BUILD)

    def test_recipe_rejects_invalid_schema_and_delta_contracts(self):
        mutations = [lambda p: p.update(extra=1),
                     lambda p: p.update(baseline_sha256="0" * 64),
                     lambda p: p.update(baseline_variable_count=True),
                     lambda p: p["numeric_variables"][0].__setitem__(1, 4),
                     lambda p: p["numeric_variables"].append(p["numeric_variables"][0]),
                     lambda p: p["numeric_variables"][0].__setitem__(0, 11),
                     lambda p: p["enabled_states"][0].__setitem__(2, 2),
                     lambda p: p["auxiliary"][0]["words"].append(-1),
                     lambda p: p["auxiliary"][1]["scalars"].__setitem__(4, 999),
                     lambda p: p["auxiliary"][0].update(raw_payload="abc"),
                     lambda p: p["omitted"]["packed_fields"].append(74)]
        for mutate in mutations:
            value = copy.deepcopy(self.value)
            mutate(value)
            with self.subTest(value=value), self.assertRaises(recipe.FormatError):
                recipe.reconstruct(self.base, value, BUILD)

    def test_json_rejects_duplicates_floats_constants_and_excessive_depth(self):
        for data in (b'{"x":1,"x":2}', b'{"x":1.0}', b'{"x":NaN}',
                     b'[' * 1200 + b']' * 1200, b'\xff'):
            with self.subTest(data=data[:30]), self.assertRaises(recipe.FormatError):
                recipe.decode_recipe(data)

    def test_corrupt_database_bounds_links_metadata_and_words(self):
        db = recipe.Database(self.saved)
        patches = [(8, 0xFFFFFFFC, 4), (db.records[0x53, 10] + 6, 0xFFFFFFF0, 4),
                   (db.records[0x53, 10] + 23, 0x82, 1),
                   (db.records[0x57, 150] + 22, 999, 4),
                   (db.records[0x56, 151] + 10, 7, 4)]
        index = db.classes[0x53][1]
        patches.extend([(index, 0, 1), (index + 8, index, 4),
                        (index + 20, 10, 4), (index + 16, db.records[0x53, 10], 4),
                        (index + 6, 33, 2)])
        for at, value, width in patches:
            data = bytearray(self.saved)
            data[at:at + width] = value.to_bytes(width, "big")
            with self.subTest(at=at, value=value), self.assertRaises(recipe.FormatError):
                recipe.extract(self.base, bytes(data), BUILD)
        with self.assertRaises(recipe.FormatError):
            recipe.Database(self.saved[:100])

    def test_outputs_are_exclusive_and_allow_input_folders(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs, outputs = root / "inputs", root / "outputs"
            inputs.mkdir()
            outputs.mkdir()
            baseline = inputs / "original.gam"
            baseline.write_bytes(self.base)
            recipe.write_new(inputs / "new.x", b"x", [baseline])
            self.assertEqual((inputs / "new.x").read_bytes(), b"x")
            with self.assertRaises(recipe.FormatError):
                recipe.write_new(baseline, b"x", [baseline])
            nested = inputs / "saves"
            nested.mkdir()
            recipe.write_new(nested / "new.x", b"nested", [baseline])
            self.assertEqual((nested / "new.x").read_bytes(), b"nested")
            alias = outputs / "alias.x"
            try:
                alias.hardlink_to(baseline)
            except OSError:
                pass
            else:
                with self.assertRaises(FileExistsError):
                    recipe.write_new(alias, b"x", [baseline])
            self.assertEqual(baseline.read_bytes(), self.base)
            target = outputs / "new.x"
            recipe.write_new(target, b"first", [baseline])
            with self.assertRaises(FileExistsError):
                recipe.write_new(target, b"second", [baseline])
            self.assertEqual(target.read_bytes(), b"first")

    def test_cli_extract_reconstruct_and_failure_do_not_modify_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for child in ("inputs", "recipes", "outputs"):
                (root / child).mkdir()
            base, saved = root / "inputs/base.gam", root / "inputs/save.x"
            base.write_bytes(self.base)
            saved.write_bytes(self.saved)
            executable = root / "inputs/game.exe"
            executable.write_bytes(b"unsupported executable")
            path, output = root / "recipes/recipe.json", root / "outputs/save.x"
            common = ["--baseline", str(base), "--executable", str(executable)]
            with mock.patch.object(recipe, "identify_executable", return_value=BUILD):
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(recipe.main(["extract", str(saved), "--output", str(path)] + common), 0)
                    self.assertEqual(recipe.main(["reconstruct", str(path), "--output", str(output)] + common), 0)
                errors = io.StringIO()
                with contextlib.redirect_stderr(errors):
                    self.assertEqual(recipe.main(["extract", str(saved), "--save-sha256", "0" * 64,
                                               "--output", str(path)] + common), 2)
                self.assertIn("SHA-256 mismatch", errors.getvalue())
            self.assertEqual(recipe.Database(output.read_bytes()).variable(10)[2], 0xFFFFFFFF)
            failed = subprocess.run([sys.executable, str(TOOLS / "save-recipe.py"),
                                     "extract", str(saved), "--output", str(path)] + common,
                                    capture_output=True, text=True)
            self.assertEqual(failed.returncode, 2)
            self.assertIn("unsupported executable SHA-256", failed.stderr)
            self.assertEqual(base.read_bytes(), self.base)
            self.assertEqual(saved.read_bytes(), self.saved)


if __name__ == "__main__":
    unittest.main()
