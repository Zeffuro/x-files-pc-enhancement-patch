"""Check canonical C++ type coverage and generate a complete MSVC layout comparison."""

import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_ida_export", ROOT / "src/game/ida/export_types.py")
export = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(export)
DATA = json.loads((ROOT / "src/game/ida/types.json").read_text(encoding="utf-8"))
PROFILES = json.loads((ROOT / "src/game/profiles/builds.json").read_text(encoding="utf-8"))
CATALOG = json.loads((ROOT / "src/game/functions/catalog.json").read_text(encoding="utf-8"))


def msvc_check_source(data):
    lines = [f'#include "{path.removeprefix("src/")}"' for path in data["coverage"]["headers"]]
    lines += ["#include <cstddef>", "#include <type_traits>", "static_assert(sizeof(void*) == 4);"]
    for index, row in enumerate(data["types"]):
        if row["kind"] == "opaque":
            continue
        name = f"CheckedType{index}"
        lines += [f"using {name} = {row['cpp_name']};",
                  f"static_assert(sizeof({name}) == {row['size']});",
                  f"static_assert(alignof({name}) == {row['align']});"]
        for field in row.get("fields", []):
            lines += [f"static_assert(offsetof({name}, {field['name']}) == {field['offset']});",
                      f"static_assert(sizeof(decltype({name}::{field['name']})) == {field['size']});"]
        if row["kind"] == "enum":
            for member in row["values"]:
                lines.append(f"static_assert(static_cast<long long>({name}::{member['name']}) == {member['value']});")
    for field in data["profile_fields"]:
        for build, offset in field["offsets"].items():
            expression = field["expression"].replace("build_profile->", "native_game::profile_" + build + ".")
            expression = expression.replace("sizeof(Rectangle)", "sizeof(native_game::Rectangle)")
            expression = expression.replace("offsetof(ChoiceViewportPrefix,", "offsetof(native_game::ChoiceViewportPrefix,")
            lines.append(f"static_assert({expression} == {offset});")
    return "\n".join(lines) + "\n"


def references(node):
    if node["kind"] == "named":
        yield node["name"]
    elif node["kind"] == "pointer":
        yield from references(node["to"])
    elif node["kind"] == "array":
        yield from references(node["element"])
    elif node["kind"] == "function":
        yield from references(node["return"])
        for parameter in node["params"]:
            yield from references(parameter)


