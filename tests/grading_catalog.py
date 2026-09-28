import csv
import importlib.util
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('grading', ROOT / 'tools/grading-catalog.py')
grading = importlib.util.module_from_spec(spec)
spec.loader.exec_module(grading)
rows = grading.read(ROOT / 'data/movie-grades.tsv')
assert rows

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'grades.tsv'
    output = Path(directory) / 'catalog.h'
    base = ['xv/12345.xmv', 10, 0, 100, 0, 255, 100, 100, 100]

    def write(values):
        with source.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.writer(stream, delimiter='\t')
            writer.writerow(grading.FIELDS)
            writer.writerows(values)

    write([base])
    grading.generate(source, output)
    assert 'xv/12345.xmv' in output.read_text()
    cases = []
    for column, value in [(0, '../xv/12345.xmv'), (1, 26), (2, -21),
                           (3, 0), (4, 128), (5, 256), (6, 126), (7, 0), (8, 200)]:
        invalid = base.copy()
        invalid[column] = value
        cases.append([invalid])
    cases.append([base, base])
    for values in cases:
        write(values)
        try:
            grading.read(source)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Invalid catalog accepted: {values}')
print(f'Validated {len(rows)} fixed movie grades and malformed catalog rejection.')
