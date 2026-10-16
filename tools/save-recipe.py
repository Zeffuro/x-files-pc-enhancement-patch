#!/usr/bin/env python3
"""Bounded numeric save recipes tied to an exact original game database."""

import argparse
import bisect
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

MAX_DATABASE = 64 * 1024 * 1024
MAX_RECIPE = 4 * 1024 * 1024
MAX_WORDS = 4096
CORE = (0x46, 0x53, 0x50)
AUXILIARY = (0x56, 0x57, 0x58, 0x5A, 0x5B, 0x5C)
ID_ORDER = (0x57, 0x56, 0x58, 0x5A, 0x5B, 0x5C)
SCENE_SCALARS = tuple([(6 + 4 * i, 4) for i in range(15)] +
                      [(106 + i, 1) for i in range(7)] +
                      [(113, 4), (117, 1), (118, 4), (122, 4), (126, 4), (130, 2)])
SCENE_REFERENCES = {22: 0x56, 26: 0x58, 30: 0x5A, 34: 0x5B, 38: 0x5C}
OPAQUE_FIELDS = (66, 82, 90)
FORMAT = "xfiles-numeric-save-recipe"
BUILDS = {
    "cd_10012": ("eae65c27f0c026530e363ebc3483a150c002e831e88ae6e04d3c2f3d4da859ce", "<III"),
    "cd_10019": ("d1cfec26c69c03a41d3a0e81783a11b7892bc6846f6326b306f99f2ed534e1d6", "<IIB"),
    "cd_10020": ("0f8b654cb78f6ae66d62bb173fae79614255ad46cdc25827bbaa98d7d0d79899", "<IIB"),
    "dvd_20000": ("1c7385b15bc11f6ff46b5a30ca383441df96be5be1c1dec31eae650f2243a2ad", "<IIB"),
}


class FormatError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise FormatError(message)


def integer(value, maximum=0xFFFFFFFF):
    require(type(value) is int and 0 <= value <= maximum, "invalid unsigned integer")
    return value


def fields(value, names):
    require(type(value) is dict and set(value) == set(names), "unexpected object fields")


def sequence(value, maximum):
    require(type(value) is list and len(value) <= maximum, "invalid or oversized array")
    return value


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_file(path, limit):
    with Path(path).open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, "input exceeds size limit")
    return data


def identify_executable(path):
    actual = digest(read_file(path, MAX_DATABASE))
    for name, (expected, _) in BUILDS.items():
        if actual == expected:
            return name
    raise FormatError("unsupported executable SHA-256")


def build_format(build):
    require(type(build) is str and build in BUILDS, "unsupported native build")
    return BUILDS[build][1]


