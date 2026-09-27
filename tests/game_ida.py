"""Exercise the IDA bridge without loading IDA or changing a database."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_ida_import", ROOT / "src/game/scripts/ida_import.py")
bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bridge)
DATA = json.loads((ROOT / "src/game/profiles/builds.json").read_text(encoding="utf-8"))


class FakeIda:
    def __init__(self, digest, base=0x710000):
        self.digest = digest
        self.base = base
        self.names = {}
        self.user = set()
        self.missing = set()
        self.not_functions = set()
        self.writes = []
        self.messages = []
        self.bits = 32

    def wait(self):
        pass

    def input_sha256(self):
        return bytes.fromhex(self.digest)

    def is_32bit(self):
        return self.bits == 32

    def imagebase(self):
        return self.base

    def is_mapped(self, ea):
        return ea not in self.missing

    def is_function(self, ea):
        return ea not in self.not_functions

    def name(self, ea):
        return self.names.get(ea, "")

    def has_user_name(self, ea):
        return ea in self.user

    def name_owner(self, name):
        return next((ea for ea, value in self.names.items() if value == name), None)

    def set_name(self, ea, name):
        self.names[ea] = name
        self.writes.append((ea, name))
        return True

    def log(self, message):
        self.messages.append(message)


class IdaImportTest(unittest.TestCase):
    def setUp(self):
        self.data = copy.deepcopy(DATA)
        self.build = self.data["builds"][0]
        self.ida = FakeIda(self.build["sha256"])

    def test_rebased_rvas_and_offsets_excluded(self):
        build, rows = bridge.plan(self.data, self.ida)
        self.assertEqual(build["id"], "cd_10012")
        actual = {name: ea for _, ea, name, _ in rows}
        self.assertEqual(actual["XFiles_application"],
                         self.ida.base + self.build["rvas"]["application"])
        self.assertNotIn("XFiles_children", actual)
        self.assertNotIn("XFiles_control_rectangle", actual)
        self.assertNotIn("XFiles_choice_viewport", actual)

    def test_japanese_hash_selects_its_function_addresses(self):
        japanese = next(row for row in self.data["builds"] if row["id"] == "cd_10020")
        ida = FakeIda(japanese["sha256"], base=0x730000)
        build, rows = bridge.plan(self.data, ida)
        actual = {name: ea for _, ea, name, _ in rows}
        self.assertEqual(build["id"], "cd_10020")
        self.assertEqual(actual["XFiles_application"], ida.base + 0x2B6F04)
        self.assertEqual(actual["XFiles_draw_list"], ida.base + 0x16E60)
        self.assertEqual(actual["XFiles_remove"], ida.base + 0x32880)
        self.assertEqual(ida.writes, [])

    def test_wrong_hash_unmapped_profile_and_architecture_fail_closed(self):
        self.ida.digest = "0" * 64
        with self.assertRaisesRegex(ValueError, "unknown"):
            bridge.plan(self.data, self.ida)
        unmapped = self.data["builds"][1]
        self.ida.digest = unmapped["sha256"]
        unmapped["rvas"] = unmapped["offsets"] = None
        with self.assertRaisesRegex(ValueError, "no verified"):
            bridge.plan(self.data, self.ida)
        self.ida.bits = 64
        with self.assertRaisesRegex(ValueError, "32-bit"):
            bridge.plan(self.data, self.ida)

    def test_user_name_collision_preview_and_idempotent_apply(self):
        rows = bridge.plan(self.data, self.ida)[1]
        first = rows[0]
        second = rows[1]
        third = rows[2]
        self.ida.names[first[1]] = "AnalystFunction"
        self.ida.user.add(first[1])
        self.ida.names[0x900000] = second[2]
        self.ida.missing.add(third[1])
        preview = bridge.plan(self.data, self.ida)[1]
        self.assertEqual([preview[i][0] for i in range(3)],
                         ["user_name", "collision", "unmapped"])
        self.assertEqual(self.ida.writes, [])
        outcomes = bridge.run(apply=True, adapter=self.ida)
        self.assertEqual([outcomes[i][0] for i in range(3)],
                         ["user_name", "collision", "unmapped"])
        self.assertEqual(self.ida.names[first[1]], "AnalystFunction")
        self.assertNotIn(second[1], self.ida.names)
        self.assertTrue(self.ida.writes)
        writes = len(self.ida.writes)
        bridge.run(apply=True, adapter=self.ida)
        self.assertEqual(len(self.ida.writes), writes)

    def test_default_run_only_previews(self):
        outcomes = bridge.run(adapter=self.ida)
        self.assertTrue(any(row[0] == "ready" for row in outcomes))
        self.assertEqual(self.ida.writes, [])

    def test_function_catalog_requires_function_start(self):
        ea = self.ida.base + self.build["rvas"]["draw_list"]
        self.ida.not_functions.add(ea)
        outcomes = bridge.run(apply=True, adapter=self.ida)
        row = next(row for row in outcomes if row[2] == "XFiles_draw_list")
        self.assertEqual(row[0], "not_function")
        self.assertNotIn(ea, self.ida.names)
        self.assertTrue(any("Draw a dialogue choice list" in line for line in self.ida.messages))

    def test_function_start_rechecked_before_write(self):
        ea = self.ida.base + self.build["rvas"]["draw_list"]
        original = self.ida.is_function
        calls = 0

        def changing(address):
            nonlocal calls
            calls += 1
            return address != ea or calls == 1

        self.ida.is_function = changing
        outcomes = bridge.run(apply=True, adapter=self.ida)
        row = next(row for row in outcomes if row[2] == "XFiles_draw_list")
        self.assertEqual(row[0], "changed_since_plan")
        self.assertNotIn(ea, self.ida.names)
        self.ida.is_function = original

    def test_rebase_during_apply_stops_following_writes(self):
        original = self.ida.set_name

        def rebase_after_first(ea, name):
            result = original(ea, name)
            self.ida.base += 0x10000
            return result

        self.ida.set_name = rebase_after_first
        outcomes = bridge.run(apply=True, adapter=self.ida)
        self.assertEqual(outcomes[0][0], "applied")
        self.assertEqual(outcomes[1][0], "changed_since_plan")
        self.assertEqual(len(self.ida.writes), 1)


if __name__ == "__main__":
    unittest.main()
