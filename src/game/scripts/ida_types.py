"""Internal canonical-type planner for the guarded IDA import entry point."""

import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[3]
NAME = re.compile(r"XFiles_[A-Za-z_][A-Za-z0-9_]*\Z")
SCALARS = {"void", "unsigned int", "int", "unsigned short", "short", "unsigned char",
           "signed char", "char", "bool", "float", "double", "wchar_t", "unsigned long",
           "long", "unsigned long long", "long long"}
CONVENTIONS = {"__cdecl", "__stdcall", "__thiscall"}


def digest(value):
    return value.hex() if isinstance(value, bytes) and len(value) == 32 else value


def fresh(manifest, root):
    sources = manifest.get("sources")
    if not isinstance(sources, dict) or not sources:
        raise ValueError("Canonical type manifest has no source identities")
    root = Path(root).resolve()
    exporter_path = root / "src/game/ida/export_types.py"
    if (root / "src/game/layouts").is_dir():
        spec = importlib.util.spec_from_file_location("xfiles_type_source_guard", exporter_path)
        exporter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(exporter)
        exporter.check_fresh(manifest)
    for relative, expected in sources.items():
        path = (root / relative).resolve()
        if (not path.is_relative_to(root) or Path(relative).is_absolute()
                or not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected)):
            raise ValueError("Invalid canonical type source identity")
        try:
            actual = hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        except OSError as error:
            raise ValueError("Canonical type source is unavailable: " + relative) from error
        if actual != expected:
            raise ValueError("Canonical type manifest is stale: " + relative)


def dependencies(node):
    kind = node.get("kind")
    if kind == "named":
        return {node["name"]}
    if kind == "pointer":
        return dependencies(node["to"])
    if kind == "array":
        return dependencies(node["element"])
    if kind == "function":
        return dependencies(node["return"]) | set().union(*(dependencies(p) for p in node["params"]))
    return set()


def record_dependencies(record):
    if record["kind"] == "struct":
        return set().union(*(dependencies(field["type"]) for field in record["fields"]))
    if record["kind"] == "alias":
        return dependencies(record["target"])
    return set()


def _node(node, names, depth=0):
    if not isinstance(node, dict) or depth > 32 or node.get("const", False) not in (True, False):
        raise ValueError("Invalid canonical type node")
    kind = node.get("kind")
    if kind == "scalar":
        if node.get("name") not in SCALARS:
            raise ValueError("Unsupported canonical scalar")
    elif kind == "named":
        if node.get("name") not in names:
            raise ValueError("Unknown canonical named dependency")
    elif kind == "pointer":
        _node(node.get("to"), names, depth + 1)
    elif kind == "array":
        if type(node.get("count")) is not int or not 0 < node["count"] <= 0x100000:
            raise ValueError("Invalid canonical array extent")
        _node(node.get("element"), names, depth + 1)
    elif kind == "function":
        if node.get("convention") not in CONVENTIONS or not isinstance(node.get("params"), list):
            raise ValueError("Unsupported canonical function convention")
        _node(node.get("return"), names, depth + 1)
        for parameter in node["params"]:
            _node(parameter, names, depth + 1)
    else:
        raise ValueError("Unsupported canonical type node")


def validate(manifest, root):
    if (manifest.get("schema") != 1 or manifest.get("target") != {
            "triple": "i686-pc-windows-msvc", "pointer_size": 4}
            or not isinstance(manifest.get("types"), list)):
        raise ValueError("Unsupported canonical type manifest")
    fresh(manifest, root)
    names = {record.get("name") for record in manifest["types"]}
    if len(names) != len(manifest["types"]) or any(not isinstance(n, str) or not NAME.fullmatch(n) for n in names):
        raise ValueError("Invalid or duplicate canonical type names")
    for record in manifest["types"]:
        kind = record.get("kind")
        if kind == "struct":
            end = 0
            for field in record["fields"]:
                _node(field["type"], names)
                if (type(field["offset"]) is not int or type(field["size"]) is not int
                        or field["offset"] < end or field["size"] < 1):
                    raise ValueError("Overlapping or invalid canonical fields")
                end = field["offset"] + field["size"]
            if end > record["size"]:
                raise ValueError("Canonical fields exceed their record")
        elif kind == "enum":
            _node(record["underlying"], names)
            if record["underlying"]["kind"] != "scalar":
                raise ValueError("Invalid canonical enum storage")
        elif kind == "alias":
            _node(record["target"], names)
        elif kind != "opaque":
            raise ValueError("Unsupported canonical record kind")
        if kind in ("struct", "enum") and (
                type(record["size"]) is not int or record["size"] < 1
                or record["align"] not in (1, 2, 4, 8, 16)):
            raise ValueError("Invalid canonical record extent")


