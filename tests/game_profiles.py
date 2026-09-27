"""Check generated native mappings and reject malformed profile data."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_generate", ROOT / "src/game/scripts/generate_profiles.py")
generate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generate)


class GameProfilesTest(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((ROOT / "src/game/profiles/builds.json").read_text(encoding="utf-8"))
        self.functions = json.loads((ROOT / "src/game/profiles/functions.json").read_text(encoding="utf-8"))

    def test_existing_tables_and_identified_build(self):
        generate.validate(self.data)
        generate.validate_functions(self.functions, self.data)
        builds = self.data["builds"]
        self.assertEqual(builds[0]["rvas"]["application"], 0x2B2ECC)
        self.assertEqual(builds[0]["offsets"]["children"], 0x800)
        self.assertEqual(builds[1]["sha256"],
                         "d1cfec26c69c03a41d3a0e81783a11b7892bc6846f6326b306f99f2ed534e1d6")
        self.assertEqual(builds[2]["rvas"]["application"], 0x2B6FEC)
        self.assertEqual(builds[2]["offsets"]["children"], 0x804)
        self.assertEqual((ROOT / "src/game/profiles/generated.h").read_text(encoding="utf-8"),
                         generate.render(self.data))
        self.assertIn("return nullptr;", generate.render(self.data))

    def test_japanese_build_has_verified_native_layout(self):
        build = next(row for row in self.data["builds"] if row["id"] == "cd_10020")
        self.assertEqual(build["sha256"],
                         "0f8b654cb78f6ae66d62bb173fae79614255ad46cdc25827bbaa98d7d0d79899")
        self.assertEqual(build["rvas"]["draw_slot"], 0x2620FC)
        self.assertEqual(build["rvas"]["remove"], 0x32880)
        self.assertEqual(build["offsets"], {"children": 0x804,
                                             "control_rectangle": 0x19C,
                                             "choice_viewport": 0x9C,
                                             "credit_position": 0xC4,
                                             "canvas": 0x7D4})

    def test_rejects_wrong_kind_and_partial_profiles(self):
        wrong_kind = copy.deepcopy(self.data)
        wrong_kind["builds"][0]["offsets"]["application"] = 0x100
        with self.assertRaises(ValueError):
            generate.validate(wrong_kind)
        partial = copy.deepcopy(self.data)
        partial["builds"][0]["offsets"] = None
        with self.assertRaises(ValueError):
            generate.validate(partial)

    def test_rejects_unknown_and_duplicate_hashes(self):
        wrong_hash = copy.deepcopy(self.data)
        wrong_hash["builds"][0]["sha256"] = "z" * 64
        with self.assertRaises(ValueError):
            generate.validate(wrong_hash)
        duplicate = copy.deepcopy(self.data)
        duplicate["builds"][1]["sha256"] = duplicate["builds"][0]["sha256"]
        with self.assertRaises(ValueError):
            generate.validate(duplicate)
        invalid_label = copy.deepcopy(self.data)
        invalid_label["builds"][0]["label"] = 'CD"; injected'
        with self.assertRaises(ValueError):
            generate.validate(invalid_label)

    def test_function_catalog_references_verified_profile_fields(self):
        duplicate = copy.deepcopy(self.functions)
        duplicate["functions"].append(duplicate["functions"][0])
        with self.assertRaisesRegex(ValueError, "Function fields"):
            generate.validate_functions(duplicate, self.data)
        bad_field = copy.deepcopy(self.functions)
        bad_field["functions"][0]["field"] = "not_a_profile_field"
        with self.assertRaisesRegex(ValueError, "Function fields"):
            generate.validate_functions(bad_field, self.data)
        extra = copy.deepcopy(self.functions)
        extra["functions"][0]["rva"] = 0x1234
        with self.assertRaisesRegex(ValueError, "entry"):
            generate.validate_functions(extra, self.data)


if __name__ == "__main__":
    unittest.main()
