"""Validate fixed movie grades and emit the embedded catalog."""
import csv
from pathlib import Path
import re
import sys

FIELDS = ('path', 'contrast', 'brightness', 'gamma', 'black', 'white', 'red', 'green', 'blue')
LIMITS = ((-25, 25), (-20, 20), (50, 200), (0, 127), (128, 255), (75, 125), (75, 125), (75, 125))


def read(path):
    with Path(path).open(encoding='utf-8', newline='') as stream:
        reader = csv.DictReader(stream, delimiter='\t')
        if tuple(reader.fieldnames or ()) != FIELDS:
            raise ValueError('Unexpected movie grading TSV columns')
        rows, seen = [], set()
        for number, row in enumerate(reader, 2):
            name = row['path']
            if not re.fullmatch(r'xv/[0-9]{5}\.xmv', name):
                raise ValueError(f'Invalid movie identity on line {number}')
            if name in seen:
                raise ValueError(f'Duplicate movie identity on line {number}')
            seen.add(name)
            values = tuple(int(row[field]) for field in FIELDS[1:])
            if any(not low <= value <= high for value, (low, high) in zip(values, LIMITS)) or values[4] - values[3] < 128:
                raise ValueError(f'Unsafe grade range on line {number}')
            if None in row:
                raise ValueError(f'Extra columns on line {number}')
            rows.append((name, values))
        return sorted(rows)


def generate(source, destination):
    rows = read(source)
    text = ['#pragma once', '#include "playback/grading.h"', '#include <array>',
            'namespace playback {',
            'struct GradeRow { std::string_view path; Grade grade; };',
            f'inline constexpr std::array<GradeRow, {len(rows)}> grading_catalog{{{{']
    for name, values in rows:
        text.append('    {"' + name + '", {' + ', '.join(map(str, values)) + '}},')
    text += ['}};', '}']
    output = Path(destination)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(text) + '\n', encoding='utf-8')


if __name__ == '__main__':
    generate(*sys.argv[1:])