def project(manifest, build_id):
    result = copy.deepcopy(manifest)
    records = {record["name"]: record for record in result["types"]}
    scalar_sizes = {"void": 0, "unsigned char": 1, "signed char": 1, "char": 1, "bool": 1,
                    "unsigned short": 2, "short": 2, "wchar_t": 2, "double": 8,
                    "unsigned long long": 8, "long long": 8}

    def size(node):
        kind = node["kind"]
        if kind == "scalar":
            return scalar_sizes.get(node["name"], 4)
        if kind == "pointer":
            return 4
        if kind == "array":
            return node["count"] * size(node["element"])
        record = records[node["name"]]
        return size(record["target"]) if record["kind"] == "alias" else record["size"]

    def gap(field):
        node = field["type"]
        return (node["kind"] == "array" and node["element"] == {"kind": "scalar", "name": "unsigned char"}
                and node["count"] == field["size"])

    touched = set()
    for overlay in result.get("profile_fields", []):
        record = records[overlay["record"]]
        start = overlay["offsets"][build_id]
        extent = size(overlay["type"])
        end = start + extent
        fields = []
        for field in record["fields"]:
            if field["name"] == overlay["field"]:
                continue
            low, high = field["offset"], field["offset"] + field["size"]
            if low < end and start < high:
                if not gap(field):
                    raise ValueError("Profile field overlaps a fixed canonical field")
                for index, (left, right) in enumerate(((low, min(start, high)), (max(end, low), high))):
                    if left < right:
                        fields.append({"name": field["name"] + "_" + str(index), "offset": left,
                                       "size": right - left, "type": {"kind": "array", "count": right - left,
                                       "element": {"kind": "scalar", "name": "unsigned char"}}})
            else:
                fields.append(field)
        fields.append({"name": overlay["field"], "offset": start, "size": extent, "type": overlay["type"]})
        record["fields"] = sorted(fields, key=lambda field: field["offset"])
        touched.add(record["name"])
    for name in touched:
        record = records[name]
        end = max(field["offset"] + field["size"] for field in record["fields"])
        record["size"] = (end + record["align"] - 1) // record["align"] * record["align"]
    return result


