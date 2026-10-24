"""Exercise supplemental naming against a rebased, mutable IDA adapter."""

import unittest
from unittest.mock import patch

from game_ida import bridge, DATA, FakeIda


SYMBOLS = bridge._symbols()
CATALOG = SYMBOLS.load(DATA)


class ResearchIda(FakeIda):
    def __init__(self, build, base=0x910000):
        super().__init__(build["sha256"], base)
        self.build = build
        self.symbols = [dict(row, name=row["source_name"] if row["kind"] == "function" else row["name"])
                        for row in SYMBOLS.rows(CATALOG, build["id"])]
        self.memory = {}
        self.code = set()
        self.unreadable = set()
        for symbol in self.symbols:
            ea = base + symbol["rva"]
            if symbol["kind"] == "function":
                self.code.add(ea)
            fingerprint = symbol["fingerprint"]
            if fingerprint is None:
                continue
            values = bytes.fromhex(fingerprint["bytes"])
            mask = bytes.fromhex(fingerprint["mask"])
            for offset, value in enumerate(values):
                if mask[offset]:
                    previous = self.memory.get(ea + offset, value)
                    assert previous == value
                    self.memory[ea + offset] = value
            for pointer in fingerprint["pointers"]:
                values = (base + pointer["rva"]).to_bytes(4, "little")
                for offset, value in enumerate(values, pointer["offset"]):
                    self.memory[ea + offset] = value

    def is_data_range(self, ea, size):
        return all(self.is_mapped(address) and address not in self.code
                   for address in range(ea, ea + size))

    def read_bytes(self, ea, size):
        if ea in self.unreadable:
            return None
        return bytes(self.memory.get(address, 0) for address in range(ea, ea + size))


