"""Preview native profile names in IDA. Call run(apply=True) to apply safe names."""

import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def _generator():
    spec = importlib.util.spec_from_file_location("game_profile_generator", HERE / "generate_profiles.py")
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


def plan(data, adapter, functions=None):
    """Return a build and (status, address, desired, current) decisions."""
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
    base = adapter.imagebase()
    decisions = []
    for field, rva in build["rvas"].items():
        ea = base + rva
        desired = "XFiles_" + field
        if not adapter.is_mapped(ea):
            status, current = "unmapped", ""
        else:
            current = adapter.name(ea)
            owner = adapter.name_owner(desired)
            if field in function_fields and not adapter.is_function(ea):
                status = "not_function"
            elif current == desired:
                status = "already"
            elif adapter.has_user_name(ea):
                status = "user_name"
            elif owner is not None and owner != ea:
                status = "collision"
            else:
                status = "ready"
        decisions.append((status, ea, desired, current))
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


def run(apply=False, adapter=None):
    """Preview by default. Call explicitly with apply=True inside IDA to rename."""
    adapter = adapter or IdaAdapter()
    adapter.wait()
    data = json.loads((HERE.parent / "profiles/builds.json").read_text(encoding="utf-8"))
    functions = json.loads((HERE.parent / "profiles/functions.json").read_text(encoding="utf-8"))
    planned_base = adapter.imagebase()
    try:
        build, decisions = plan(data, adapter, functions)
    except ValueError as error:
        adapter.log("X-Files profile import stopped: " + str(error))
        return []
    if adapter.imagebase() != planned_base or _digest(adapter.input_sha256()) != build["sha256"]:
        adapter.log("X-Files profile import stopped: input image changed during planning")
        return []
    adapter.log(f"X-Files {build['label']} at {planned_base:#x}: " +
                ("apply" if apply else "preview"))
    roles = {"XFiles_" + item["field"]: item["role"] for item in functions["functions"]}
    outcomes = []
    for status, ea, desired, current in decisions:
        if apply and status == "ready":
            if (adapter.imagebase() != planned_base or
                _digest(adapter.input_sha256()) != build["sha256"] or
                not adapter.is_mapped(ea) or
                (desired in roles and not adapter.is_function(ea)) or
                adapter.name(ea) != current or adapter.has_user_name(ea) or
                adapter.name_owner(desired) not in (None, ea)):
                status = "changed_since_plan"
            elif adapter.set_name(ea, desired):
                status = "applied"
            else:
                status = "failed"
        outcomes.append((status, ea, desired, current))
        adapter.log(f"  {status:18} {ea:#x} {desired}" +
                    (f" [{roles[desired]}]" if desired in roles else "") +
                    (f" (existing: {current})" if current else ""))
    return outcomes


if __name__ == "__main__":
    run()