class IdaAdapter:
    def __init__(self):
        import ida_auto
        import ida_ida
        import ida_nalt
        import ida_typeinf
        self.auto, self.ida, self.nalt, self.t = ida_auto, ida_ida, ida_nalt, ida_typeinf
        self.types = self.t

    def wait(self):
        self.auto.auto_wait()

    def is_32bit(self):
        return self.ida.inf_is_32bit_exactly()

    def input_sha256(self):
        return self.nalt.retrieve_input_file_sha256()

    def open(self):
        return self.t.new_til("xfiles_preview", "Canonical X-Files type preview")

    def close(self, til):
        self.t.free_til(til)

    def named(self, til, name):
        value = self.t.tinfo_t()
        return value if value.get_named_type(til, name) else None

    def current(self, name):
        return self.named(self.t.get_idati(), name)

    def identical(self, left, right):
        try:
            return self.signature(left) == self.signature(right)
        except ValueError:
            return False

    def signature(self, value, seen=None, depth=0, shallow=False):
        if depth > 96:
            raise ValueError("Type graph exceeds the comparison bound")
        seen = set() if seen is None else seen
        name = value.get_type_name() or None
        qualifiers = (value.is_const(), value.is_volatile())
        if shallow and depth and name:
            return ("reference", name, qualifiers)
        if name:
            if name in seen:
                return ("reference", name, qualifiers)
            seen.add(name)
        common = (name, qualifiers, value.get_size(), value.get_alignment())

        def child(item):
            return self.signature(item, seen, depth + 1, shallow)

        if value.is_forward_decl():
            return ("opaque", common, value.get_forward_type())
        if value.is_udt():
            detail = self.t.udt_type_data_t()
            if not value.get_udt_details(detail):
                raise ValueError("Cannot compare record details")
            return ("record", common, detail.is_union, tuple(
                (member.name, member.offset, member.size, child(member.type)) for member in detail))
        if value.is_enum():
            detail = self.t.enum_type_data_t()
            if not value.get_enum_details(detail):
                raise ValueError("Cannot compare enum details")
            storage = detail.bte & (self.t.BTE_SIZE_MASK | self.t.BTE_BITMASK)
            semantics = detail.taenum_bits & (self.t.TAENUM_64BIT | self.t.TAENUM_SIGNED | self.t.TAENUM_UNSIGNED)
            return ("enum", common, storage, semantics,
                    tuple((member.name, member.value) for member in detail))
        if value.is_ptr():
            detail = self.t.ptr_type_data_t()
            if (not value.get_ptr_details(detail) or detail.is_shifted()
                    or not detail.closure.empty() or detail.based_ptr_size):
                raise ValueError("Unsupported pointer comparison")
            return ("pointer", common, detail.taptr_bits, child(detail.obj_type))
        if value.is_array():
            detail = self.t.array_type_data_t()
            if not value.get_array_details(detail):
                raise ValueError("Cannot compare array details")
            return ("array", common, detail.base, detail.nelems, child(detail.elem_type))
        if value.is_func():
            detail = self.t.func_type_data_t()
            if not value.get_func_details(detail):
                raise ValueError("Cannot compare function details")
            return ("function", common, detail.cc, child(detail.rettype),
                    tuple((arg.flags, child(arg.type)) for arg in detail))
        if value.is_complex() or value.is_bitfield():
            raise ValueError("Unsupported complex type comparison")
        return ("scalar", common, value.get_realtype())

    def snapshot(self, value):
        return self.signature(value, shallow=True)

    def getexisting_tinfo(self, ea):
        value = self.t.tinfo_t()
        return value if self.nalt.get_tinfo(value, ea) else None

    def is_userti(self, ea):
        return self.nalt.is_userti(ea)

    def apply_tinfo(self, ea, value):
        return self.t.apply_tinfo(ea, value, self.t.TINFO_DEFINITE)

    def forward(self, til, name):
        value = self.t.tinfo_t()
        if value.create_forward_decl(til, self.t.BTF_STRUCT, name) != self.t.TERR_OK:
            raise ValueError("Cannot declare canonical forward type: " + name)

    def node(self, til, node):
        value = self.t.tinfo_t()
        kind = node["kind"]
        if kind == "scalar":
            if node["name"] == "void":
                value, valid = self.t.tinfo_t(self.t.BTF_VOID), True
            else:
                valid = self.t.parse_decl(value, til, node["name"] + " value;", self.t.PT_SIL)
        elif kind == "named":
            valid = value.get_named_type(til, node["name"])
        elif kind == "pointer":
            valid = value.create_ptr(self.node(til, node["to"]))
        elif kind == "array":
            valid = value.create_array(self.node(til, node["element"]), node["count"])
        else:
            detail = self.t.func_type_data_t()
            detail.rettype = self.node(til, node["return"])
            detail.cc = getattr(self.t, {"__cdecl": "CM_CC_CDECL", "__stdcall": "CM_CC_STDCALL",
                                       "__thiscall": "CM_CC_THISCALL"}[node["convention"]])
            for index, parameter in enumerate(node["params"]):
                arg = self.t.funcarg_t()
                arg.name, arg.type = "arg" + str(index), self.node(til, parameter)
                detail.push_back(arg)
            valid = value.create_func(detail)
        if not valid:
            raise ValueError("Cannot construct canonical type node: " + str(node))
        if node.get("const"):
            value.set_const()
        return value

    def enum_details(self, record):
        detail = self.t.enum_type_data_t()
        detail.bte = self.t.BTE_ALWAYS | record["size"].bit_length()
        unsigned = record["underlying"]["name"].startswith("unsigned")
        detail.taenum_bits = self.t.TAENUM_UNSIGNED if unsigned else self.t.TAENUM_SIGNED
        for member in record["values"]:
            detail.push_back(self.t.edm_t(record["name"] + "_" + member["name"],
                                         member["value"] & ((1 << (record["size"] * 8)) - 1)))
        return detail

    def define(self, til, record, replace=True):
        kind = record["kind"]
        if kind == "opaque":
            return self.named(til, record["name"])
        value = self.t.tinfo_t()
        if kind == "alias":
            value = self.node(til, record["target"])
        elif kind == "enum":
            if not value.create_enum(self.enum_details(record)):
                raise ValueError("Cannot construct canonical enum")
        else:
            detail = self.t.udt_type_data_t()
            detail.total_size = record["size"]
            detail.unpadded_size = record["size"]
            detail.effalign = record["align"]
            detail.sda = record["align"].bit_length()
            detail.set_fixed()
            for field in record["fields"]:
                member = self.t.udm_t()
                member.name, member.offset, member.size = field["name"], field["offset"] * 8, field["size"] * 8
                member.type = self.node(til, field["type"])
                if member.type.get_size() != field["size"]:
                    raise ValueError("Canonical member size differs: " + record["name"] + "." + field["name"])
                detail.push_back(member)
            if not value.create_udt(detail, self.t.BTF_STRUCT):
                raise ValueError("Cannot construct canonical record")
        self.verify(value, record)
        flags = self.t.NTF_REPLACE if replace else 0
        if value.set_named_type(til, record["name"], flags) != self.t.TERR_OK:
            raise ValueError("Cannot retain canonical preview type: " + record["name"])
        stored = self.named(til, record["name"])
        if kind == "enum" and stored.set_enum_is_bitmask(self.t.tinfo_t.ENUMBM_OFF) != self.t.TERR_OK:
            raise ValueError("Cannot preserve canonical ordinary enum semantics")
        self.verify(stored, record)
        return stored

    def verify(self, value, record):
        kind = record["kind"]
        if "size" in record and value.get_size() != record["size"]:
            raise ValueError(f"Canonical record size differs: {record['name']} "
                             f"(SDK {value.get_size()}, expected {record['size']})")
        if "align" in record and value.get_alignment() != record["align"]:
            raise ValueError(f"Canonical record alignment differs: {record['name']} "
                             f"(SDK {value.get_alignment()}, expected {record['align']})")
        if kind == "struct":
            actual = self.t.udt_type_data_t()
            if not value.get_udt_details(actual) or len(actual) != len(record["fields"]):
                raise ValueError("Canonical record members differ")
            if any(member.name != field["name"] or member.offset != field["offset"] * 8
                   or member.size != field["size"] * 8
                   for member, field in zip(actual, record["fields"])):
                raise ValueError("Canonical record offsets differ")
        elif kind == "enum":
            actual = self.t.enum_type_data_t()
            mask = (1 << (record["size"] * 8)) - 1
            expected = {record["name"] + "_" + member["name"]: member["value"] & mask
                        for member in record["values"]}
            if (not value.get_enum_details(actual)
                    or {member.name: member.value & mask for member in actual} != expected):
                raise ValueError("Canonical enum values differ")

    def forward_local(self, name):
        self.forward(self.t.get_idati(), name)
        value = self.current(name)
        if value is None or not value.is_forward_struct():
            raise ValueError("New forward declaration changed")

    def install(self, record, replace):
        if record["kind"] == "enum":
            if replace or self.current(record["name"]) is not None:
                raise ValueError("Existing enum definition is preserved")
            sign = self.node(self.t.get_idati(), record["underlying"]).get_sign()
            self.t.create_enum_type(record["name"], self.enum_details(record), record["size"], sign, False)
            current = self.current(record["name"])
            if current is None:
                raise ValueError("Cannot create canonical ordinary enum")
            self.verify(current, record)
            return current
        return self.define(self.t.get_idati(), record, replace)

    def enum_snapshots(self, names):
        snapshots = {}
        for name in names:
            value = self.current(name)
            if value is None or not value.is_enum():
                continue
            detail = self.t.enum_type_data_t()
            if not value.get_enum_details(detail):
                raise ValueError("Cannot snapshot existing enum")
            metadata = (value.get_type_cmt(), tuple((m.name, m.value, m.cmt) for m in detail))
            snapshots[name] = (self.signature(value), metadata)
        return snapshots

    def restore_enums(self, snapshots):
        # IDA can infer bitmask semantics while synchronizing a dependent structure.
        for name, before in snapshots.items():
            after = self.enum_snapshots([name]).get(name)
            if after == before:
                continue
            if after is None or after[1] != before[1]:
                raise ValueError("Existing enum metadata changed")
            original, changed = before[0], after[0]
            restored = changed[:2] + (original[2],) + changed[3:]
            if (restored != original or original[2] & self.t.BTE_BITMASK
                    or changed[2] != original[2] | self.t.BTE_BITMASK):
                raise ValueError("Existing enum semantics changed")
            if self.current(name).set_enum_is_bitmask(self.t.tinfo_t.ENUMBM_OFF) != self.t.TERR_OK:
                raise ValueError("Cannot restore ordinary enum semantics")
            if self.enum_snapshots([name]).get(name) != before:
                raise ValueError("Ordinary enum restoration differs")


