"""Check supplemental names and original-image guards without IDA or game assets."""

import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("game_ida_symbols", ROOT / "src/game/scripts/ida_symbols.py")
symbols = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(symbols)
PROFILES = json.loads((ROOT / "src/game/profiles/builds.json").read_text(encoding="utf-8"))
CATALOG = symbols.load(PROFILES)


def image_prefix(symbol, base):
    fingerprint = symbol["fingerprint"]
    actual = bytearray.fromhex(fingerprint["bytes"])
    for pointer in fingerprint["pointers"]:
        offset = pointer["offset"]
        actual[offset:offset + 4] = (base + pointer["rva"]).to_bytes(4, "little")
    return actual


def reader(symbol, base, prefix):
    def read(ea, length):
        if ea != base + symbol["rva"] or length != len(prefix):
            raise ValueError("Unexpected read range")
        return bytes(prefix)
    return read


class IdaSymbolsTest(unittest.TestCase):
    def setUp(self):
        self.catalog = copy.deepcopy(CATALOG)
        self.build = self.catalog["builds"]["dvd_20000"]
        self.symbol = self.build["symbols"][0]

    def test_complete_catalog_and_build_identity_coverage(self):
        self.assertEqual(set(CATALOG["builds"]), {build["id"] for build in PROFILES["builds"]})
        for build in PROFILES["builds"]:
            rows = symbols.rows(CATALOG, build["id"])
            self.assertEqual(CATALOG["builds"][build["id"]]["sha256"], build["sha256"])
            tables = [row for row in rows if row["source"] == "shiloh"]
            self.assertEqual(len(tables), 41)
            self.assertEqual(sum(row["shape"]["rows"] for row in tables), 449)
            self.assertEqual(rows[:41], tables)
            self.assertTrue(all(row["kind"] == "data" for row in tables))
            counts = {"cd_10012": 472, "cd_10019": 505, "cd_10020": 500, "dvd_20000": 608}
            self.assertEqual(len(rows), counts[build["id"]])
        names = [row for row in self.build["symbols"] if row["source"] == "xesf"]
        self.assertEqual(len(names), 555)
        self.assertEqual(len({row["rva"] for row in names}), 555)
        self.assertEqual(len({row["name"] for row in names}), 555)
        self.assertEqual(sum(row["kind"] == "function" for row in names), 541)
        self.assertEqual(sum(row["kind"] == "data" for row in names), 14)
        self.assertEqual(sum(row["source_name"].startswith("fn_") for row in names), 204)
        self.assertIn("fn_get_argument", {row["source_name"] for row in names})
        self.assertTrue(all(row["name"] == "XFiles_re_" + row["source_name"] for row in names))
        self.assertEqual(sum(row["fingerprint"] is None for row in names), 9)
        self.assertTrue(all(row["kind"] == "data" and row["zero_fill"] for row in names if row["fingerprint"] is None))

    def test_cd_labels_share_verified_dvd_names_and_cover_registries(self):
        original = {row["source_name"]: row for row in self.build["symbols"] if row["source"] == "xesf"}
        builtins = {name for name in original if name.startswith("fn_")} - {"fn_get_argument"}
        data = {name for name, row in original.items() if row["kind"] == "data"}
        self.assertEqual(len(builtins), 203)
        for build_id, count in (("cd_10012", 399), ("cd_10019", 432), ("cd_10020", 427)):
            with self.subTest(build=build_id):
                rows = [row for row in symbols.rows(CATALOG, build_id) if row["source"] == "xesf"]
                names = {row["source_name"] for row in rows}
                self.assertTrue(builtins <= names <= original.keys())
                self.assertEqual({row["source_name"] for row in rows if row["kind"] == "data"}, data)
                self.assertEqual(sum(row["kind"] == "function" for row in rows), count)
                self.assertEqual(sum(row["zero_fill"] for row in rows), 9)
                for row in rows:
                    self.assertEqual(row["kind"], original[row["source_name"]]["kind"])
                    self.assertEqual(row["name"], original[row["source_name"]]["name"])

    def test_native_profile_and_table_aliases_are_retained(self):
        names = [row for row in self.build["symbols"] if row["source"] == "xesf"]
        native = next(row for row in PROFILES["builds"] if row["id"] == "dvd_20000")
        self.assertEqual(sum(row["rva"] in native["rvas"].values() for row in names), 9)
        for source_name, table_name in (("g_ing_phones", "ING_PHONES"), ("g_phone_book", "PHONES")):
            rows = self.build["symbols"]
            label = next(row for row in rows if row["source_name"] == source_name)
            table = next(row for row in rows if row["source_name"] == table_name)
            self.assertEqual(label["rva"], table["rva"])
            self.assertLess(rows.index(table), rows.index(label))

    def test_every_prefix_matches_at_original_and_rebased_addresses(self):
        for build_id in CATALOG["builds"]:
            for row in symbols.rows(CATALOG, build_id):
                for base in (0x400000, 0x710000):
                    with self.subTest(build=build_id, name=row["name"], base=base):
                        if row["fingerprint"] is None:
                            self.assertTrue(symbols.matches(row, base, mock.Mock(side_effect=AssertionError("BSS read"))))
                        else:
                            prefix = image_prefix(row, base)
                            self.assertTrue(symbols.matches(row, base, reader(row, base, prefix)))

    def test_every_prefix_rejects_changed_checked_bytes(self):
        for build_id in CATALOG["builds"]:
            for row in symbols.rows(CATALOG, build_id):
                fp = row["fingerprint"]
                if fp is None:
                    continue
                with self.subTest(build=build_id, name=row["name"]):
                    prefix = image_prefix(row, 0x710000)
                    checked = next((i for i, value in enumerate(bytes.fromhex(fp["mask"])) if value), None)
                    if checked is None:
                        checked = fp["pointers"][0]["offset"]
                    prefix[checked] ^= 1
                    self.assertFalse(symbols.matches(row, 0x710000, reader(row, 0x710000, prefix)))

    def test_function_relocation_bytes_can_change(self):
        row = next(row for row in self.build["symbols"] if row["kind"] == "function" and "00" in row["fingerprint"]["mask"])
        prefix = image_prefix(row, 0x710000)
        mask = bytes.fromhex(row["fingerprint"]["mask"])
        for offset, enabled in enumerate(mask):
            if not enabled:
                prefix[offset] ^= 0x5a
        self.assertTrue(symbols.matches(row, 0x710000, reader(row, 0x710000, prefix)))

    def test_pointer_slots_reject_original_base_and_wrong_target(self):
        row = next(row for row in self.build["symbols"] if row["fingerprint"] and row["fingerprint"]["pointers"])
        original = image_prefix(row, 0x400000)
        self.assertFalse(symbols.matches(row, 0x710000, reader(row, 0x710000, original)))
        rebased = image_prefix(row, 0x710000)
        rebased[row["fingerprint"]["pointers"][0]["offset"]] ^= 1
        self.assertFalse(symbols.matches(row, 0x710000, reader(row, 0x710000, rebased)))

    def test_pointer_only_enclosures_have_verified_targets(self):
        row = next(row for row in self.build["symbols"] if row["source_name"] == "ENCLOSURES")
        self.assertEqual(bytes.fromhex(row["fingerprint"]["mask"]), bytes(32))
        self.assertEqual(len(row["fingerprint"]["pointers"]), 8)
        symbols.validate(self.catalog, PROFILES)
        row["fingerprint"]["pointers"] = []
        with self.assertRaisesRegex(ValueError, "checked bytes"):
            symbols.validate(self.catalog, PROFILES)

    def test_schema_build_hash_and_source_fail_closed(self):
        cases = [
            lambda c: c.update(schema=2),
            lambda c: c.update(schema=True),
            lambda c: c.update(extra=1),
            lambda c: c.update(sources=[]),
            lambda c: c["sources"].update(xesf="https://github.com/xesf/the-x-files-game/blob/main/tools/ida/names.txt"),
            lambda c: c["builds"].update(unknown=copy.deepcopy(self.build)),
            lambda c: c["builds"]["dvd_20000"].update(sha256="0" * 64),
            lambda c: c["builds"]["dvd_20000"].update(symbols={}),
        ]
        for mutate in cases:
            catalog = copy.deepcopy(CATALOG)
            mutate(catalog)
            with self.subTest(mutation=cases.index(mutate)), self.assertRaises(ValueError):
                symbols.validate(catalog, PROFILES)
        for malformed in (None, [], {}, {"schema": 1, "sources": {}, "builds": {}}):
            with self.subTest(catalog=malformed), self.assertRaises(ValueError):
                symbols.validate(malformed, PROFILES)

    def test_original_binary_sources_require_exact_profile_identities(self):
        source = self.catalog["sources"]["original"]
        self.assertEqual(source, {"kind": "original_binary", "builds": {
            build["id"]: build["sha256"] for build in PROFILES["builds"]}})
        for build_id in CATALOG["builds"]:
            rows = [row for row in symbols.rows(CATALOG, build_id) if row["source"] == "original"]
            expected = {"ai_list_clear", "ai_list_set_udf", "ai_glow_set_udf", "ai_glow_view_get",
                        "action_list_storage", "action_list_ensure_cursor", "ai_icon_click",
                        "ai_list_cycle", "ai_glow_view_dispatch", "action_list_current",
                        "action_list_restore_position", "TriggerHandler_handle_timer"}
            if build_id != "dvd_20000":
                expected |= {"ai_list_get", "ai_trigger_get", "ai_icon_show",
                             "timer_fired_callback", "timers_post_fired", "TimerEvent_ctor"}
            self.assertEqual({row["source_name"] for row in rows}, expected)
            self.assertTrue(all(row["kind"] == "function" and row["fingerprint"] for row in rows))
        cases = [
            lambda source: source.update(kind="binary"),
            lambda source: source.update(extra=True),
            lambda source: source.update(builds={}),
            lambda source: source["builds"].update(dvd_20000="0" * 64),
            lambda source: source["builds"].update(unknown=PROFILES["builds"][0]["sha256"]),
            lambda source: source["builds"].pop("dvd_20000"),
        ]
        for mutate in cases:
            catalog = copy.deepcopy(CATALOG)
            mutate(catalog["sources"]["original"])
            with self.subTest(mutation=cases.index(mutate)), self.assertRaises(ValueError):
                symbols.validate(catalog, PROFILES)

    def test_name_kind_extent_and_shape_fail_closed(self):
        cases = [dict(name="bad name"), dict(name="generic_name"), dict(source_name="a/b"),
                 dict(source="unknown"), dict(kind="code"), dict(rva=-1), dict(rva=True),
                 dict(rva=0x100000000), dict(size=0), dict(size=False), dict(size="4"), dict(size=0x100001),
                 dict(rva=0xffffffff, size=2), dict(zero_fill=1), dict(shape=None),
                 dict(shape=dict(rows=2, columns=1, stride=4, storage="u32"))]
        for changes in cases:
            catalog = copy.deepcopy(CATALOG)
            catalog["builds"]["dvd_20000"]["symbols"][0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                symbols.validate(catalog, PROFILES)

    def test_duplicate_names_rejected_and_address_aliases_allowed(self):
        self.build["symbols"].append(copy.deepcopy(self.symbol))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            symbols.validate(self.catalog, PROFILES)
        self.build["symbols"][-1]["name"] += "_alias"
        symbols.validate(self.catalog, PROFILES)

    def test_invalid_fingerprint_masks_lengths_and_pointers(self):
        valid = dict(bytes="0102030400000000", mask="ffffffff00000000", pointers=[dict(offset=4, rva=0x1000)])
        invalid = [None, {}, dict(valid, bytes="zz"), dict(valid, bytes="00" * 33),
                   dict(valid, bytes="00" * 3, mask="ff" * 3, pointers=[]),
                   dict(valid, mask="ff"), dict(valid, mask="01" * 8),
                   dict(valid, mask="0000000000000000", pointers=[]),
                   dict(valid, pointers=None), dict(valid, pointers=[dict(offset=3, rva=0x1000)]),
                   dict(valid, pointers=[dict(offset=5, rva=0x1000)]),
                   dict(valid, pointers=[dict(offset=4, rva=-1)]),
                   dict(valid, pointers=[dict(offset=4, rva=True)]),
                   dict(valid, pointers=[dict(offset=4, rva=0x100000000)]),
                   dict(valid, pointers=[dict(offset=4, rva=0x1000)] * 2)]
        for fingerprint in invalid:
            catalog = copy.deepcopy(CATALOG)
            row = catalog["builds"]["dvd_20000"]["symbols"][0]
            row["fingerprint"] = fingerprint
            with self.subTest(fingerprint=fingerprint), self.assertRaises(ValueError):
                symbols.validate(catalog, PROFILES)
        self.symbol["fingerprint"] = valid
        symbols.validate(self.catalog, PROFILES)

    def test_zero_fill_only_allows_data_without_fingerprint(self):
        self.symbol.update(fingerprint=None, zero_fill=True)
        symbols.validate(self.catalog, PROFILES)
        self.symbol["kind"] = "function"
        with self.assertRaises(ValueError):
            symbols.validate(self.catalog, PROFILES)
        self.assertFalse(symbols.matches(self.symbol, 0x400000, mock.Mock()))

    def test_reader_errors_short_reads_and_address_overflow(self):
        prefix = image_prefix(self.symbol, 0x710000)
        for result in (None, b"", bytes(prefix[:-1]), bytes(prefix) + b"\0", "bytes"):
            with self.subTest(result=result):
                self.assertFalse(symbols.matches(self.symbol, 0x710000, lambda ea, size: result))
        self.assertFalse(symbols.matches(self.symbol, 0x710000, mock.Mock(side_effect=RuntimeError("unreadable"))))
        for base in (-1, True, 0xffffffffffffffff, 0x10000000000000000):
            self.assertFalse(symbols.matches(self.symbol, base, mock.Mock()))
        row = next(row for row in self.build["symbols"] if row["fingerprint"] and row["fingerprint"]["pointers"])
        prefix = image_prefix(row, 0x710000)
        row["fingerprint"]["pointers"][0]["rva"] = 0xffffffff
        self.assertFalse(symbols.matches(row, 0x710000, reader(row, 0x710000, prefix)))

    def test_wide_effective_addresses_and_pointer_width(self):
        row = next(row for row in self.build["symbols"] if row["kind"] == "function")
        base = 0x100000000
        prefix = image_prefix(row, base)
        self.assertTrue(symbols.matches(row, base, reader(row, base, prefix)))
        pointer = next(row for row in self.build["symbols"] if row["fingerprint"] and row["fingerprint"]["pointers"])
        prefix = image_prefix(pointer, 0x400000)
        self.assertFalse(symbols.matches(pointer, base, reader(pointer, base, prefix)))

    def test_catalog_load_errors_and_unknown_build(self):
        for value in ("{", "null"):
            with mock.patch.object(symbols, "CATALOG", mock.Mock(read_text=mock.Mock(return_value=value))):
                with self.assertRaises(ValueError):
                    symbols.load(PROFILES)
        with mock.patch.object(symbols, "CATALOG", mock.Mock(read_text=mock.Mock(side_effect=OSError("missing")))):
            with self.assertRaises(ValueError):
                symbols.load(PROFILES)
        with self.assertRaises(KeyError):
            symbols.rows(CATALOG, "unknown")


if __name__ == "__main__":
    unittest.main()
