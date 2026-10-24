"""Exercise the complete IDA entry point and address/type mutation guards without IDA."""

import copy
import json
from pathlib import Path
import sys
import types as python_types
import unittest
from unittest.mock import patch

from game_ida import bridge, DATA
from game_ida_research import ResearchIda
from game_ida_types import FakeAdapter


ROOT = Path(__file__).resolve().parents[1]
TYPE_BRIDGE = bridge._types()
CATALOG = json.loads((ROOT / "src/game/functions/catalog.json").read_text(encoding="utf-8"))


class FakeType:
    def __init__(self, node=None, size=0):
        self.node, self.size = copy.deepcopy(node), size
        self.arguments = [python_types.SimpleNamespace(name="arg" + str(i), type=copy.deepcopy(value),
                          flags=0, argloc=("stack", i * 4), cmt="")
                          for i, value in enumerate((node or {}).get("params", []))]
        self.metadata = {"flags": 0, "comment": ""}

    def get_size(self):
        return self.size

    def __eq__(self, other):
        return isinstance(other, FakeType) and self.node == other.node and self.size == other.size

    def get_func_details(self, detail, flags=0):
        if not self.node or self.node["kind"] != "function":
            return False
        detail.extend(copy.deepcopy(self.arguments))
        detail.node, detail.size, detail.metadata = copy.deepcopy(self.node), self.size, copy.deepcopy(self.metadata)
        return True

    def create_func(self, detail):
        self.node, self.size = copy.deepcopy(detail.node), detail.size
        self.arguments, self.metadata = copy.deepcopy(list(detail)), copy.deepcopy(detail.metadata)
        return True


class FakeFunctionData(list):
    pass


class AnnotationAdapter(FakeAdapter):
    def __init__(self, build, names):
        super().__init__(build)
        self.names = names
        self.annotations, self.user_annotations, self.annotation_writes = {}, set(), []
        self.types = python_types.SimpleNamespace(get_idati=lambda: "local", tinfo_t=FakeType,
                                                  func_type_data_t=FakeFunctionData, GTD_NO_ARGLOCS=0)

    def input_sha256(self):
        return self.names.input_sha256()

    def is_32bit(self):
        return self.names.is_32bit()

    def node(self, session, node):
        records = self.local if session == "local" else session["types"]

        def size(value):
            kind = value["kind"]
            if kind == "scalar":
                return {"void": 0, "char": 1, "unsigned char": 1, "signed char": 1, "bool": 1,
                        "short": 2, "unsigned short": 2, "wchar_t": 2, "double": 8,
                        "long long": 8, "unsigned long long": 8}.get(value["name"], 4)
            if kind == "pointer":
                return 4
            if kind == "array":
                return value["count"] * size(value["element"])
            if kind == "function":
                return 0
            record = records[value["name"]]
            return size(record["target"]) if record["kind"] == "alias" else record["size"]

        return FakeType(node, size(node))

    def getexisting_tinfo(self, ea):
        return self.annotations.get(ea)

    def is_userti(self, ea):
        return ea in self.user_annotations

    def apply_tinfo(self, ea, value):
        self.annotations[ea] = copy.deepcopy(value)
        self.user_annotations.add(ea)
        self.annotation_writes.append((ea, value))
        return True