class TypePlan:
    def __init__(self, profiles, build, manifest, adapter, root):
        self.profiles, self.build, self.manifest = profiles, build, manifest
        self.adapter, self.root = adapter, root
        self.til, self.types, self.decisions = adapter.open(), {}, []
        self.records = {r["name"]: r for r in manifest["types"]}
        self.dependencies = {name: record_dependencies(record) - {name} for name, record in self.records.items()}

    def close(self):
        if self.til is not None:
            self.types.clear()
            self.decisions.clear()
            self.adapter.close(self.til)
            self.til = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def closure(self, name):
        result, pending = set(), [name]
        while pending:
            current = pending.pop()
            if current not in result:
                result.add(current)
                pending.extend(self.dependencies[current])
        return result

    def node(self, node):
        return self.adapter.node(self.til, node)

    def cpp_node(self, name):
        mapping = self.manifest.get("cpp_names", {})
        normalized = re.sub(r"\s+", "", name)
        node = next((value for key, value in mapping.items() if re.sub(r"\s+", "", key) == normalized), None)
        if node is None:
            raise ValueError("Unresolved canonical C++ type: " + name)
        return node

    def cpp_type(self, name):
        return self.node(self.cpp_node(name))

    parse_cpp = cpp_type

    def make_function(self, entry):
        value = self.node({"kind": "function", "return": self.cpp_node(entry["return"]),
                           "params": [self.cpp_node(spelling) for spelling, _ in entry["params"]],
                           "convention": entry["convention"]})
        detail = self.adapter.t.func_type_data_t()
        if not value.get_func_details(detail):
            raise ValueError("Cannot inspect canonical function prototype")
        for arg, (_, name) in zip(detail, entry["params"]):
            arg.name = name
        if not value.create_func(detail):
            raise ValueError("Cannot retain canonical parameter names")
        return value


