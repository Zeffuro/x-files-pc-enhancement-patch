"""Report QuickTime handler coverage and observed calls."""

import argparse
import collections
import json
from pathlib import Path
import re
import struct


ROOT = Path(__file__).resolve().parents[1]


def source_coverage():
    selectors = {
        int(value, 16): name
        for name, value in re.findall(
            r"XFILES_SELECTOR\((\w+),\s*(0x[0-9a-f]+)\)",
            (ROOT / "src/selectors.inc").read_text(), re.I)
    }
    handlers = {}
    for path in (ROOT / "src").rglob("*.cpp"):
        source = path.read_text()
        for match in re.finditer(r"bind_entry\(Selector::(\w+),\s*([^\n]+?)\)", source):
            handlers[match[1]] = {
                "function": match[2], "file": path.relative_to(ROOT).as_posix(),
                "line": source[:match.start()].count("\n") + 1,
                "coverage": "handler present; argument restrictions may remain",
            }
    source = (ROOT / "src/quickdraw/procedures.cpp").read_text()
    types = (ROOT / "src/quickdraw/types.h").read_text().split("struct Procedures {")[1].split("};")[0]
    fields = re.findall(r"\w*Procedure\s+(\w+);", types)
    callbacks = []
    for index, field in enumerate(fields):
        match = re.search(r"output->" + field + r"\s*=\s*([^;]+);", source)
        function = match[1] if match else "unassigned"
        callbacks.append({"field": field, "offset": hex(index * 4), "function": function,
                          "coverage": "missing" if function.startswith("missing_procedure")
                          else "handler present; argument restrictions may remain"})
    return selectors, handlers, callbacks


def game_thunks(path, selectors):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if data[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", data, pe + 4)[0] != 0x14c:
        raise ValueError("Expected an x86 PE executable")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 52)[0]
    result = collections.defaultdict(list)
    for index in range(count):
        section = pe + 24 + optional_size + index * 40
        rva, size, offset = struct.unpack_from("<III", data, section + 12)
        flags = struct.unpack_from("<I", data, section + 36)[0]
        if not flags & 0x20000000:
            continue
        code = data[offset:offset + size]
        for match in re.finditer(rb"\xb8(.{4})\xff\x25(.{4})", code, re.S):
            selector = int.from_bytes(match[1], "little")
            start = match.start() - 15
            if selector not in selectors or start < 0:
                continue
            prefix = code[start:match.start()]
            if prefix[0] == 0xa1 and prefix[5:9] == b"\x0b\xc0\x74\x06" and prefix[9:11] == b"\xff\x15":
                result[selector].append(hex(base + rva + start))
    return result


def log_evidence(paths):
    calls = collections.defaultdict(set)
    failures = collections.Counter()
    for path in paths:
        for line in path.read_text(errors="replace").splitlines():
            match = re.match(r"(CALL|UNSUPPORTED) selector=(0x\w+) name=(.*?) caller=(0x\w+)", line)
            if not match:
                continue
            kind, selector, name, caller = match.groups()
            if kind == "CALL":
                calls[int(selector, 16)].add(caller)
            else:
                failures[name] += 1
    return calls, failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path)
    parser.add_argument("--log", type=Path, action="append", default=[])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    selectors, handlers, callbacks = source_coverage()
    thunks = game_thunks(args.game, selectors) if args.game else {}
    calls, failures = log_evidence(args.log)
    report = {"callbacks": callbacks, "selectors": [], "runtime_failures": dict(failures)}
    for value, name in selectors.items():
        entry = {"name": name, "selector": hex(value), "handler": handlers.get(name),
                 "thunks": thunks.get(value, []), "observed_callers": sorted(calls.get(value, []))}
        report["selectors"].append(entry)
    for callback in callbacks:
        print(f"{callback['offset']:>4} {callback['field']:<20} {callback['coverage']}")
    missing = [row for row in report["selectors"] if not row["handler"] and row["thunks"]]
    print(f"\n{len(missing)} selectors have game thunks but no handler:")
    for row in missing:
        print(f"  {row['name']:<32} {len(row['observed_callers'])} observed callers")
    print("\nA linked thunk is not proof of execution. Callback offsets need separate verification.")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
