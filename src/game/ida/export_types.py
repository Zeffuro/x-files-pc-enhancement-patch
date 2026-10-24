"""Export verified native C++ layouts using a 32-bit Microsoft ABI Clang AST."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys


HERE = Path(__file__).resolve().parent
GAME = HERE.parent
ROOT = GAME.parents[1]
OUTPUT = HERE / "types.json"
TRIPLE = "i686-pc-windows-msvc"


def inputs():
    return sorted((GAME / "layouts").rglob("*.h")) + [
        GAME / "access/views.json", GAME / "functions/catalog.json",
        GAME / "functions/generated.h", GAME / "profiles/builds.json",
        GAME / "profiles/generated.h", Path(__file__).resolve(),
    ]


def source_hashes():
    return {path.relative_to(ROOT).as_posix(): source_digest(path.read_bytes())
            for path in inputs()}


def source_digest(data):
    """Keep text identities stable across Git's Windows checkout conversion."""
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def check_fresh(data):
    if data.get("schema") != 1 or data.get("sources") != source_hashes():
        raise ValueError("Native IDA types are stale. Run src/game/ida/export_types.py")
    if data.get("target") != {"triple": TRIPLE, "pointer_size": 4}:
        raise ValueError("Native IDA types require the verified 32-bit Microsoft ABI")


def compiler_args(msvc=None, sdk=None):
    if not msvc:
        base = Path(os.environ.get("ProgramFiles", "C:/Program Files"))
        choices = sorted(base.glob("Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/include"))
        if not choices:
            raise ValueError("MSVC headers were not found. Supply --msvc-include")
        msvc = choices[-1]
    if not sdk:
        base = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
        choices = sorted((base / "Windows Kits/10/Include").glob("*/um/windows.h"))
        if not choices:
            raise ValueError("Windows SDK headers were not found. Supply --sdk-include")
        sdk = choices[-1].parents[1]
    return ["-x", "c++", "-std=c++20", "--target=" + TRIPLE, "-fms-extensions",
            "-fms-compatibility", "-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH",
            "-D_HAS_CXX20=0", "-D_CRT_USE_BUILTIN_OFFSETOF", "-DWIN32_LEAN_AND_MEAN",
            "-DNOMINMAX", "-DUNICODE", "-D_UNICODE", "-I" + str(ROOT / "src"),
            "-isystem", str(msvc), "-isystem", str(Path(sdk) / "ucrt"),
            "-isystem", str(Path(sdk) / "shared"), "-isystem", str(Path(sdk) / "um")]


def qualified(cursor):
    names = []
    while cursor is not None and cursor.kind.name != "TRANSLATION_UNIT":
        if cursor.spelling:
            names.append(cursor.displayname if cursor.kind.name in
                         ("STRUCT_DECL", "CLASS_DECL") else cursor.spelling)
        cursor = cursor.semantic_parent
    return "::".join(reversed(names))


def type_name(cpp):
    cpp = cpp.removeprefix("native_game::")
    return "XFiles_" + re.sub(r"[^A-Za-z0-9]+", "_", cpp).strip("_")


def profile_entries(views):
    view_types = {obj["name"]: obj["layout"] for group in views.values()
                  for obj in group["objects"]}
    entries = []
    for group in views.values():
        for obj in group["objects"]:
            for field in obj["fields"]:
                if "build_profile->" not in field["offset"]:
                    continue
                kind = field.get("type") or view_types.get(field.get("view"))
                if kind is None:
                    kind = f"decltype({obj['layout']}::{field.get('member', field['name'])})"
                entries.append({"cpp_record": obj["layout"], "field": field["name"],
                                "cpp_type": kind, "expression": field["offset"]})
    return entries


def translation_unit():
    headers = sorted((GAME / "layouts").rglob("*.h"))
    catalog = json.loads((GAME / "functions/catalog.json").read_text(encoding="utf-8"))
    profiles = json.loads((GAME / "profiles/builds.json").read_text(encoding="utf-8"))["builds"]
    views = json.loads((GAME / "access/views.json").read_text(encoding="utf-8"))
    dynamic = profile_entries(views)
    spellings = {kind for _, kind in catalog["globals"]}
    for function in catalog["functions"] + catalog.get("vtable_functions", []):
        spellings.add(function["return"])
        spellings.update(kind for kind, _ in function["params"])
    spellings.update(entry["cpp_type"] for entry in dynamic)
    spellings = sorted(spellings)
    text = [f'#include "{path.relative_to(ROOT / "src").as_posix()}"' for path in headers]
    text += ['#include "game/functions/generated.h"', "namespace native_game {",
             "using DrawList = NativeFunctions::DrawList;"]
    for index, spelling in enumerate(spellings):
        text.append(f"using __IdaType{index} = {spelling};")
    for index, entry in enumerate(dynamic):
        for build in profiles:
            expression = entry["expression"].replace("build_profile->", "profile_" + build["id"] + ".")
            text.append(f"enum {{ __IdaOffset{index}_{build['id']} = {expression} }};")
    text.append("}")
    return "\n".join(text), headers, spellings, dynamic, profiles