def _identity(profiles, build, adapter):
    if (not adapter.is_32bit() or digest(adapter.input_sha256()) != build["sha256"]
            or not any(p["id"] == build["id"] and p["sha256"] == build["sha256"]
                       and p.get("rvas") is not None for p in profiles["builds"])):
        raise ValueError("Canonical types require an exact mapped 32-bit executable")


def plan(profiles, build, adapter=None, root=None, manifest=None):
    adapter, root = adapter or IdaAdapter(), Path(root or ROOT)
    adapter.wait()
    _identity(profiles, build, adapter)
    manifest = copy.deepcopy(manifest) if manifest is not None else json.loads(
        (root / "src/game/ida/types.json").read_text(encoding="utf-8"))
    validate(manifest, root)
    manifest = project(manifest, build["id"])
    validate(manifest, root)
    result = TypePlan(profiles, copy.deepcopy(build), manifest, adapter, root)
    try:
        for name, record in result.records.items():
            if record["kind"] in ("struct", "opaque"):
                adapter.forward(result.til, name)
        pending = list(result.records)
        errors = {}
        while pending:
            progress = False
            for name in list(pending):
                try:
                    result.types[name] = adapter.define(result.til, result.records[name])
                except ValueError as error:
                    errors[name] = str(error)
                    continue
                pending.remove(name)
                progress = True
            if not progress:
                details = "; ".join(name + ": " + errors[name] for name in pending)
                raise ValueError("Canonical type dependencies or sizes cannot be resolved: " + details)
        for name, record in result.records.items():
            result.types[name] = adapter.named(result.til, name)
            adapter.verify(result.types[name], record)
        conflicts = {name for name, expected in result.types.items()
                     if (current := adapter.current(name)) is not None and not adapter.identical(current, expected)}
        for name, expected in result.types.items():
            status = "conflict" if name in conflicts else (
                "dependency_conflict" if result.closure(name) & conflicts else (
                    "already" if adapter.current(name) is not None else "ready"))
            result.decisions.append((status, name, expected))
        return result
    except Exception:
        result.close()
        raise


