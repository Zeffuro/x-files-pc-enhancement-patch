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
        self.symbols = SYMBOLS.rows(CATALOG, build["id"])
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


if __name__ == "__main__":
    unittest.main()