class FullImportTest(unittest.TestCase):
    def setUp(self):
        self.build = DATA["builds"][0]
        self.names = ResearchIda(self.build)
        self.adapter = AnnotationAdapter(self.build, self.names)
        self.original_plan = TYPE_BRIDGE.plan

        def plan(profiles, build):
            return self.original_plan(profiles, build, self.adapter, ROOT)

        self.patches = [patch.object(bridge, "IdaAdapter", return_value=self.names),
                        patch.object(bridge, "_types", return_value=TYPE_BRIDGE),
                        patch.object(TYPE_BRIDGE, "plan", side_effect=plan)]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)

    def ea(self, field):
        return self.names.base + self.build["rvas"][field]

    def annotations(self, result):
        return {field: status for status, _, field in result["annotations"]}

    def test_preview_is_read_only_and_complete_catalog_annotations_are_planned(self):
        result = bridge.full_run()
        expected = {entry["field"] for entry in CATALOG["functions"]}
        expected.update(field for field, _ in CATALOG["globals"])
        actual = set(self.annotations(result))
        self.assertEqual(actual, expected)
        self.assertTrue(all(entry["field"] not in actual for entry in CATALOG["unbound"]))
        self.assertEqual(len(CATALOG["unbound"]), 3)
        self.assertTrue(result["names"] and result["types"] and result["annotations"])
        self.assertEqual(self.names.writes, [])
        self.assertEqual(self.adapter.writes, [])
        self.assertEqual(self.adapter.annotation_writes, [])
        self.assertTrue(all(session["closed"] for session in self.adapter.sessions))

    def test_apply_then_repeat_keeps_all_annotations_and_types_idempotent(self):
        result = bridge.full_run(True)
        self.assertTrue(all(status == "applied" for status, _, _ in result["annotations"]))
        before = (len(self.names.writes), len(self.adapter.writes), len(self.adapter.annotation_writes))
        second = bridge.full_run(True)
        self.assertTrue(all(status == "already" for status, _, _ in second["annotations"]))
        self.assertEqual(before, (len(self.names.writes), len(self.adapter.writes), len(self.adapter.annotation_writes)))
        unbound = {self.ea(entry["field"]) for entry in CATALOG["unbound"]}
        self.assertTrue(unbound.isdisjoint(ea for ea, _ in self.adapter.annotation_writes))

    def test_catalog_parameter_names_apply_and_repeat_for_every_build(self):
        for build in DATA["builds"]:
            with self.subTest(build=build["id"]):
                self.build, self.names = build, ResearchIda(build)
                self.adapter = AnnotationAdapter(build, self.names)
                with patch.object(bridge, "IdaAdapter", return_value=self.names):
                    result = bridge.full_run(True)
                    self.assertTrue(all(status == "applied" for status, _, _ in result["annotations"]))
                    for entry in CATALOG["functions"]:
                        value = self.adapter.annotations[self.ea(entry["field"])]
                        self.assertEqual([arg.name for arg in value.arguments],
                                         [name for _, name in entry["params"]])
                    writes = len(self.adapter.annotation_writes)
                    repeated = bridge.full_run(True)
                    self.assertTrue(all(status == "already" for status, _, _ in repeated["annotations"]))
                    self.assertEqual(len(self.adapter.annotation_writes), writes)

    def test_save_requires_apply_and_uses_explicit_sdk_flags_after_closing_preview(self):
        saved = []

        def save(path, flags):
            self.assertTrue(all(session["closed"] for session in self.adapter.sessions))
            saved.append((path, flags))
            return True

        loader = python_types.SimpleNamespace(PATH_TYPE_IDB=7, DBFL_COMP=0x20,
                                              get_path=lambda kind: "checked-fixture.i64",
                                              save_database=save)
        with patch.dict(sys.modules, {"ida_loader": loader}):
            with self.assertRaisesRegex(ValueError, "preview"):
                bridge.full_run(save=True)
            self.assertEqual(saved, [])
            self.assertEqual(self.names.writes, [])
            bridge.full_run(apply=True, save=True)
        self.assertEqual(saved, [("checked-fixture.i64", loader.DBFL_COMP)])

    def test_existing_user_signature_is_preserved(self):
        address = self.ea("draw_list")
        original = FakeType({"kind": "scalar", "name": "short"}, 2)
        self.adapter.annotations[address] = original
        self.adapter.user_annotations.add(address)
        result = bridge.full_run(True)
        self.assertEqual(self.annotations(result)["draw_list"], "user_type")
        self.assertEqual(self.adapter.annotations[address], original)
        self.assertNotIn(address, [ea for ea, _ in self.adapter.annotation_writes])

    def test_conflicting_named_dependency_blocks_dependent_signatures_and_globals(self):
        original = {"kind": "user", "size": 100}
        self.adapter.local["XFiles_Container"] = original
        result = bridge.full_run(True)
        statuses = self.annotations(result)
        self.assertEqual(statuses["draw_list"], "dependency_conflict")
        self.assertEqual(statuses["draw_slot"], "dependency_conflict")
        self.assertEqual(self.adapter.local["XFiles_Container"], original)
        self.assertTrue({self.ea("draw_list"), self.ea("draw_slot")}.isdisjoint(
            ea for ea, _ in self.adapter.annotation_writes))

    def test_missing_function_start_and_code_in_global_extent_are_not_annotated(self):
        self.names.not_functions.add(self.ea("draw_list"))
        self.names.code.add(self.ea("application") + 3)
        result = bridge.full_run(True)
        statuses = self.annotations(result)
        self.assertEqual(statuses["draw_list"], "not_function")
        self.assertEqual(statuses["application"], "not_data")
        self.assertTrue({self.ea("draw_list"), self.ea("application")}.isdisjoint(
            ea for ea, _ in self.adapter.annotation_writes))

    def mutate_after_type_apply(self, mutation):
        original = TYPE_BRIDGE.run

        def change(planned, apply=False):
            result = original(planned, apply)
            if apply:
                mutation()
            return result

        item = patch.object(TYPE_BRIDGE, "run", side_effect=change)
        item.start()
        self.addCleanup(item.stop)

    def test_rebase_after_planning_cannot_apply_types_at_old_addresses(self):
        self.mutate_after_type_apply(lambda: setattr(self.names, "base", self.names.base + 0x10000))
        result = bridge.full_run(True)
        self.assertTrue(all(status == "changed_since_plan" for status, _, _ in result["annotations"]))
        self.assertEqual(self.adapter.annotation_writes, [])

    def test_rebase_before_first_mutation_stops_every_stage(self):
        def rebase(profiles, build):
            result = self.original_plan(profiles, build, self.adapter, ROOT)
            self.names.base += 0x10000
            return result

        with patch.object(TYPE_BRIDGE, "plan", side_effect=rebase), self.assertRaises(ValueError):
            bridge.full_run(True)
        self.assertEqual(self.names.writes, [])
        self.assertEqual(self.adapter.writes, [])
        self.assertEqual(self.adapter.annotation_writes, [])
        self.assertTrue(all(session["closed"] for session in self.adapter.sessions))

    def test_other_known_hash_after_planning_cannot_receive_old_build_annotations(self):
        self.mutate_after_type_apply(lambda: setattr(self.names, "digest", DATA["builds"][1]["sha256"]))
        result = bridge.full_run(True)
        self.assertTrue(all(status == "changed_since_plan" for status, _, _ in result["annotations"]))
        self.assertEqual(self.adapter.annotation_writes, [])

    def test_function_and_data_validity_are_rechecked_after_type_import(self):
        function, data = self.ea("draw_list"), self.ea("application")

        def invalidate():
            self.names.not_functions.add(function)
            self.names.code.add(data + 3)

        self.mutate_after_type_apply(invalidate)
        result = bridge.full_run(True)
        statuses = self.annotations(result)
        self.assertEqual(statuses["draw_list"], "changed_since_plan")
        self.assertEqual(statuses["application"], "changed_since_plan")
        self.assertTrue({function, data}.isdisjoint(ea for ea, _ in self.adapter.annotation_writes))

    def test_known_hash_change_inside_name_planning_cannot_write_another_build(self):
        original = bridge._plan
        count = 0

        def change(*args, **kwargs):
            nonlocal count
            count += 1
            if count == 2:
                self.names.digest = DATA["builds"][1]["sha256"]
            return original(*args, **kwargs)

        with patch.object(bridge, "_plan", side_effect=change), self.assertRaises(ValueError):
            bridge.full_run(True)
        self.assertEqual(self.names.writes, [])
        self.assertEqual(self.adapter.writes, [])
        self.assertEqual(self.adapter.annotation_writes, [])


if __name__ == "__main__":
    unittest.main()