class Exporter:
    def __init__(self, cindex, headers):
        self.cindex = cindex
        self.headers = {str(path.resolve()).casefold() for path in headers}
        self.types = {}
        self.cpp_names = {}
        self.coverage = []

    def remember(self, cpp, node):
        self.cpp_names[cpp] = node
        if cpp.startswith("native_game::"):
            self.cpp_names[cpp[len("native_game::"):]] = node
        return node

    def node(self, kind):
        original = kind.spelling
        const = kind.is_const_qualified()
        name = kind.kind.name
        if name in ("TYPEDEF", "ELABORATED", "UNEXPOSED", "AUTO"):
            canonical = kind.get_canonical()
            if canonical.kind == kind.kind and canonical.spelling == kind.spelling:
                raise ValueError("Unresolved native C++ type: " + original)
            result = self.node(canonical)
        elif name == "POINTER":
            result = {"kind": "pointer", "to": self.node(kind.get_pointee())}
        elif name == "CONSTANTARRAY":
            result = {"kind": "array", "count": kind.element_count,
                      "element": self.node(kind.element_type)}
        elif name == "RECORD" and kind.spelling.startswith("std::array<"):
            record = kind.get_declaration()
            fields = list(record.type.get_fields())
            if len(fields) != 1:
                raise ValueError("Unsupported standard array representation: " + original)
            result = self.node(fields[0].type)
            if kind.get_size() != fields[0].type.get_size() or record.type.get_offset(fields[0].spelling) != 0:
                raise ValueError("Standard array contains unmodeled storage")
        elif name in ("RECORD", "ENUM"):
            declaration = kind.get_declaration()
            cpp = qualified(declaration)
            if cpp == "std::byte":
                result = {"kind": "scalar", "name": "unsigned char"}
            else:
                exported = self.record(declaration)
                result = {"kind": "named", "name": exported}
        elif name in ("FUNCTIONPROTO", "FUNCTIONNOPROTO"):
            if name == "FUNCTIONNOPROTO" or kind.is_function_variadic():
                raise ValueError("Incomplete or variadic native signature: " + original)
            convention = "__stdcall" if "stdcall" in original else (
                "__thiscall" if "thiscall" in original else "__cdecl")
            result = {"kind": "function", "return": self.node(kind.get_result()),
                      "params": [self.node(param) for param in kind.argument_types()],
                      "convention": convention}
        else:
            scalars = {"VOID": "void", "BOOL": "bool", "CHAR_S": "char", "CHAR_U": "char",
                       "SCHAR": "signed char", "UCHAR": "unsigned char", "SHORT": "short",
                       "USHORT": "unsigned short", "INT": "int", "UINT": "unsigned int",
                       "LONG": "long", "ULONG": "unsigned long", "LONGLONG": "long long",
                       "ULONGLONG": "unsigned long long", "FLOAT": "float", "DOUBLE": "double",
                       "WCHAR": "wchar_t"}
            if name not in scalars:
                raise ValueError("Unsupported native type: " + original + " (" + name + ")")
            result = {"kind": "scalar", "name": scalars[name]}
        result = dict(result)
        if const:
            result["const"] = True
        self.remember(original, result)
        return result

    def record(self, cursor):
        cpp = qualified(cursor)
        name = type_name(cpp)
        if name in self.types:
            if self.types[name]["cpp_name"] != cpp:
                raise ValueError("Flattened native type collision: " + cpp)
            return name
        definition = cursor.get_definition()
        if definition is not None:
            cursor = definition
        row = {"name": name, "cpp_name": cpp}
        self.types[name] = row
        self.remember(cpp, {"kind": "named", "name": name})
        if not cursor.is_definition() or cursor.type.get_size() < 0:
            row["kind"] = "opaque"
            return name
        row.update(size=cursor.type.get_size(), align=cursor.type.get_align())
        if cursor.kind.name == "ENUM_DECL":
            row.update(kind="enum", underlying=self.node(cursor.enum_type), values=[
                {"name": child.spelling, "value": child.enum_value} for child in cursor.get_children()
                if child.kind.name == "ENUM_CONSTANT_DECL"])
        else:
            if cursor.kind.name not in ("STRUCT_DECL", "CLASS_DECL"):
                raise ValueError("Unsupported native record declaration: " + cpp)
            fields = []
            for field in cursor.type.get_fields():
                offset = field.get_field_offsetof()
                if not field.spelling or field.is_bitfield() or offset < 0 or offset % 8:
                    raise ValueError("Unsupported native member: " + cpp + "::" + field.spelling)
                fields.append({"name": field.spelling, "offset": offset // 8,
                               "size": field.type.get_size(), "type": self.node(field.type)})
            if any(child.kind.name in ("CXX_BASE_SPECIFIER", "UNION_DECL") for child in cursor.get_children()):
                raise ValueError("Unsupported native inheritance or union: " + cpp)
            row.update(kind="struct", fields=fields)
        return name

    def collect(self, cursor):
        for child in cursor.get_children():
            filename = str(Path(child.location.file.name).resolve()).casefold() if child.location.file else ""
            owned = filename in self.headers
            category = child.kind.name
            if category == "NAMESPACE":
                if child.spelling == "native_game" or qualified(child).startswith("native_game::"):
                    self.collect(child)
            elif owned and category in ("STRUCT_DECL", "CLASS_DECL", "ENUM_DECL"):
                self.record(child)
                self.collect(child)
                self.coverage.append({"cpp_name": qualified(child), "kind": category, "status": "exported"})
            elif owned and category == "CLASS_TEMPLATE":
                self.coverage.append({"cpp_name": qualified(child), "kind": category,
                                      "status": "concrete_instantiations_only"})
            elif owned and category in ("TYPE_ALIAS_DECL", "TYPEDEF_DECL"):
                cpp = qualified(child)
                name = type_name(cpp)
                self.types[name] = {"name": name, "cpp_name": cpp, "kind": "alias",
                                    "size": child.underlying_typedef_type.get_size(),
                                    "align": child.underlying_typedef_type.get_align(),
                                    "target": self.node(child.underlying_typedef_type)}
                self.remember(cpp, {"kind": "named", "name": name})
                self.coverage.append({"cpp_name": cpp, "kind": category, "status": "exported"})


