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
                                             "canvas": 0x7D4,
                                             "main_text_rectangle": 0x9C})

    def test_native_corner_addresses_cover_all_profiles(self):
        for build, vtable, rectangle in zip(
                self.data["builds"],
                (0x254AD8, 0x256B28, 0x2563E0, 0x257B20),
                (0x2B94D8, 0x2BC4E0, 0x2BC838, 0x2BD500)):
            self.assertEqual(build["rvas"]["menu_corner"], vtable)
            self.assertEqual(build["rvas"]["menu_corner_rectangle"], rectangle)

    def test_world_interaction_types_cover_all_profiles(self):
        expected = ((0x255F10, 0x254790, 0x259330, 0x263BD0, 0x263AA8, 0x261A58),
                    (0x257F70, 0x2567D8, 0x25B680, 0x2660D8, 0x2662A8, 0x264118),
                    (0x25B798, 0x258D20, 0x25C9E8, 0x267F80, 0x268150, 0x2651D0),
                    (0x258F68, 0x2577D0, 0x25C678, 0x267368, 0x267240, 0x265120))
        for build, values in zip(self.data["builds"], expected):
            self.assertEqual(tuple(build["rvas"][field] for field in
                                   ("world_picture", "world_hotspot", "registry_container",
                                    "conversation_association", "conversation_association_tree",
                                    "association_id_node")), values)

    def test_world_cursor_types_cover_all_profiles(self):
        fields = ("world_hover", "default_inventory_action", "cursor_object", "cursor_resource",
                  "cursor_group", "cursor_group_list", "world_hotspot_resource")
        expected = ((0x2b2acc, 0x2b16b8, 0x25aaa0, 0x265a28, 0x25c5c0, 0x25bf00, 0x2646d8),
                    (0x2b5af4, 0x2b46e8, 0x25d218, 0x2681b8, 0x25ed58, 0x25e698, 0x266e68),
                    (0x2b6608, 0x2b6544, 0x258150, 0x266ba8, 0x25f268, 0x25f400, 0x266cf8),
                    (0x2b6b00, 0x2b56e8, 0x25e218, 0x2691c0, 0x25fd58, 0x25f800, 0x267e70))
        for build, values in zip(self.data["builds"], expected):
            self.assertEqual(tuple(build["rvas"][field] for field in fields), values)

    def test_configured_picture_resource_types_cover_all_profiles(self):
        expected = ((0x264658, 0x265a98), (0x266de8, 0x268228),
                    (0x267f10, 0x267b30), (0x267df0, 0x269230))
        for build, values in zip(self.data["builds"], expected):
            self.assertEqual(tuple(build["rvas"][field] for field in
                                   ("world_picture_resource", "conversation_resource")), values)

    def test_navigation_cursor_types_cover_all_profiles(self):
        expected = ((0x25a780, 0x266f68, 0x266ef8), (0x25cef0, 0x2696f8, 0x269688),
                    (0x258440, 0x266c88, 0x266c18), (0x25dee8, 0x26a700, 0x26a690))
        fields = ("world_navigation", "world_navigation_resource", "world_navigation_shape_resource")
        for build, values in zip(self.data["builds"], expected):
            self.assertEqual(tuple(build["rvas"][field] for field in fields), values)

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
