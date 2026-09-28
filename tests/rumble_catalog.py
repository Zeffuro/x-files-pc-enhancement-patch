"""Reject broken catalog edits before they can become compiled motor output."""

import csv
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("rumble_catalog", ROOT / "tools/rumble-catalog.py")
CATALOG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CATALOG)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.data = Path(self.temp.name)
        for source in (ROOT / "data").glob("rumble-*.tsv"):
            shutil.copyfile(source, self.data / source.name)

    def change(self, name, field, value):
        path = self.data / ("rumble-" + name + ".tsv")
        with path.open(encoding="utf-8", newline="") as source:
            reader = csv.DictReader(source, delimiter="\t")
            header, rows = reader.fieldnames, list(reader)
        rows[0][field] = value
        with path.open("w", encoding="utf-8", newline="") as output:
            writer = csv.DictWriter(output, fieldnames=header, delimiter="\t", lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)

    def test_all_edition_paths(self):
        CATALOG.compile_catalog(self.data)
        with (self.data / "rumble-clips.tsv").open(encoding="utf-8") as source:
            paths = {row["path"] for row in csv.DictReader(source, delimiter="\t")}
        self.assertEqual(len(paths), 17)
        catalogs = list((ROOT / "data").glob("media-*.tsv"))
        self.assertGreaterEqual(len(catalogs), 7)
        for catalog in catalogs:
            available = {line.split("\t")[0].lower()
                         for line in catalog.read_text(encoding="utf-8-sig").splitlines()}
            self.assertFalse(paths - available, catalog.name)

    def test_invalid_rows(self):
        cases = [("effects", "low_motor", "65536"), ("effects", "duration_ms", "60000"),
                 ("effects", "duration_ms", "0"), ("effects", "ps1_ticks", "6"),
                 ("effects", "overlap", "add"), ("effects", "timing_basis", "measured"),
                 ("clips", "path", "../xv/21230.xmv"), ("clips", "start_ms", "1"),
                 ("clips", "selector_ids", "999"), ("clips", "effect", "missing"),
                 ("events", "active_type", "8"), ("events", "effect", "none"),
                 ("events", "origin", "direct"), ("events", "clip_fallback", "1")]
        cases.append(("events", "path", "xv/21782.xmv"))
        for name, field, value in cases:
            with self.subTest(name=name, field=field, value=value):
                source = ROOT / "data" / ("rumble-" + name + ".tsv")
                self.change(name, field, value)
                with self.assertRaises(ValueError):
                    CATALOG.compile_catalog(self.data)
                shutil.copyfile(source, self.data / source.name)

    def test_duplicate_and_malformed(self):
        for name in ("effects", "clips", "events"):
            path = self.data / ("rumble-" + name + ".tsv")
            original = path.read_text(encoding="utf-8")
            for content in (original + original.splitlines()[1] + "\n",
                            original.replace("\t", ",", 1), original + "broken\n"):
                path.write_text(content, encoding="utf-8")
                with self.assertRaises(ValueError):
                    CATALOG.compile_catalog(self.data)
            path.write_text(original, encoding="utf-8")


if __name__ == "__main__":
    unittest.main()
