"""Preview guarded native and supplemental IDA names. Apply only when requested."""

import importlib.util
import json
from pathlib import Path

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
    definitions = [{"name": "XFiles_" + field, "rva": rva,
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
        definitions.extend(symbols.rows(catalog, build["id"]))
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


def run(apply=False, adapter=None, research=True):
    """Preview by default. Call explicitly with apply=True inside IDA to rename."""
    adapter = adapter or IdaAdapter()
    adapter.wait()
    data = json.loads((HERE.parent / "profiles/builds.json").read_text(encoding="utf-8"))
    functions = json.loads((HERE.parent / "profiles/functions.json").read_text(encoding="utf-8"))
    planned_base = adapter.imagebase()
    try:
        build, decisions, definitions, symbols = _plan(data, adapter, functions, research)
    except ValueError as error:
        adapter.log("X-Files profile import stopped: " + str(error))
        return []
    if adapter.imagebase() != planned_base or _digest(adapter.input_sha256()) != build["sha256"]:
        adapter.log("X-Files profile import stopped: input image changed during planning")
        return []
    adapter.log(f"X-Files {build['label']} at {planned_base:#x}: " +
                ("apply" if apply else "preview"))
    descriptions = {item["name"]: item for item in definitions}
    outcomes = []
    for status, ea, desired, current in decisions:
        if apply and status == "ready":
            if (adapter.imagebase() != planned_base or
                _digest(adapter.input_sha256()) != build["sha256"] or
                _guard(adapter, descriptions[desired], planned_base, symbols) or
                adapter.name(ea) != current or adapter.has_user_name(ea) or
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


if __name__ == "__main__":
    run()
