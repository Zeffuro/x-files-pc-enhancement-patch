"""Import verified native names, C++ layouts and complete known signatures."""

import importlib.util
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent


def _generator():
    spec = importlib.util.spec_from_file_location("game_profile_generator", HERE / "generate_profiles.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _symbols():
    spec = importlib.util.spec_from_file_location("game_ida_symbols", HERE / "ida_symbols.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _types():
    spec = importlib.util.spec_from_file_location("game_ida_types", HERE / "ida_types.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _digest(value):
    if isinstance(value, bytes):
        if len(value) == 32:
            return value.hex()
        try:
            value = value.decode("ascii")
        except UnicodeDecodeError:
            return None
    if isinstance(value, str) and len(value) == 64 and all(c in "0123456789abcdef" for c in value):
        return value
    return None


def _guard(adapter, symbol, base, symbols):
    ea = base + symbol["rva"]
    if not adapter.is_mapped(ea):
        return "unmapped"
    if symbol["kind"] == "function" and not adapter.is_function(ea):
        return "not_function"
    if symbol.get("source"):
        if symbol["kind"] == "data":
            check = getattr(adapter, "is_data_range", None)
            if check is None:
                return "unsupported_adapter"
            if not check(ea, symbol["size"]):
                return "not_data"
        read = getattr(adapter, "read_bytes", None)
        if symbol["fingerprint"] is not None and read is None:
            return "bytes_unavailable"
        if not symbols.matches(symbol, base, read):
            return "fingerprint_mismatch"
    return None


def _managed_functions(definitions):
    return {(row["rva"], row["legacy_name"]): row for row in definitions if row["kind"] == "function"}


def _plan(data, adapter, functions=None, research=True):
    generator = _generator()
    generator.validate(data)
    if functions is None:
        functions = json.loads((HERE.parent / "profiles/functions.json").read_text(encoding="utf-8"))
    generator.validate_functions(functions, data)
    function_fields = {item["field"] for item in functions["functions"]}
    if not adapter.is_32bit():
        raise ValueError("A 32-bit executable database is required")
    digest = _digest(adapter.input_sha256())
    build = next((item for item in data["builds"] if item["sha256"] == digest), None)
    if build is None:
        raise ValueError("Input executable SHA-256 is unknown")
    if build["rvas"] is None:
        raise ValueError("This build has no verified native mapping")
    roles = {item["field"]: item["role"] for item in functions["functions"]}
    definitions = [{"name": field if field in function_fields else "XFiles_" + field,
                    "legacy_name": "XFiles_" + field, "rva": rva,
                    "kind": "function" if field in function_fields else "profile",
                    "role": roles.get(field, "")}
                   for field, rva in build["rvas"].items()]
    symbols = None
    if research:
        symbols = _symbols()
        try:
            catalog = symbols.load(data)
        except OSError as error:
            raise ValueError("Supplemental IDA catalog unavailable: " + str(error)) from error
        definitions.extend(dict(row, name=row["source_name"] if row["kind"] == "function" else row["name"],
                                legacy_name=row["name"]) for row in symbols.rows(catalog, build["id"]))
    managed = _managed_functions(definitions)
    base = adapter.imagebase()
    decisions = []
    addresses = set()
    for symbol in definitions:
        ea = base + symbol["rva"]
        desired = symbol["name"]
        alias = ea in addresses
        addresses.add(ea)
        if not adapter.is_mapped(ea):
            status, current = "unmapped", ""
        else:
            current = adapter.name(ea)
            owner = adapter.name_owner(desired)
            if alias:
                status = "alias"
            elif invalid := _guard(adapter, symbol, base, symbols):
                status = invalid
            elif current == desired:
                status = "already"
            elif legacy := managed.get((symbol["rva"], current)):
                status = _guard(adapter, legacy, base, symbols) or (
                    "collision" if owner is not None and owner != ea else "migrate")
            elif adapter.has_user_name(ea):
                status = "user_name"
            elif owner is not None and owner != ea:
                status = "collision"
            else:
                status = "ready"
        decisions.append((status, ea, desired, current))
    return build, decisions, definitions, symbols


def plan(data, adapter, functions=None, research=True):
    """Return a build and (status, address, desired, current) decisions."""
    build, decisions, _, _ = _plan(data, adapter, functions, research)
    return build, decisions


class IdaAdapter:
    def __init__(self):
        import ida_auto
        import ida_bytes
        import ida_funcs
        import ida_ida
        import ida_idaapi
        import ida_kernwin
        import ida_nalt
        import ida_name

        self.auto = ida_auto
        self.bytes = ida_bytes
        self.funcs = ida_funcs
        self.ida = ida_ida
        self.api = ida_idaapi
        self.ui = ida_kernwin
        self.nalt = ida_nalt
        self.names = ida_name

    def wait(self):
        self.auto.auto_wait()

    def input_sha256(self):
        return self.nalt.retrieve_input_file_sha256()

    def is_32bit(self):
        return self.ida.inf_is_32bit_exactly()

    def imagebase(self):
        return self.nalt.get_imagebase()

    def is_mapped(self, ea):
        return self.bytes.is_mapped(ea)

    def is_function(self, ea):
        function = self.funcs.get_func(ea)
        return function is not None and function.start_ea == ea

    def is_data_range(self, ea, size):
        return all(self.bytes.is_mapped(address) and
                   not self.bytes.is_code(self.bytes.get_flags(address))
                   for address in range(ea, ea + size))

    def read_bytes(self, ea, size):
        return self.bytes.get_bytes(ea, size)

    def name(self, ea):
        return self.names.get_name(ea)

    def has_user_name(self, ea):
        return self.bytes.has_user_name(self.bytes.get_flags(ea))

    def name_owner(self, name):
        ea = self.names.get_name_ea(self.api.BADADDR, name)
        return None if ea == self.api.BADADDR else ea

    def set_name(self, ea, name):
        return self.names.set_name(ea, name, self.names.SN_CHECK | self.names.SN_NOWARN)

    def log(self, message):
        self.ui.msg(message + "\n")


def run(apply=False, adapter=None, research=True, expected_identity=None):
    """Preview by default. Call explicitly with apply=True inside IDA to rename."""
    adapter = adapter or IdaAdapter()
    adapter.wait()
    if expected_identity is not None and (
            _digest(adapter.input_sha256()), adapter.imagebase()) != expected_identity:
        raise ValueError("Input image changed before naming")
    data = json.loads((HERE.parent / "profiles/builds.json").read_text(encoding="utf-8"))
    functions = json.loads((HERE.parent / "profiles/functions.json").read_text(encoding="utf-8"))
    planned_base = adapter.imagebase()
    try:
        build, decisions, definitions, symbols = _plan(data, adapter, functions, research)
    except ValueError as error:
        adapter.log("X-Files profile import stopped: " + str(error))
        return []
    if expected_identity is not None and (
            _digest(adapter.input_sha256()), adapter.imagebase()) != expected_identity:
        raise ValueError("Input image changed during name planning")
    if adapter.imagebase() != planned_base or _digest(adapter.input_sha256()) != build["sha256"]:
        adapter.log("X-Files profile import stopped: input image changed during planning")
        return []
    adapter.log(f"X-Files {build['label']} at {planned_base:#x}: " +
                ("apply" if apply else "preview"))
    descriptions = {item["name"]: item for item in definitions}
    managed = _managed_functions(definitions)
    outcomes = []
    for status, ea, desired, current in decisions:
        if apply and status in ("ready", "migrate"):
            legacy = managed.get((ea - planned_base, current)) if status == "migrate" else None
            if (adapter.imagebase() != planned_base or
                _digest(adapter.input_sha256()) != build["sha256"] or
                _guard(adapter, descriptions[desired], planned_base, symbols) or
                (status == "migrate" and (legacy is None or _guard(adapter, legacy, planned_base, symbols))) or
                adapter.name(ea) != current or (adapter.has_user_name(ea) and legacy is None) or
                adapter.name_owner(desired) not in (None, ea)):
                status = "changed_since_plan"
            elif adapter.set_name(ea, desired):
                status = "applied"
            else:
                status = "failed"
        outcomes.append((status, ea, desired, current))
        description = descriptions[desired]
        role = description.get("role", "")
        source = description.get("source", "")
        detail = role or (f"{source}; {description['kind']}; {description['size']} bytes" if source else "")
        adapter.log(f"  {status:18} {ea:#x} {desired}" +
                    (f" [{detail}]" if detail else "") +
                    (f" (existing: {current})" if current else ""))
    return outcomes


def _argument_details(adapter, value):
    if value is None:
        return None
    detail = adapter.types.func_type_data_t()
    return detail if value.get_func_details(detail, adapter.types.GTD_NO_ARGLOCS) else None


def _argument_names(adapter, value):
    detail = _argument_details(adapter, value)
    return tuple(arg.name for arg in detail) if detail is not None else None


def _named_parameters(adapter, value, desired):
    detail = _argument_details(adapter, value)
    if detail is None or len(detail) != len(desired):
        raise ValueError("Verified function parameters differ")
    owners = {}
    for index, argument in enumerate(detail):
        owners.setdefault(argument.name, set()).add(index)
    for index, (argument, name) in enumerate(zip(detail, desired)):
        if not argument.name or re.fullmatch(r"arg[0-9]+|arg_[0-9A-Fa-f]+|a[1-9][0-9]*", argument.name):
            if owners.get(name, set()) - {index}:
                continue
            owners[argument.name].discard(index)
            argument.name = name
            owners.setdefault(name, set()).add(index)
    result = adapter.types.tinfo_t()
    merged = tuple(arg.name for arg in detail)
    if not result.create_func(detail):
        raise ValueError("Cannot retain verified parameter names")
    return result, merged


def _annotations(build, planned, names):
    catalog = json.loads((HERE.parent / "functions/catalog.json").read_text(encoding="utf-8"))
    rows = []
    for entry in catalog["functions"]:
        node = {"kind": "function", "return": planned.cpp_node(entry["return"]),
                "params": [planned.cpp_node(kind) for kind, _ in entry["params"]],
                "convention": entry["convention"]}
        rows.append((entry["field"], "function", node, tuple(name for _, name in entry["params"])))
    rows += [(field, "global", planned.cpp_node(kind), None) for field, kind in catalog["globals"]]
    decisions = []
    adapter = planned.adapter
    types = _types()
    blocked = {name for status, name, _ in planned.decisions
               if status not in ("ready", "already")}
    for field, kind, node, parameters in rows:
        ea = names.imagebase() + build["rvas"][field]
        expected = planned.node(node)
        dependencies = set().union(*(planned.closure(name) for name in types.dependencies(node)))
        current = adapter.getexisting_tinfo(ea)
        argument_names = _argument_names(adapter, current) if parameters is not None else None
        if dependencies & blocked:
            status = "dependency_conflict"
        elif kind == "function" and not names.is_function(ea):
            status = "not_function"
        elif kind == "global" and not names.is_data_range(ea, expected.get_size()):
            status = "not_data"
        elif current is not None and adapter.identical(current, expected):
            status = "parameters" if parameters is not None and _named_parameters(
                adapter, current, parameters)[1] != argument_names else "already"
        elif current is not None and adapter.is_userti(ea):
            status = "user_type"
        else:
            status = "ready"
        decisions.append((status, ea, field, node, parameters, argument_names))
    return decisions


def _apply_annotations(decisions, planned, names, apply, base):
    adapter = planned.adapter
    outcomes = []
    types = _types()
    for status, ea, field, node, parameters, argument_names in decisions:
        if apply and status in ("ready", "parameters"):
            try:
                types.fresh(planned.manifest, planned.root)
                if (names.imagebase() != base or not names.is_32bit() or
                        _digest(names.input_sha256()) != planned.build["sha256"]):
                    raise ValueError("Input image changed")
                dependencies = set().union(*(planned.closure(name) for name in types.dependencies(node)))
                for name in dependencies:
                    current = adapter.current(name)
                    if current is None or not adapter.identical(current, planned.types[name]):
                        raise ValueError("Canonical dependency changed")
                expected = adapter.node(adapter.types.get_idati(), node)
                current = adapter.getexisting_tinfo(ea)
                same = current is not None and adapter.identical(current, expected)
                if parameters is not None and _argument_names(adapter, current) != argument_names:
                    status = "changed_since_plan"
                elif status == "parameters" and not same:
                    status = "changed_since_plan"
                elif current is not None and adapter.is_userti(ea) and not same:
                    status = "user_type"
                elif same:
                    status = "parameters" if parameters is not None else "already"
                elif (node["kind"] == "function" and not names.is_function(ea)) or (
                        node["kind"] != "function" and not names.is_data_range(ea, expected.get_size())):
                    status = "changed_since_plan"
                else:
                    if parameters is not None:
                        expected = _named_parameters(adapter, expected, parameters)[0]
                    status = "applied" if adapter.apply_tinfo(ea, expected) else "failed"
                if status == "parameters":
                    expected, merged = _named_parameters(adapter, current, parameters)
                    if not names.is_function(ea):
                        status = "changed_since_plan"
                    elif merged == argument_names:
                        status = "already"
                    else:
                        status = "applied" if adapter.apply_tinfo(ea, expected) else "failed"
            except ValueError:
                status = "changed_since_plan"
        outcomes.append((status, ea, field))
        label = field if node["kind"] == "function" else "XFiles_" + field
        names.log(f"  {status:18} {ea:#x} type {label}")
    return outcomes


def full_run(apply=False, save=False):
    """Preview every verified import stage, or apply them together with apply=True."""
    from collections import Counter

    if save and not apply:
        raise ValueError("A preview does not save the database")
    names = IdaAdapter()
    names.wait()
    data = json.loads((HERE.parent / "profiles/builds.json").read_text(encoding="utf-8"))
    build, _ = plan(data, names)
    identity = build["sha256"], names.imagebase()
    types = _types()
    with types.plan(data, build) as planned:
        annotations = _annotations(build, planned, names)
        name_rows = run(apply=apply, adapter=names, expected_identity=identity)
        if (_digest(names.input_sha256()), names.imagebase()) != identity:
            raise ValueError("Input image changed after naming")
        type_rows = types.run(planned, apply=apply)
        for status, name in type_rows:
            names.log(f"  {status:18} type {name}")
        annotation_rows = _apply_annotations(annotations, planned, names, apply, identity[1])
    result = {"build": build["id"], "names": name_rows, "types": type_rows,
              "annotations": annotation_rows}
    for stage in ("names", "types", "annotations"):
        names.log(f"X-Files {stage}: {dict(Counter(row[0] for row in result[stage]))}")
    if save:
        import ida_loader

        if any(row[0] in ("failed", "changed_since_plan")
               for stage in ("names", "types", "annotations") for row in result[stage]):
            raise RuntimeError("X-Files import needs attention before saving")
        if (_digest(names.input_sha256()), names.imagebase()) != identity:
            raise ValueError("Input image changed before saving")
        types.fresh(planned.manifest, planned.root)
        path = ida_loader.get_path(ida_loader.PATH_TYPE_IDB)
        if not ida_loader.save_database(path, ida_loader.DBFL_COMP):
            raise RuntimeError("X-Files database save failed")
        names.log("X-Files database saved: " + path)
    return result


if globals().get("_XFILES_IMPORT_AUTORUN", True):
    try:
        import ida_kernwin as _ida_runtime
    except ImportError:
        if __name__ == "__main__":
            raise RuntimeError("Run this script inside IDA") from None
    else:
        full_run(apply=True, save=True)
