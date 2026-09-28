"""Validate the vibration catalogs and compile the playback lookup tables."""

import argparse
import csv
import re
from pathlib import Path


def rows(directory, name, header):
    with (directory / name).open(encoding="utf-8", newline="") as source:
        table = csv.DictReader(source, delimiter="\t")
        if table.fieldnames != header.split():
            raise ValueError(f"{name}: unexpected header")
        result = list(table)
    if not result or len(result) > 256 or any(None in r or None in r.values() for r in result):
        raise ValueError(f"{name}: invalid rows")
    return result


def number(value, maximum=65535):
    if not re.fullmatch(r"[0-9]+", value) or int(value) > maximum:
        raise ValueError(f"Invalid integer: {value}")
    return int(value)


def compile_catalog(directory):
    effects = rows(directory, "rumble-effects.tsv",
                   "effect low_motor high_motor duration_ms ps1_large ps1_small ps1_ticks timing_basis overlap")
    clips = rows(directory, "rumble-clips.tsv",
                 "path effect start_ms selector_ids action_cases source")
    events = rows(directory, "rumble-events.tsv",
                  "selector_id action_case path effect active_type special_state origin clip_fallback")
    indexed = {}
    for row in effects:
        name = row["effect"]
        if name in indexed or name not in {"ps1_short", "ps1_long"}:
            raise ValueError("Duplicate or unknown effect")
        low, high, duration = (number(row[key]) for key in ("low_motor", "high_motor", "duration_ms"))
        large, small, ticks = (number(row[key], limit) for key, limit in
                               (("ps1_large", 255), ("ps1_small", 1), ("ps1_ticks", 60)))
        if (low != large * 257 or high != small * 65535 or not 0 < duration <= 2000
                or duration != round(ticks * 1000 / 60)
                or row["timing_basis"] != "assumed_60_updates_per_second"
                or row["overlap"] != "replace_strength_extend_end"):
            raise ValueError("Invalid effect contract")
        indexed[name] = (low, high, duration)
    if set(indexed) != {"ps1_short", "ps1_long"}:
        raise ValueError("Missing effect")
    paths = {}
    for row in clips:
        path = row["path"]
        if (not re.fullmatch(r"xv/[0-9]{5}\.xmv", path) or path in paths
                or row["effect"] not in indexed or row["start_ms"] != "0"
                or row["source"] != "ps1_pal"):
            raise ValueError("Invalid clip cue")
        for key in ("selector_ids", "action_cases"):
            values = row[key].split(",")
            if len(values) != len(set(values)):
                raise ValueError("Duplicate clip cross-reference")
            for value in values:
                number(value)
        paths[path] = row
    seen = set()
    for row in events:
        number(row["selector_id"])
        number(row["action_case"])
        key = tuple(row.values())
        if key in seen:
            raise ValueError("Duplicate event")
        seen.add(key)
        if (row["active_type"] not in {"any", "7", "not_7"}
                or row["special_state"] not in {"any", "23", "not_23"}
                or row["origin"] not in {"direct", "gunfire_helper", "none"}
                or row["clip_fallback"] not in {"0", "1"}
                or row["effect"] not in {*indexed, "none"}
                or (row["path"] != "-" and not re.fullmatch(r"xv/[0-9]{5}\.xmv", row["path"]))):
            raise ValueError("Invalid event contract")
        if (row["effect"] == "none") != (row["origin"] == "none"):
            raise ValueError("Inconsistent negative event")
        if row["origin"] == "gunfire_helper" and row["effect"] != "ps1_short":
            raise ValueError("Unsupported helper effect")
        if (row["origin"] == "direct") != (row["clip_fallback"] == "1"):
            raise ValueError("Missing direct fallback")
        if row["origin"] != "direct" and row["path"] in paths:
            raise ValueError("Non-direct event collides with a clip cue")
        if row["clip_fallback"] == "1":
            clip = paths.get(row["path"])
            if (not clip or row["effect"] != clip["effect"]
                    or row["selector_id"] not in clip["selector_ids"].split(",")
                    or row["action_case"] not in clip["action_cases"].split(",")):
                raise ValueError("Event/clip mismatch")
    for path, clip in paths.items():
        matches = [r for r in events if r["path"] == path and r["clip_fallback"] == "1"]
        for field, singular in (("selector_ids", "selector_id"), ("action_cases", "action_case")):
            if set(clip[field].split(",")) != {r[singular] for r in matches}:
                raise ValueError("Unmapped clip reference")
    lines = ["// Generated from data/rumble-*.tsv.", "#pragma once", "",
             '#include "enhancements/rumble_catalog.h"', "",
             "namespace enhancements::rumble::catalog {", ""]
    for name, (low, high, duration) in sorted(indexed.items()):
        lines.append("inline constexpr Effect %s{{%d, %d}, %d};" % (name, low, high, duration))
    lines += ["", "inline constexpr Clip clips[] = {"]
    for path, clip in sorted(paths.items()):
        lines.append(f'    {{L"{path}", {clip["effect"]}}},')
    lines += ["};", "", "}", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    content = compile_catalog(args.data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != content:
        with args.output.open("w", encoding="utf-8", newline="\n") as output:
            output.write(content)


if __name__ == "__main__":
    main()
