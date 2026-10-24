"""Exercise canonical type planning without mutating an IDA database."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_ida_types", ROOT / "src/game/scripts/ida_types.py")
types = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(types)
PROFILES = json.loads((ROOT / "src/game/profiles/builds.json").read_text(encoding="utf-8"))


class FakeAdapter:
    def __init__(self, build):
        self.digest = build["sha256"]
        self.bits = 32
        self.local, self.writes, self.sessions = {}, [], []
        self.fail_copy = False
        self.after_install = None

    def wait(self):
        pass

    def is_32bit(self):
        return self.bits == 32

    def input_sha256(self):
        return self.digest

    def open(self):
        session = {"types": {}, "closed": False}
        self.sessions.append(session)
        return session

    def close(self, session):
        session["closed"] = True

    def current(self, name):
        return self.local.get(name)

    def named(self, session, name):
        return session["types"].get(name)

    def identical(self, left, right):
        return left == right

    def forward(self, session, name):
        session["types"][name] = {"name": name, "kind": "opaque"}

    def define(self, session, record):
        session["types"][record["name"]] = copy.deepcopy(record)
        return session["types"][record["name"]]

    def verify(self, value, record):
        if value != record:
            raise ValueError("Stored canonical type changed")

    def node(self, session, node):
        return copy.deepcopy(node)

    def snapshot(self, value):
        return copy.deepcopy(value)

    def enum_snapshots(self, names):
        return {name: copy.deepcopy(self.local[name]) for name in names if name in self.local}

    def restore_enums(self, snapshots):
        if any(self.local[name] != value for name, value in snapshots.items()):
            raise ValueError("Existing enum changed")

    def forward_local(self, name):
        if name in self.local:
            raise ValueError("Local type already exists")
        self.local[name] = {"name": name, "kind": "opaque"}

    def install(self, record, replace):
        if self.fail_copy:
            raise ValueError("Creation failed")
        name = record["name"]
        if name in self.local and not replace:
            raise AssertionError("An unowned local type reached the replacement API")
        self.local[name] = copy.deepcopy(record)
        self.writes.append(name)
        if self.after_install:
            self.after_install(name)
        return self.local[name]


class SdkVector(list):
    is_union = False
    push_back = list.append


class SdkType:
    def __init__(self, kind="scalar", name=None, size=4, **data):
        self.kind, self.name, self.size, self.data = kind, name, size, data

    def get_type_name(self):
        return self.name

    def is_const(self):
        return self.data.get("const", False)

    def is_volatile(self):
        return False

    def get_size(self):
        return self.size

    def get_alignment(self):
        return 4

    def is_forward_decl(self):
        return False

    def is_udt(self):
        return self.kind == "record"

    def get_udt_details(self, detail):
        detail.extend(SimpleNamespace(name=name, offset=offset, size=size, type=value)
                      for name, offset, size, value in self.data["fields"])
        return True

    def is_enum(self):
        return self.kind == "enum"

    def get_enum_details(self, detail):
        detail.bte, detail.taenum_bits = self.data["bte"], self.data["flags"]
        detail.extend(SimpleNamespace(name=name, value=value, cmt=self.data.get("comments", {}).get(name, ""))
                      for name, value in self.data["values"])
        return True

    def get_type_cmt(self):
        return self.data.get("comment", "")

    def is_ptr(self):
        return self.kind == "pointer"

    def get_ptr_details(self, detail):
        detail.obj_type, detail.taptr_bits, detail.based_ptr_size = self.data["to"], 0, 0
        detail.closure = SimpleNamespace(empty=lambda: True)
        detail.is_shifted = lambda: False
        return True

    def is_array(self):
        return self.kind == "array"

    def get_array_details(self, detail):
        detail.elem_type, detail.nelems, detail.base = self.data["element"], self.data["count"], 0
        return True

    def is_func(self):
        return self.kind == "function"

    def get_func_details(self, detail):
        detail.rettype, detail.cc = self.data["return"], self.data["cc"]
        detail.extend(SimpleNamespace(flags=0, type=value) for value in self.data["params"])
        return True

    def is_complex(self):
        return self.kind != "scalar"

    def is_bitfield(self):
        return False

    def get_realtype(self):
        return self.data.get("code", "uint32")


class AutoBitmaskType(SdkType):
    ENUMBM_OFF = 0

    def create_enum(self, detail):
        self.kind = "enum"
        self.data = {"bte": detail.bte, "flags": detail.taenum_bits,
                     "values": [(member.name, member.value) for member in detail]}
        return True

    def set_named_type(self, til, name, flags):
        self.name, self.attached = name, til
        self.data["bte"] |= 16
        til[name] = copy.deepcopy(self)
        return 0

    def get_named_type(self, til, name):
        if name not in til:
            return False
        self.__dict__.update(copy.deepcopy(til[name].__dict__))
        self.attached = til
        return True

    def set_enum_is_bitmask(self, mode):
        self.data["bte"] &= ~16
        self.attached[self.name].data["bte"] &= ~16
        return 0


class IdaTypesTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "source.h"
        self.source.write_text("canonical fixture\n", encoding="utf-8")
        self.build = PROFILES["builds"][0]
        self.adapter = FakeAdapter(self.build)
        self.manifest = {
            "schema": 1, "sources": {"source.h": hashlib.sha256(
                self.source.read_bytes().replace(b"\r\n", b"\n")).hexdigest()},
            "target": {"triple": "i686-pc-windows-msvc", "pointer_size": 4},
            "types": [
                {"kind": "struct", "name": "XFiles_A", "size": 4, "align": 4,
                 "fields": [{"name": "next", "offset": 0, "size": 4,
                             "type": {"kind": "pointer", "to": {"kind": "named", "name": "XFiles_B"}}}]},
                {"kind": "struct", "name": "XFiles_B", "size": 4, "align": 4,
                 "fields": [{"name": "next", "offset": 0, "size": 4,
                             "type": {"kind": "pointer", "to": {"kind": "named", "name": "XFiles_A"}}}]},
            ],
            "cpp_names": {"A *": {"kind": "pointer", "to": {"kind": "named", "name": "XFiles_A"}}},
        }

    def plan(self):
        return types.plan(PROFILES, self.build, self.adapter, self.root, self.manifest)

    def test_preview_forward_cycles_close_and_cpp_resolution_are_read_only(self):
        with self.plan() as planned:
            self.assertEqual(types.run(planned), [("ready", "XFiles_A"), ("ready", "XFiles_B")])
            self.assertEqual(planned.cpp_node("A*"), self.manifest["cpp_names"]["A *"])
            self.assertEqual(self.adapter.writes, [])
            self.assertEqual(set(planned.types), {"XFiles_A", "XFiles_B"})
        self.assertTrue(self.adapter.sessions[0]["closed"])
        self.assertEqual(self.adapter.local, {})
        with self.assertRaises(ValueError):
            types.run(planned, True)

    def test_apply_copies_only_missing_closure_and_repeat_is_idempotent(self):
        with self.plan() as planned:
            results = types.run(planned, True)
            self.assertEqual(results, [("applied", "XFiles_A"), ("applied", "XFiles_B")])
        self.assertEqual(set(self.adapter.writes), {"XFiles_A", "XFiles_B"})
        with self.plan() as planned:
            self.assertTrue(all(status == "already" for status, _ in types.run(planned, True)))
        self.assertEqual(len(self.adapter.writes), 2)

    def test_conflict_blocks_transitive_pointer_dependents_without_replacement(self):
        self.adapter.local["XFiles_B"] = {"kind": "user", "size": 100}
        before = copy.deepcopy(self.adapter.local)
        with self.plan() as planned:
            self.assertEqual(types.run(planned, True), [
                ("dependency_conflict", "XFiles_A"), ("conflict", "XFiles_B")])
        self.assertEqual(self.adapter.local, before)
        self.assertEqual(self.adapter.writes, [])

    def test_dependency_appearing_after_plan_cannot_be_overwritten(self):
        with self.plan() as planned:
            self.adapter.local["XFiles_B"] = {"kind": "user"}
            self.assertTrue(all(status == "changed_since_plan" for status, _ in types.run(planned, True)))
        self.assertEqual(self.adapter.writes, [])

    def test_source_and_identity_changes_after_plan_cannot_write(self):
        for change in (lambda: setattr(self.adapter, "digest", "0" * 64),
                       lambda: setattr(self.adapter, "bits", 64),
                       lambda: self.source.write_text("changed", encoding="utf-8")):
            self.adapter = FakeAdapter(self.build)
            self.source.write_text("canonical fixture\n", encoding="utf-8")
            with self.plan() as planned:
                change()
                self.assertTrue(all(status == "changed_since_plan" for status, _ in types.run(planned, True)))
            self.assertEqual(self.adapter.writes, [])

    def test_stale_sources_wrong_target_and_unknown_identity_stop_before_session(self):
        cases = [lambda: self.source.write_text("stale", encoding="utf-8"),
                 lambda: self.manifest["target"].update(pointer_size=8),
                 lambda: setattr(self.adapter, "digest", "0" * 64)]
        for mutate in cases:
            self.setUp()
            mutate()
            with self.assertRaises(ValueError):
                self.plan()
            self.assertEqual(self.adapter.sessions, [])

    def test_invalid_graph_overlap_and_source_escape_are_rejected(self):
        for mutate in (
                lambda m: m["types"][0]["fields"][0]["type"]["to"].update(name="XFiles_Unknown"),
                lambda m: m["types"][0]["fields"][0].update(size=8),
                lambda m: m["sources"].update({"../escape.h": "0" * 64})):
            manifest = copy.deepcopy(self.manifest)
            mutate(manifest)
            with self.assertRaises(ValueError):
                types.plan(PROFILES, self.build, self.adapter, self.root, manifest)
        self.assertEqual(self.adapter.sessions, [])

    def test_failed_sdk_copy_is_reported(self):
        self.adapter.fail_copy = True
        with self.plan() as planned:
            self.assertTrue(all(status == "failed" for status, _ in types.run(planned, True)))
        self.assertEqual(self.adapter.writes, [])

    def test_all_profile_fields_have_exact_nonoverlapping_four_build_layouts(self):
        manifest = json.loads((ROOT / "src/game/ida/types.json").read_text(encoding="utf-8"))
        self.assertEqual(len(manifest["profile_fields"]), 8)
        expected = {("XFiles_MainView", "children"): (2048, 2052),
                    ("XFiles_MainView", "canvas"): (2000, 2004),
                    ("XFiles_ScriptControlPrefix", "rectangle"): (408, 412),
                    ("XFiles_MainTextGroupPrefix", "viewport"): (152, 156),
                    ("XFiles_ChoiceList", "viewport"): (152, 156),
                    ("XFiles_ChoiceList", "tab"): (172, 176),
                    ("XFiles_ChoiceList", "scroll_position"): (308, 312),
                    ("XFiles_CreditTextOwnerPrefix", "position"): (192, 196)}
        sizes = {"children": 20, "canvas": 48, "rectangle": 20, "viewport": 20,
                 "tab": 20, "scroll_position": 4, "position": 12}
        for build in PROFILES["builds"]:
            projected = types.project(manifest, build["id"])
            types.validate(projected, ROOT)
            records = {r["name"]: r for r in projected["types"]}
            for (record_name, field_name), offsets in expected.items():
                record = records[record_name]
                field = next(f for f in record["fields"] if f["name"] == field_name)
                self.assertEqual(field["offset"], offsets[0 if build["id"] == "cd_10012" else 1])
                self.assertEqual(field["size"], sizes[field_name])
                self.assertLessEqual(field["offset"] + field["size"], record["size"])

    def test_profile_projection_rejects_overlap_with_fixed_fields(self):
        manifest = copy.deepcopy(self.manifest)
        manifest["profile_fields"] = [{"record": "XFiles_A", "field": "overlap",
                                       "offsets": {self.build["id"]: 0},
                                       "type": {"kind": "scalar", "name": "int"}}]
        with self.assertRaisesRegex(ValueError, "fixed canonical field"):
            types.project(manifest, self.build["id"])

    def test_real_manifest_cannot_drop_a_canonical_source(self):
        manifest = json.loads((ROOT / "src/game/ida/types.json").read_text(encoding="utf-8"))
        manifest["sources"].pop(next(iter(manifest["sources"])))
        with self.assertRaisesRegex(ValueError, "stale"):
            types.fresh(manifest, ROOT)

    def sdk_adapter(self):
        adapter = types.IdaAdapter.__new__(types.IdaAdapter)
        adapter.t = SimpleNamespace(udt_type_data_t=SdkVector, ptr_type_data_t=SimpleNamespace,
                                    array_type_data_t=SimpleNamespace, func_type_data_t=SdkVector,
                                    enum_type_data_t=SdkVector, BTE_SIZE_MASK=7, BTE_BITMASK=16,
                                    TAENUM_64BIT=1, TAENUM_SIGNED=32, TAENUM_UNSIGNED=64)
        return adapter

    def test_sdk_graph_comparison_resolves_cycles_without_cross_til_ordinals(self):
        adapter = self.sdk_adapter()
        left = SdkType("record", "XFiles_A", fields=[])
        other = SdkType("record", "XFiles_B", fields=[])
        left.data["fields"] = [("next", 0, 32, SdkType("pointer", to=other))]
        other.data["fields"] = [("next", 0, 32, SdkType("pointer", to=left))]
        right = copy.deepcopy(left)
        self.assertTrue(adapter.identical(left, right))
        right.data["fields"][0][3].data["to"].data["fields"][0] = (
            "next", 0, 32, SdkType(code="float32"))
        self.assertFalse(adapter.identical(left, right))

    def test_same_extent_scalar_array_qualifier_and_calling_convention_changes_conflict(self):
        adapter = self.sdk_adapter()
        integer, floating = SdkType(), SdkType(code="float32")
        self.assertFalse(adapter.identical(integer, floating))
        self.assertFalse(adapter.identical(SdkType("pointer", to=integer),
                                           SdkType("pointer", to=SdkType(const=True))))
        self.assertFalse(adapter.identical(SdkType("array", count=2, element=SdkType(size=2)),
                                           SdkType("array", count=1, element=integer)))
        self.assertFalse(adapter.identical(SdkType("function", cc="stdcall", params=[integer], **{"return": integer}),
                                           SdkType("function", cc="cdecl", params=[integer], **{"return": integer})))

    def test_enum_display_normalization_preserves_storage_bitmask_sign_and_values(self):
        adapter = self.sdk_adapter()
        original = SdkType("enum", "XFiles_Kind", bte=131, flags=64, values=[("one", 1)])
        displayed = copy.deepcopy(original)
        displayed.data["bte"] = 131 | 96
        self.assertTrue(adapter.identical(original, displayed))
        for key, value in (("bte", 131 | adapter.t.BTE_BITMASK), ("flags", 32),
                           ("values", [("one", 2)])):
            altered = copy.deepcopy(original)
            altered.data[key] = value
            self.assertFalse(adapter.identical(original, altered))

    def test_enums_are_installed_before_dependency_copying(self):
        enum = {"kind": "enum", "name": "XFiles_Kind", "size": 4, "align": 4,
                "underlying": {"kind": "scalar", "name": "unsigned int"},
                "values": [{"name": "one", "value": 1}]}
        self.manifest["types"].append(enum)
        self.manifest["types"][0]["fields"][0]["type"] = {"kind": "named", "name": "XFiles_Kind"}
        with self.plan() as planned:
            types.run(planned, True)
        self.assertEqual(self.adapter.writes[0], "XFiles_Kind")
        self.assertEqual(len(self.adapter.writes), 3)

    def test_source_line_endings_are_canonicalized_without_ignoring_content_changes(self):
        self.source.write_bytes(b"canonical fixture\n")
        with self.plan() as planned:
            self.assertTrue(all(status == "ready" for status, _ in types.run(planned)))
        self.source.write_bytes(b"canonical fixture\r\n")
        with self.plan() as planned:
            self.assertTrue(all(status == "ready" for status, _ in types.run(planned)))
        self.source.write_bytes(b"canonical changed\r\n")
        with self.assertRaisesRegex(ValueError, "stale"):
            self.plan()

    def test_changed_owned_forward_is_preserved_during_cycle_completion(self):
        def edit_pending(name):
            if name == "XFiles_A":
                self.adapter.local["XFiles_B"] = {"kind": "user", "size": 100}

        self.adapter.after_install = edit_pending
        with self.plan() as planned:
            results = dict((name, status) for status, name in types.run(planned, True))
        self.assertEqual(results["XFiles_B"], "changed_since_plan")
        self.assertEqual(self.adapter.local["XFiles_B"], {"kind": "user", "size": 100})
        self.assertNotIn("XFiles_B", self.adapter.writes)

    def sdk_enum_fixture(self):
        adapter = self.sdk_adapter()
        adapter.t.tinfo_t, adapter.t.TERR_OK, adapter.t.NTF_REPLACE = AutoBitmaskType, 0, 1
        adapter.t.BTE_ALWAYS = 128
        adapter.t.edm_t = lambda name, value: SimpleNamespace(name=name, value=value)
        record = {"kind": "enum", "name": "XFiles_Kind", "size": 4, "align": 4,
                  "underlying": {"kind": "scalar", "name": "unsigned int"},
                  "values": [{"name": "one", "value": 1}]}
        til = {}
        adapter.t.get_idati = lambda: til
        stored = adapter.define(til, record, replace=False)
        return adapter, til, record["name"], stored

    def test_sdk_ordinary_enum_survives_named_storage_auto_bitmask_heuristic(self):
        adapter, til, name, stored = self.sdk_enum_fixture()
        self.assertEqual(stored.data["bte"] & adapter.t.BTE_BITMASK, 0)
        self.assertEqual(til[name].data["bte"] & adapter.t.BTE_BITMASK, 0)

    def test_witnessed_enum_bitmask_drift_restores_existing_user_comments(self):
        adapter, til, name, _ = self.sdk_enum_fixture()
        til[name].data.update(comment="User type comment", comments={name + "_one": "User member comment"})
        before = adapter.enum_snapshots([name])
        til[name].data["bte"] |= adapter.t.BTE_BITMASK
        adapter.restore_enums(before)
        self.assertEqual(adapter.enum_snapshots([name]), before)

    def test_enum_restore_rejects_other_semantic_or_metadata_changes(self):
        for key, value in (("flags", 32), ("values", [("different", 7)]), ("comment", "Changed")):
            adapter, til, name, _ = self.sdk_enum_fixture()
            before = adapter.enum_snapshots([name])
            til[name].data["bte"] |= adapter.t.BTE_BITMASK
            til[name].data[key] = value
            with self.assertRaises(ValueError):
                adapter.restore_enums(before)
            self.assertTrue(til[name].data["bte"] & adapter.t.BTE_BITMASK)


if __name__ == "__main__":
    unittest.main()
