import ast
from collections import Counter
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def rows(path, marker):
    body = path.read_text(encoding="utf-8").split(marker, 1)[1].split("\n};", 1)[0]
    result, row, value = [], None, []
    for token in re.findall(r'Message::[a-z0-9_]+|L"(?:\\.|[^"\\])*"|[{},]', body):
        if token == "{":
            row, value = [], []
        elif row is not None and token in {",", "}"}:
            row.append("".join(value))
            value = []
            if token == "}":
                result.append(row)
                row = None
        elif token.startswith('L"'):
            value.append(ast.literal_eval(token[1:]))
        elif token.startswith("Message::"):
            value.append(token.removeprefix("Message::"))
    return result


class TranslationCatalogs(unittest.TestCase):
    def test_complete_translation(self):
        languages = ("german", "french", "spanish", "italian", "japanese", "russian")
        catalogs = [rows(ROOT / f"src/localization/{name}.h", f"{name}_entries[] = {{")
                    for name in languages]
        english_rows = rows(ROOT / "src/localization/english.h", "english_entries[] = {")
        english = dict(english_rows)
        self.assertTrue(english)
        keys = Counter(row[0] for row in english_rows)
        enum = (ROOT / "src/localization/translation.h").read_text().split("enum class Message {", 1)[1].split("};", 1)[0]
        ids = re.findall(r"^\s+([a-z][a-z0-9_]*)\s*,?\s*$", enum, re.MULTILINE)
        self.assertEqual(keys, Counter(key for key in ids if key != "count"))
        self.assertEqual(len(set(english.values())), len(english_rows))
        placeholders = re.compile(r"%(?:\d+\$)?[-+#0 ]*\d*(?:\.\d+)?[hljztL]*[diuoxXfFeEgGaAcspn]")
        for language, catalog in zip(languages, catalogs):
            with self.subTest(language=language):
                self.assertTrue(all(len(row) == 2 for row in catalog))
                self.assertEqual(keys, Counter(row[0] for row in catalog))
                self.assertEqual(len({row[0] for row in catalog}), len(catalog))
                for key, translated in catalog:
                    source = english[key]
                    self.assertTrue(translated.strip(), key)
                    self.assertEqual(placeholders.findall(source), placeholders.findall(translated), source)
                    self.assertEqual(source.count("\n"), translated.count("\n"), source)
                    self.assertEqual(source.count("&"), translated.count("&"), source)


if __name__ == "__main__":
    unittest.main()