class ResearchImportTest(unittest.TestCase):
    def setUp(self):
        self.build = next(row for row in DATA["builds"] if row["id"] == "dvd_20000")
        self.ida = ResearchIda(self.build)

    def symbol(self, name):
        return next(row for row in self.ida.symbols if row["source_name"] == name)

    def outcome(self, rows, name):
        return next(row for row in rows if row[2] == name)

    def test_full_catalog_preview_apply_and_idempotence_all_builds(self):
        for build in DATA["builds"]:
            with self.subTest(build=build["id"]):
                ida = ResearchIda(build)
                preview = bridge.run(adapter=ida)
                self.assertEqual(len(preview), len(build["rvas"]) + len(ida.symbols))
                self.assertTrue(all(row[0] in ("ready", "alias") for row in preview))
                self.assertEqual(ida.writes, [])
                outcomes = bridge.run(apply=True, adapter=ida)
                self.assertTrue(all(row[0] in ("applied", "alias") for row in outcomes))
                count = len(ida.writes)
                bridge.run(apply=True, adapter=ida)
                self.assertEqual(count, len(ida.writes))
                self.assertEqual(len({ea for ea, _ in ida.writes}), count)

    def test_native_and_table_names_win_shared_addresses(self):
        outcomes = bridge.run(apply=True, adapter=self.ida)
        for name in ("vc_queue_action_list", "g_ing_phones", "g_phone_book"):
            symbol = self.symbol(name)
            self.assertEqual(self.outcome(outcomes, symbol["name"])[0], "alias")
        self.assertEqual(self.ida.names[self.ida.base + self.build["rvas"]["cheat_table"]],
                         "XFiles_cheat_table")
        for name in ("ING_PHONES", "PHONES"):
            symbol = self.symbol(name)
            self.assertEqual(self.ida.names[self.ida.base + symbol["rva"]], symbol["name"])

    def test_user_names_collisions_and_patched_bytes_are_preserved(self):
        first, second, third = [self.symbol(name) for name in
                                ("operator_new", "heap_alloc_retry", "neo_class_register")]
        self.ida.user.add(self.ida.base + first["rva"])
        self.ida.names[self.ida.base + first["rva"]] = "MyAllocator"
        self.ida.names[0x9000000] = second["name"]
        self.ida.memory[self.ida.base + third["rva"]] ^= 0xff
        rows = bridge.run(apply=True, adapter=self.ida)
        self.assertEqual(self.outcome(rows, first["name"])[0], "user_name")
        self.assertEqual(self.outcome(rows, second["name"])[0], "collision")
        self.assertEqual(self.outcome(rows, third["name"])[0], "fingerprint_mismatch")
        self.assertEqual(self.ida.names[self.ida.base + first["rva"]], "MyAllocator")
        written = {ea for ea, _ in self.ida.writes}
        self.assertTrue(all(self.ida.base + symbol["rva"] not in written
                            for symbol in (first, second, third)))

    def test_function_boundaries_and_full_data_extent_are_guarded(self):
        function = self.symbol("operator_new")
        table = self.symbol("ING_RECORDS")
        bss = next(row for row in self.ida.symbols if row["zero_fill"])
        self.ida.not_functions.add(self.ida.base + function["rva"])
        self.ida.code.add(self.ida.base + table["rva"] + table["size"] - 1)
        self.ida.code.add(self.ida.base + bss["rva"])
        rows = bridge.run(apply=True, adapter=self.ida)
        self.assertEqual(self.outcome(rows, function["name"])[0], "not_function")
        self.assertEqual(self.outcome(rows, table["name"])[0], "not_data")
        self.assertEqual(self.outcome(rows, bss["name"])[0], "not_data")

    def test_unreadable_prefix_and_changed_rebased_pointer_are_rejected(self):
        function = self.symbol("operator_new")
        table = self.symbol("ENCLOSURES")
        self.ida.unreadable.add(self.ida.base + function["rva"])
        pointer = table["fingerprint"]["pointers"][0]
        self.ida.memory[self.ida.base + table["rva"] + pointer["offset"]] ^= 1
        rows = bridge.run(apply=True, adapter=self.ida)
        for symbol in (function, table):
            self.assertEqual(self.outcome(rows, symbol["name"])[0], "fingerprint_mismatch")

    def test_fingerprint_rechecked_before_writing(self):
        symbol = self.symbol("operator_new")
        ea = self.ida.base + symbol["rva"]
        original = self.ida.read_bytes
        calls = 0

        def change(address, size):
            nonlocal calls
            if address == ea:
                calls += 1
                if calls > 1:
                    return b"\xff" * size
            return original(address, size)

        self.ida.read_bytes = change
        rows = bridge.run(apply=True, adapter=self.ida)
        self.assertEqual(self.outcome(rows, symbol["name"])[0], "changed_since_plan")
        self.assertNotIn(ea, self.ida.names)

    def test_bad_supplement_stops_all_writes_and_native_only_option(self):
        with patch.object(bridge, "_symbols") as loader:
            loader.return_value.load.side_effect = ValueError("Invalid supplemental catalog")
            self.assertEqual(bridge.run(apply=True, adapter=self.ida), [])
            self.assertEqual(self.ida.writes, [])
        rows = bridge.run(apply=True, adapter=self.ida, research=False)
        self.assertEqual(len(rows), len(self.build["rvas"]))
        self.assertFalse(any(name.startswith("XFiles_re_") or name.startswith("XFiles_table_")
                             for _, name in self.ida.writes))

    def test_exact_legacy_function_names_migrate_all_builds_and_repeat_without_writes(self):
        for build in DATA["builds"]:
            with self.subTest(build=build["id"]):
                ida = ResearchIda(build)
                _, decisions, definitions, _ = bridge._plan(DATA, ida)
                functions, data = {}, {}
                for decision, symbol in zip(decisions, definitions):
                    if decision[0] != "ready":
                        continue
                    address = decision[1]
                    if symbol["kind"] == "function":
                        functions[address] = symbol["name"]
                        ida.names[address] = symbol["legacy_name"]
                    else:
                        data[address] = symbol["name"]
                        ida.names[address] = symbol["name"]
                    ida.user.add(address)
                preview = bridge.run(adapter=ida)
                self.assertTrue(all(status == "migrate" for status, ea, _, _ in preview if ea in functions
                                    and status != "alias"))
                self.assertEqual(ida.writes, [])
                bridge.run(True, ida)
                self.assertTrue(all(ida.names[ea] == name and not name.startswith("XFiles_")
                                    for ea, name in functions.items()))
                self.assertTrue(all(ida.names[ea] == name and name.startswith("XFiles_")
                                    for ea, name in data.items()))
                count = len(ida.writes)
                bridge.run(True, ida)
                self.assertEqual(len(ida.writes), count)

    def test_wrong_address_legacy_names_custom_names_and_bare_collisions_are_preserved(self):
        cases = (("XFiles_re_operator_new", False, "user_name"),
                 ("XFiles_custom_allocator", False, "user_name"),
                 ("XFiles_re_heap_alloc_retry", True, "collision"))
        for current, collision, expected in cases:
            ida = ResearchIda(self.build)
            symbol = next(row for row in ida.symbols if row["source_name"] == "heap_alloc_retry")
            ea = ida.base + symbol["rva"]
            ida.names[ea] = current
            ida.user.add(ea)
            if collision:
                ida.names[0x9000000] = symbol["name"]
            result = bridge.run(True, ida)
            self.assertEqual(self.outcome(result, symbol["name"])[0], expected)
            self.assertEqual(ida.names[ea], current)
            self.assertNotIn(ea, [address for address, _ in ida.writes])

    def test_verified_legacy_alias_migrates_to_native_primary_name(self):
        symbol = self.symbol("vc_queue_action_list")
        address = self.ida.base + symbol["rva"]
        self.ida.names[address] = "XFiles_re_" + symbol["source_name"]
        self.ida.user.add(address)
        result = bridge.run(True, self.ida)
        self.assertEqual(self.outcome(result, "queue_action_list")[0], "applied")
        self.assertEqual(self.ida.names[address], "queue_action_list")
        self.assertEqual(self.outcome(result, symbol["name"])[0], "alias")

    def test_legacy_migration_rechecks_name_owner_bytes_and_function_entry(self):
        for change in ("name", "owner", "bytes", "entry"):
            ida = ResearchIda(self.build)
            symbol = next(row for row in ida.symbols if row["source_name"] == "action_list_storage")
            address = ida.base + symbol["rva"]
            ida.names[address] = "XFiles_re_" + symbol["source_name"]
            ida.user.add(address)
            original = bridge._plan

            def mutate(*args, **kwargs):
                result = original(*args, **kwargs)
                if change == "name":
                    ida.names[address] = "CustomAfterPlan"
                elif change == "owner":
                    ida.names[0x9000000] = symbol["name"]
                elif change == "bytes":
                    ida.memory[address] ^= 1
                else:
                    ida.not_functions.add(address)
                return result

            with patch.object(bridge, "_plan", side_effect=mutate):
                result = bridge.run(True, ida)
            self.assertEqual(self.outcome(result, symbol["name"])[0], "changed_since_plan")
            self.assertNotIn(address, [ea for ea, _ in ida.writes])


if __name__ == "__main__":
    unittest.main()