class Database:
    def __init__(self, data, baseline=False):
        require(type(data) is bytes and 32 <= len(data) <= MAX_DATABASE, "invalid database size")
        self.data, self.records, self.classes, self.nodes = data, {}, {}, []
        require([self.u32(i) for i in (0, 20, 24, 28)] ==
                [5, 0x501, 0x40000, 256], "unsupported database header")
        root = self.u32(8)
        require(4 <= self.u32(12) <= 11, "unsupported class count")
        require(self.part(root, 1)[0] & 0x80 and self.u32(root + 2) == 1,
                "unsupported class directory")
        count = self.u16(root + 6)
        require(count == self.u32(12), "class directory count mismatch")
        self.part(root, 8 + count * 43)
        self.nodes.append((root, root + 8 + count * 43))
        seen_nodes, object_ids, targets = {root}, set(), set()
        for i in range(count):
            at = root + 8 + 43 * i
            cls = self.u32(at + 11)
            require(self.part(at + 10, 1) == b"\x01" and self.u32(at + 27) == 7 and
                    self.u32(at + 35) == cls, "unsupported class index")
            if i == 0:
                require(cls == 1 and self.u16(at + 8) == count - 1 and
                        self.u32(at + 31) == 0, "invalid master class")
                master_list = self.u32(at + 19)
                continue
            require(cls in CORE + AUXILIARY + (0x59,) and cls not in self.classes,
                    "unknown or duplicate class")
            index, record_count = self.u32(at + 31), self.u32(at + 15)
            require(0 < record_count <= 200000, "invalid record count")
            self.classes[cls] = (record_count, index, self.u32(at + 39))
            previous = -1
            for target, identity in self.tree(index, 8, seen_nodes):
                require(identity > previous and identity not in object_ids and target not in targets,
                        "duplicate or unordered record identity")
                require(target >= 32, "null record")
                self.part(target, 6)
                object_ids.add(identity)
                targets.add(target)
                self.records[cls, identity] = target
                require(len(self.records) <= 200000, "too many database records")
                previous = identity
            require(sum(c == cls for c, _ in self.records) == record_count,
                    "record count mismatch")
        listed = [value[0] for value in self.tree(master_list, 4, seen_nodes)]
        require(len(listed) == count - 1 and set(listed) == set(self.classes),
                "master class list mismatch")
        self.master_order = listed
        self.nodes.sort()
        require(all(a[1] <= b[0] for a, b in zip(self.nodes, self.nodes[1:])),
                "overlapping index nodes")
        self.definitions = []
        for (cls, _), at in self.records.items():
            size = 24 if cls == 0x53 else 12 if cls == 0x46 else 132 if cls == 0x57 else 14
            if cls == 0x59:
                size = 6
            self.part(at, size)
            require(self.part(at, 1)[0] & 0x80 and self.u32(at + 2) == 1,
                    "unsupported record header")
            self.definitions.append((at, at + size))
        occupied = sorted(self.nodes + self.definitions)
        require(all(a[1] <= b[0] for a, b in zip(occupied, occupied[1:])),
                "overlapping record or index")
        self.occupied, self.starts = occupied, [a for a, _ in occupied]
        require(set(CORE) <= set(self.classes), "missing core classes")
        require(len(self.identities(0x53)) <= 4096, "too many variables")
        require(self.u32(16) > max(object_ids), "invalid dynamic identity highwater")
        if baseline:
            require(set(self.classes) == set(CORE), "baseline must be an original database")

    def part(self, at, size):
        require(type(at) is int and type(size) is int and 0 <= at <= len(self.data) and
                0 <= size <= len(self.data) - at, "database read out of bounds")
        return self.data[at:at + size]

    def u16(self, at):
        return int.from_bytes(self.part(at, 2), "big")

    def u32(self, at):
        return int.from_bytes(self.part(at, 4), "big")

    def tree(self, root, width, seen):
        pending, result = [(root, 0)], []
        while pending:
            at, depth = pending.pop()
            require(at >= 32 and at not in seen and depth < 32 and len(seen) < 8192,
                    "cyclic or oversized index tree")
            seen.add(at)
            header = self.part(at, 8)
            require(self.u32(at + 2) == 1, "unsupported index version")
            count, leaf = self.u16(at + 6), bool(header[0] & 0x80)
            require(0 < count <= 32, "invalid index node count")
            extent = 8 + count * (width if leaf else 4)
            self.part(at, extent)
            self.nodes.append((at, at + extent))
            if leaf:
                result.extend(tuple(self.u32(at + 8 + i * width + j)
                                    for j in range(0, width, 4)) for i in range(count))
                require(len(result) <= 200000, "oversized index")
            else:
                pending.extend((self.u32(at + 8 + i * 4), depth + 1)
                               for i in reversed(range(count)))
        return result

    def identities(self, cls):
        return {identity for c, identity in self.records if c == cls}

    def blob(self, at, limit=65536):
        pointer, size = self.u32(at), self.u32(at + 4)
        require(bool(pointer) == bool(size) and size <= limit, "invalid blob descriptor")
        if size:
            index = bisect.bisect_right(self.starts, pointer)
            require(pointer >= 32 and (index == 0 or pointer >= self.occupied[index - 1][1]) and
                    (index == len(self.occupied) or pointer + size <= self.occupied[index][0]),
                    "blob overlaps record or index")
        return self.part(pointer, size)

    def variable(self, identity):
        at = self.records[0x53, identity]
        name = self.blob(at + 6, 1024)
        require(2 <= len(name) <= 1024 and name[-1] == 0 and 0 not in name[:-1] and
                all(32 <= c < 127 for c in name[:-1]), "invalid variable name")
        metadata = self.part(at + 18, 6)
        require(metadata[-1] & 0x7F in (0, 1, 2, 3), "unsupported variable type")
        return name, metadata, self.u32(at + 14)

    def words(self, descriptor):
        blob = self.blob(descriptor, MAX_WORDS * 4)
        require(len(blob) % 4 == 0, "unaligned typed word array")
        return list(struct.unpack("<" + "I" * (len(blob) // 4), blob))


def dynamic_ids(base):
    start = base.u32(16)
    require(start <= 0xFFFFFFFF - len(ID_ORDER), "dynamic identity overflow")
    return {cls: start + i for i, cls in enumerate(ID_ORDER)}


def validate_baseline(base):
    variables = base.identities(0x53)
    for identity in variables:
        _, metadata, value = base.variable(identity)
        if metadata[-1] & 0x7F == 3:
            require((0x50, value) in base.records, "missing baseline string reference")
    for identity in base.identities(0x50):
        base.blob(base.records[0x50, identity] + 6)
    for identity in base.identities(0x46):
        require(base.part(base.records[0x46, identity] + 10, 1)[0] in (0, 1),
                "invalid baseline enabled state")
    return variables


def validate_timers(timers):
    seen = set()
    for timer in sequence(timers, 256):
        fields(timer, ("timer_id", "duration_ms", "periodic"))
        identity = integer(timer["timer_id"], 255)
        integer(timer["duration_ms"])
        require(type(timer["periodic"]) is bool, "invalid timer periodic flag")
        require(identity not in seen, "duplicate timer identity")
        seen.add(identity)


def extract(baseline, saved, build):
    timer_format = build_format(build)
    base, source = Database(baseline, True), Database(saved)
    validate_baseline(base)
    require(set(source.classes) in (set(CORE + AUXILIARY), set(CORE + AUXILIARY + (0x59,))),
            "save must contain the six supported runtime classes")
    numeric, enabled, strings = [], [], []
    for cls in (0x46, 0x53):
        require(base.identities(cls) == source.identities(cls), "save and baseline identities differ")
    for identity in sorted(base.identities(0x53)):
        name, metadata, old = base.variable(identity)
        other_name, other_metadata, new = source.variable(identity)
        require((name, metadata) == (other_name, other_metadata), "variable metadata differs")
        if metadata[-1] & 0x7F == 3:
            require((0x50, old) in base.records and (0x50, new) in source.records,
                    "missing string reference")
            if old != new or base.blob(base.records[0x50, old] + 6) != source.blob(
                    source.records[0x50, new] + 6):
                strings.append(identity)
        elif old != new:
            numeric.append([identity, old, new])
    for identity in sorted(base.identities(0x46)):
        before, after = base.records[0x46, identity], source.records[0x46, identity]
        require(base.part(before + 6, 4) == source.part(after + 6, 4) and
                base.part(before + 11, 1) == source.part(after + 11, 1), "enabled metadata differs")
        old, new = base.part(before + 10, 1)[0], source.part(after + 10, 1)[0]
        require(old in (0, 1) and new in (0, 1), "invalid enabled state")
        if old != new:
            enabled.append([identity, old, new])
    ids, source_ids, auxiliary, omitted = dynamic_ids(base), {}, [], []
    for cls in AUXILIARY:
        identities = source.identities(cls)
        require(len(identities) == 1, "runtime class must have one record")
        source_ids[cls] = next(iter(identities))
    for cls in AUXILIARY:
        at = source.records[cls, source_ids[cls]]
        scalars = []
        if cls == 0x57:
            for offset, width in SCENE_SCALARS:
                value = int.from_bytes(source.part(at + offset, width), "big")
                if offset in SCENE_REFERENCES:
                    target_class = SCENE_REFERENCES[offset]
                    require(value == source_ids[target_class], "invalid runtime reference")
                    value = ids[target_class]
                scalars.append(value)
            omitted = [offset for offset in OPAQUE_FIELDS if source.blob(at + offset)]
            timer_blob = source.blob(at + 98, 256 * struct.calcsize(timer_format))
            require(len(timer_blob) % struct.calcsize(timer_format) == 0, "incomplete timer record")
            timers = []
            for identity, duration, periodic in struct.iter_unpack(timer_format, timer_blob):
                require(periodic in (0, 1), "invalid saved timer periodic flag")
                timers.append(dict(timer_id=identity, duration_ms=duration, periodic=bool(periodic)))
            validate_timers(timers)
        auxiliary.append(dict(class_id=cls, flags=list(source.part(at, 2)), scalars=scalars,
                              words=source.words(at + (74 if cls == 0x57 else 6))))
    result = dict(format=FORMAT, version=1, source_sha256=digest(saved),
                  native_build=build, executable_sha256=BUILDS[build][0], timers=timers,
                  baseline_sha256=digest(baseline), baseline_variable_count=len(base.identities(0x53)),
                  baseline_next_id=base.u32(16), numeric_variables=numeric, enabled_states=enabled,
                  auxiliary=auxiliary, omitted=dict(photo_records=len(source.identities(0x59)),
                                                  string_variables=strings, packed_fields=omitted))
    validate_recipe(result, base)
    return result


def validate_recipe(recipe, base):
    fields(recipe, ("format", "version", "source_sha256", "baseline_sha256",
                    "baseline_variable_count", "baseline_next_id", "numeric_variables",
                    "enabled_states", "auxiliary", "omitted", "native_build",
                    "executable_sha256", "timers"))
    require(recipe["format"] == FORMAT and type(recipe["version"]) is int and
            recipe["version"] == 1, "unsupported recipe format")
    for key in ("source_sha256", "baseline_sha256"):
        require(type(recipe[key]) is str and re.fullmatch("[0-9a-f]{64}", recipe[key]),
                "invalid SHA-256")
    require(recipe["baseline_sha256"] == digest(base.data), "baseline SHA-256 mismatch")
    build_format(recipe["native_build"])
    require(recipe["executable_sha256"] == BUILDS[recipe["native_build"]][0],
            "recipe executable SHA-256 mismatch")
    validate_timers(recipe["timers"])
    variable_ids = validate_baseline(base)
    require(integer(recipe["baseline_variable_count"], 4096) == len(base.identities(0x53)) and
            integer(recipe["baseline_next_id"]) == base.u32(16), "baseline metadata mismatch")
    for key, cls, maximum in (("numeric_variables", 0x53, 0xFFFFFFFF),
                              ("enabled_states", 0x46, 1)):
        previous = -1
        for row in sequence(recipe[key], 4096):
            require(type(row) is list and len(row) == 3, "invalid delta")
            identity, before, after = (integer(row[0]), integer(row[1], maximum),
                                       integer(row[2], maximum))
            require(identity > previous and (cls, identity) in base.records and before != after,
                    "duplicate, unordered or unknown delta")
            at = base.records[cls, identity]
            if cls == 0x53:
                _, metadata, old = base.variable(identity)
                require(metadata[-1] & 0x7F != 3, "string variable delta is forbidden")
            else:
                old = base.part(at + 10, 1)[0]
            require(old == before, "delta expected value mismatch")
            previous = identity
    auxiliary = sequence(recipe["auxiliary"], 6)
    require(len(auxiliary) == 6, "missing runtime records")
    ids = dynamic_ids(base)
    for cls, item in zip(AUXILIARY, auxiliary):
        fields(item, ("class_id", "flags", "scalars", "words"))
        require(integer(item["class_id"]) == cls, "unexpected runtime class order")
        flags = sequence(item["flags"], 2)
        require(len(flags) == 2, "invalid record flags")
        for value in flags:
            integer(value, 255)
        require(flags[0] & 0x80, "invalid runtime record header")
        scalars = sequence(item["scalars"], len(SCENE_SCALARS))
        require(len(scalars) == (len(SCENE_SCALARS) if cls == 0x57 else 0), "invalid scalar layout")
        for (offset, width), value in zip(SCENE_SCALARS, scalars):
            integer(value, (1 << (8 * width)) - 1)
            if offset in SCENE_REFERENCES:
                require(value == ids[SCENE_REFERENCES[offset]], "invalid remapped runtime reference")
        for value in sequence(item["words"], MAX_WORDS):
            integer(value)
    omissions = recipe["omitted"]
    fields(omissions, ("photo_records", "string_variables", "packed_fields"))
    integer(omissions["photo_records"], 1024)
    strings = sequence(omissions["string_variables"], 4096)
    require(all(type(i) is int and i in variable_ids and
                base.variable(i)[1][-1] & 0x7F == 3 for i in strings) and
            strings == sorted(set(strings)), "invalid omitted string identities")
    packed = sequence(omissions["packed_fields"], 4)
    require(all(type(i) is int and i in OPAQUE_FIELDS for i in packed) and
            packed == sorted(set(packed)), "invalid omitted packed fields")


def reconstruct(baseline, recipe, build):
    timer_format = build_format(build)
    base = Database(baseline, True)
    validate_recipe(recipe, base)
    require(recipe["native_build"] == build, "recipe and executable builds differ")
    output = bytearray(baseline)

    def put(at, value, width=4):
        output[at:at + width] = value.to_bytes(width, "big")

    def allocate(size):
        at = (len(output) + 7) & ~7
        require(at + size <= MAX_DATABASE, "output exceeds size limit")
        output.extend(bytes(at + size - len(output)))
        return at

    for identity, _, value in recipe["numeric_variables"]:
        put(base.records[0x53, identity] + 14, value)
    for identity, _, value in recipe["enabled_states"]:
        put(base.records[0x46, identity] + 10, value, 1)
    classes, ids = dict(base.classes), dynamic_ids(base)
    for item in recipe["auxiliary"]:
        cls = item["class_id"]
        at = allocate(132 if cls == 0x57 else 14)
        output[at:at + 2] = bytes(item["flags"])
        put(at + 2, 1)
        for (offset, width), value in zip(SCENE_SCALARS, item["scalars"]):
            put(at + offset, value, width)
        if item["words"]:
            payload = struct.pack("<" + "I" * len(item["words"]), *item["words"])
            blob = allocate(len(payload))
            output[blob:blob + len(payload)] = payload
            descriptor = at + (74 if cls == 0x57 else 6)
            put(descriptor, blob)
            put(descriptor + 4, len(payload))
        if cls == 0x57 and recipe["timers"]:
            payload = b"".join(struct.pack(timer_format, timer["timer_id"], timer["duration_ms"],
                                           int(timer["periodic"])) for timer in recipe["timers"])
            blob = allocate(len(payload))
            output[blob:blob + len(payload)] = payload
            put(at + 98, blob)
            put(at + 102, len(payload))
        index = allocate(16)
        put(index, 0xC0, 1)
        put(index + 2, 1)
        put(index + 6, 1, 2)
        put(index + 8, at)
        put(index + 12, ids[cls])
        classes[cls] = (1, index, 0x49442020)
    ordered = base.master_order + list(AUXILIARY)
    master_list = allocate(8 + 4 * len(ordered))
    put(master_list, 0xC0, 1)
    put(master_list + 2, 1)
    put(master_list + 6, len(ordered), 2)
    for i, cls in enumerate(ordered):
        put(master_list + 8 + 4 * i, cls)
    extent = 8 + 43 * (len(classes) + 1)
    # The native compatibility reader prefetches seven bytes beyond the final row.
    directory = allocate((extent + 7 + 7) & ~7)
    put(directory, 0xC0, 1)
    put(directory + 2, 1)
    put(directory + 6, len(classes) + 1, 2)
    master = directory + 8
    put(master + 8, len(classes), 2)
    put(master + 10, 1, 1)
    put(master + 11, 1)
    put(master + 19, master_list)
    put(master + 27, 7)
    put(master + 35, 1)
    put(master + 39, 0x6E756C6C)
    for i, (cls, (count, index, tag)) in enumerate(sorted(classes.items()), 1):
        at = directory + 8 + 43 * i
        for offset, value in ((11, cls), (15, count), (27, 7), (31, index), (35, cls), (39, tag)):
            put(at + offset, value)
        put(at + 10, 1, 1)
    put(8, directory)
    put(12, len(classes) + 1)
    put(16, base.u32(16) + len(ids))
    result = bytes(output)
    Database(result)
    return result


def decode_recipe(data):
    require(type(data) is bytes and len(data) <= MAX_RECIPE, "invalid or oversized recipe input")
    depth, quoted, escaped = 0, False, False
    for byte in data:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            require(depth <= 16, "recipe nesting exceeds limit")
        elif byte in (93, 125):
            depth -= 1

    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key")
            result[key] = value
        return result

    def invalid(_):
        raise FormatError("non-integer JSON number")

    try:
        return json.loads(data, object_pairs_hook=pairs, parse_float=invalid, parse_constant=invalid)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise FormatError("invalid recipe JSON: " + str(error)) from error


def write_new(path, data, inputs):
    target = Path(path).resolve()
    for source in inputs:
        require(Path(source).resolve() != target, "output must not replace an input file")
    with target.open("xb") as stream:
        stream.write(data)
    return dict(path=str(target), bytes=len(data), sha256=digest(data))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, argument in (("extract", "save"), ("reconstruct", "recipe")):
        command = commands.add_parser(name)
        command.add_argument(argument)
        command.add_argument("--baseline", required=True, help="matching original XFILES.GAM")
        command.add_argument("--executable", required=True, help="verified original XFiles.exe")
        command.add_argument("--output", required=True, help="new file, including inside a game folder")
        if name == "extract":
            command.add_argument("--save-sha256", help="optional expected source save SHA-256")
    args = parser.parse_args(argv)
    try:
        baseline = read_file(args.baseline, MAX_DATABASE)
        build = identify_executable(args.executable)
        if args.command == "extract":
            source = read_file(args.save, MAX_DATABASE)
            if args.save_sha256:
                require(digest(source) == args.save_sha256, "source SHA-256 mismatch")
            value = extract(baseline, source, build)
            output = (json.dumps(value, indent=2) + "\n").encode("ascii")
            inputs = [args.baseline, args.save, args.executable]
        else:
            value = decode_recipe(read_file(args.recipe, MAX_RECIPE))
            output = reconstruct(baseline, value, build)
            inputs = [args.baseline, args.recipe, args.executable]
        print(json.dumps(write_new(args.output, output, inputs)))
        return 0
    except (OSError, FormatError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