def run(planned, apply=False):
    if planned.til is None:
        raise ValueError("Canonical type preview has already been closed")
    outcomes = {name: status for status, name, _ in planned.decisions}
    if not apply:
        return [(status, name) for status, name, _ in planned.decisions]
    adapter, owned = planned.adapter, {}
    enums = [name for name, record in planned.records.items() if record["kind"] == "enum"]

    def guard(name):
        _identity(planned.profiles, planned.build, adapter)
        fresh(planned.manifest, planned.root)
        for dependency in planned.closure(name):
            if outcomes[dependency] in ("failed", "changed_since_plan", "conflict", "dependency_conflict"):
                raise ValueError("Canonical dependency is unavailable")
            current = adapter.current(dependency)
            if dependency in owned:
                if current is None or adapter.snapshot(current) != owned[dependency]:
                    raise ValueError("Owned forward declaration changed")
            elif current is not None and not adapter.identical(current, planned.types[dependency]):
                raise ValueError("Canonical dependency changed")

    for status, name, _ in planned.decisions:
        if status != "ready":
            continue
        try:
            guard(name)
            if adapter.current(name) is not None:
                outcomes[name] = "already"
            elif planned.records[name]["kind"] in ("struct", "opaque"):
                adapter.forward_local(name)
                owned[name] = adapter.snapshot(adapter.current(name))
        except ValueError:
            outcomes[name] = "changed_since_plan"
    pending = [name for status, name, _ in planned.decisions if outcomes[name] == "ready"]
    pending.sort(key=lambda name: planned.records[name]["kind"] != "enum")
    while pending:
        progress = False
        for name in list(pending):
            try:
                guard(name)
            except ValueError:
                outcomes[name] = "changed_since_plan"
            else:
                try:
                    snapshots = adapter.enum_snapshots(enums)
                    try:
                        current = adapter.install(planned.records[name], name in owned)
                    finally:
                        adapter.restore_enums(snapshots)
                    adapter.verify(current, planned.records[name])
                    owned[name] = adapter.snapshot(current)
                    outcomes[name] = "applied"
                except ValueError:
                    current = adapter.current(name)
                    if current is None or (name in owned and adapter.snapshot(current) == owned[name]):
                        continue
                    outcomes[name] = "failed"
            pending.remove(name)
            progress = True
        if not progress:
            for name in pending:
                outcomes[name] = "failed"
            break
    for name, status in list(outcomes.items()):
        if status == "applied" and not adapter.identical(adapter.current(name), planned.types[name]):
            outcomes[name] = "failed"
    return [(outcomes[name], name) for _, name, _ in planned.decisions]