def generate(msvc=None, sdk=None, dependency_dir=None):
    if dependency_dir:
        sys.path.insert(0, str(dependency_dir))
    try:
        from clang import cindex
    except ImportError as error:
        raise ValueError("Generation requires the libclang Python package") from error
    text, headers, spellings, dynamic, profiles = translation_unit()
    path = str(HERE / "__native_type_export.cpp")
    def parse(source):
        unit = cindex.Index.create().parse(path, args=compiler_args(msvc, sdk),
                                           unsaved_files=[(path, source)],
                                           options=cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
        errors = [str(diagnostic) for diagnostic in unit.diagnostics if diagnostic.severity >= 3]
        if errors:
            raise ValueError("Native C++ type extraction failed:\n" + "\n".join(errors[:12]))
        exporter = Exporter(cindex, headers)
        exporter.collect(unit.cursor)
        return unit, exporter

    unit, exporter = parse(text)
    concrete = sorted(row["cpp_name"] for row in exporter.types.values()
                      if row["kind"] == "opaque" and "<" in row["cpp_name"])
    if concrete:
        text += "\n" + "\n".join(f"static_assert(sizeof({cpp}) > 0);" for cpp in concrete)
        unit, exporter = parse(text)
    anchors = {}
    offsets = {}
    for cursor in unit.cursor.walk_preorder():
        if cursor.spelling.startswith("__IdaType") and cursor.kind.name == "TYPE_ALIAS_DECL":
            anchors[int(cursor.spelling[len("__IdaType"):])] = cursor.underlying_typedef_type
        elif cursor.spelling.startswith("__IdaOffset") and cursor.kind.name == "ENUM_CONSTANT_DECL":
            index, build = cursor.spelling[len("__IdaOffset"):].split("_", 1)
            offsets[(int(index), build)] = cursor.enum_value
    for index, spelling in enumerate(spellings):
        exporter.remember(spelling, exporter.node(anchors[index]))
    profile_fields = []
    for index, entry in enumerate(dynamic):
        row = dict(entry)
        row["record"] = type_name("native_game::" + row.pop("cpp_record"))
        row["type"] = exporter.cpp_names[entry["cpp_type"]]
        row["offsets"] = {build["id"]: offsets[index, build["id"]] for build in profiles}
        profile_fields.append(row)
    return {"schema": 1, "sources": source_hashes(),
            "target": {"triple": TRIPLE, "pointer_size": 4},
            "types": sorted(exporter.types.values(), key=lambda row: row["name"]),
            "cpp_names": dict(sorted(exporter.cpp_names.items())),
            "profile_fields": profile_fields,
            "coverage": {"headers": [path.relative_to(ROOT).as_posix() for path in headers],
                         "declarations": sorted(exporter.coverage, key=lambda row: row["cpp_name"]),
                         "instantiations": sorted(row["cpp_name"] for row in exporter.types.values()
                                                  if "<" in row["cpp_name"])}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Check all canonical source hashes without Clang")
    parser.add_argument("--verify-ast", action="store_true", help="Regenerate and compare the complete AST export")
    parser.add_argument("--dependency-dir", type=Path)
    parser.add_argument("--msvc-include", type=Path)
    parser.add_argument("--sdk-include", type=Path)
    args = parser.parse_args()
    try:
        if args.check or args.verify_ast:
            current = json.loads(OUTPUT.read_text(encoding="utf-8"))
            check_fresh(current)
            if args.verify_ast and current != generate(args.msvc_include, args.sdk_include, args.dependency_dir):
                raise ValueError("Native IDA AST export differs from canonical C++ definitions")
        else:
            data = generate(args.msvc_include, args.sdk_include, args.dependency_dir)
            OUTPUT.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
            print(f"Exported {len(data['types'])} native types and {len(data['profile_fields'])} profile fields")
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error


if __name__ == "__main__":
    main()