class NativeTypeExportTest(unittest.TestCase):
    def test_source_hashes_are_equivalent_for_lf_crlf_and_mixed_checkouts(self):
        source = b"struct Example {\n    int field;\n};\n"
        crlf = source.replace(b"\n", b"\r\n")
        mixed = source.replace(b"\n", b"\r\n", 1)
        self.assertEqual(export.source_digest(source), export.source_digest(crlf))
        self.assertEqual(export.source_digest(source), export.source_digest(mixed))
        self.assertNotEqual(export.source_digest(source), export.source_digest(source.replace(b"int", b"short")))
        self.assertNotEqual(export.source_digest(b"first\rsecond"), export.source_digest(b"first\nsecond"))

    def test_source_hashes_cover_all_layouts_and_canonical_catalogs(self):
        export.check_fresh(DATA)
        actual = set(DATA["coverage"]["headers"])
        expected = {path.relative_to(ROOT).as_posix() for path in (ROOT / "src/game/layouts").rglob("*.h")}
        self.assertEqual(actual, expected)
        self.assertTrue(expected <= DATA["sources"].keys())
        for path in ("src/game/access/views.json", "src/game/functions/catalog.json",
                     "src/game/profiles/builds.json", "src/game/ida/export_types.py"):
            self.assertIn(path, DATA["sources"])
        for key in DATA["sources"]:
            stale = copy.deepcopy(DATA)
            stale["sources"][key] = "0" * 64
            with self.subTest(source=key), self.assertRaises(ValueError):
                export.check_fresh(stale)

    def test_generated_types_have_complete_nonoverlapping_member_extents(self):
        rows = {row["name"]: row for row in DATA["types"]}
        self.assertEqual(len(rows), len(DATA["types"]))
        self.assertTrue(all(name.startswith("XFiles_") for name in rows))
        for row in rows.values():
            if row["kind"] == "opaque":
                continue
            self.assertGreater(row["size"], 0)
            self.assertIn(row["align"], (1, 2, 4, 8, 16))
            previous = 0
            for field in row.get("fields", []):
                self.assertGreaterEqual(field["offset"], previous)
                self.assertGreater(field["size"], 0)
                previous = field["offset"] + field["size"]
                self.assertLessEqual(previous, row["size"])
                self.assertTrue(set(references(field["type"])) <= rows.keys())
            if row["kind"] == "alias":
                self.assertTrue(set(references(row["target"])) <= rows.keys())

    def test_nested_templates_and_only_true_forward_types_are_preserved(self):
        rows = {row["cpp_name"]: row for row in DATA["types"]}
        opaque = {name for name, row in rows.items() if row["kind"] == "opaque"}
        self.assertEqual(opaque, {"native_game::AiActionListCursor", "quickdraw::Port"})
        self.assertEqual(rows["native_game::InventoryIcon::Graphic::Resource"]["size"], 8)
        nodes = [row for name, row in rows.items() if name.startswith("native_game::List<") and name.endswith("::Node")]
        self.assertEqual(len(nodes), 5)
        for node in nodes:
            self.assertEqual(node["size"], 12)
            self.assertEqual([field["offset"] for field in node["fields"]], [0, 4, 8])
        parameters = [name for name in rows if name.startswith("native_game::StoryParameterArray<")]
        self.assertEqual(len(parameters), 4)
        self.assertTrue(all(row["status"] in ("exported", "concrete_instantiations_only")
                            for row in DATA["coverage"]["declarations"]))

    def test_every_catalog_signature_and_global_type_is_resolved(self):
        requested = {kind for _, kind in CATALOG["globals"]}
        for function in CATALOG["functions"] + CATALOG["vtable_functions"]:
            requested.add(function["return"])
            requested.update(kind for kind, _ in function["params"])
        self.assertTrue(requested <= DATA["cpp_names"].keys())
        self.assertEqual(DATA["cpp_names"]["DrawList"]["to"]["convention"], "__stdcall")
        row = next(row for row in DATA["types"] if row["cpp_name"] == "native_game::CreditGroupVTable::Draw")
        self.assertEqual(row["target"]["to"]["convention"], "__stdcall")
        self.assertEqual(len(row["target"]["to"]["params"]), 3)

    def test_all_profile_fields_use_compiler_evaluated_original_expressions(self):
        views = json.loads((ROOT / "src/game/access/views.json").read_text(encoding="utf-8"))
        expected = export.profile_entries(views)
        self.assertEqual(len(DATA["profile_fields"]), len(expected))
        for actual, original in zip(DATA["profile_fields"], expected):
            self.assertEqual(actual["expression"], original["expression"])
            self.assertEqual(actual["cpp_type"], original["cpp_type"])
            self.assertEqual(set(actual["offsets"]), {build["id"] for build in PROFILES["builds"]})
            if "+" not in original["expression"]:
                key = original["expression"].removeprefix("build_profile->")
                self.assertEqual(actual["offsets"], {build["id"]: build["offsets"][key] for build in PROFILES["builds"]})

    def test_wrong_target_and_stale_schema_fail_closed(self):
        for key, value in (("schema", 2), ("target", {"triple": "x86_64-pc-windows-msvc", "pointer_size": 8})):
            stale = copy.deepcopy(DATA)
            stale[key] = value
            with self.assertRaises(ValueError):
                export.check_fresh(stale)

    def test_msvc_comparison_covers_every_concrete_type_and_member(self):
        source = msvc_check_source(DATA)
        for index, row in enumerate(DATA["types"]):
            if row["kind"] != "opaque":
                self.assertIn(f"using CheckedType{index} = {row['cpp_name']};", source)
                for field in row.get("fields", []):
                    self.assertIn(f"offsetof(CheckedType{index}, {field['name']})", source)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--msvc-check":
        Path(sys.argv[2]).write_text(msvc_check_source(DATA), encoding="utf-8")
    else:
        unittest.main()
