"""Verify catalog argument labels without decompilation or changing custom names."""

import copy
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from game_ida import bridge, DATA
from game_ida_full import AnnotationAdapter, FakeType, TYPE_BRIDGE
from game_ida_research import ResearchIda


ROOT = Path(__file__).resolve().parents[1]
CATALOG = json.loads((ROOT / "src/game/functions/catalog.json").read_text(encoding="utf-8"))
FUNCTION = {"kind": "function", "return": {"kind": "scalar", "name": "int"},
            "params": [{"kind": "pointer", "to": {"kind": "scalar", "name": "void"}}] * 3,
            "convention": "__stdcall"}


class IdaParameterTest(unittest.TestCase):
    def setUp(self):
        self.build = DATA["builds"][0]
        self.names = ResearchIda(self.build)
        self.adapter = AnnotationAdapter(self.build, self.names)
        self.address = self.names.base + self.build["rvas"]["draw_list"]
        self.desired = tuple(name for entry in CATALOG["functions"] if entry["field"] == "draw_list"
                             for _, name in entry["params"])

    def test_generic_names_merge_without_mutating_custom_names_or_metadata(self):
        current = FakeType(FUNCTION)
        for argument, name in zip(current.arguments, ("a1", "custom_queue", "arg_8")):
            argument.name = name
            argument.cmt = "Existing parameter comment"
        current.metadata = {"flags": 0x100, "comment": "Existing function comment"}
        before = copy.deepcopy(current)
        changed, names = bridge._named_parameters(self.adapter, current, self.desired)
        self.assertEqual(names, ("object", "custom_queue", "clip"))
        self.assertEqual([arg.name for arg in current.arguments], [arg.name for arg in before.arguments])
        self.assertEqual(changed.metadata, before.metadata)
        self.assertEqual([(arg.flags, arg.argloc, arg.cmt, arg.type) for arg in changed.arguments],
                         [(arg.flags, arg.argloc, arg.cmt, arg.type) for arg in before.arguments])

    def test_generic_match_is_anchored_and_this_and_custom_names_are_preserved(self):
        names = ("", "arg0", "arg_0", "arg_C", "a1", "this", "user_value", "a0", "arg_label")
        node = dict(FUNCTION, params=FUNCTION["params"] * 3)
        current = FakeType(node)
        for arg, name in zip(current.arguments, names):
            arg.name = name
        expected = tuple("verified_" + str(i) for i in range(len(names)))
        _, actual = bridge._named_parameters(self.adapter, current, expected)
        self.assertEqual(actual[:5], expected[:5])
        self.assertEqual(actual[5:], names[5:])

    def test_parameter_count_mismatch_cannot_construct_a_signature(self):
        with self.assertRaisesRegex(ValueError, "parameters differ"):
            bridge._named_parameters(self.adapter, FakeType(FUNCTION), self.desired[:2])

    def test_custom_argument_name_collision_does_not_rename_or_duplicate_it(self):
        current = FakeType(FUNCTION)
        current.arguments[1].name = "object"
        _, actual = bridge._named_parameters(self.adapter, current, self.desired)
        self.assertEqual(actual, ("arg0", "object", "clip"))

    def test_sdk_consuming_argument_vector_does_not_hide_name_changes(self):
        class ConsumingType(FakeType):
            def create_func(self, detail):
                result = super().create_func(detail)
                detail.clear()
                return result

        self.adapter.types.tinfo_t = ConsumingType
        value, names = bridge._named_parameters(self.adapter, FakeType(FUNCTION), self.desired)
        self.assertEqual(names, self.desired)
        self.assertEqual(tuple(arg.name for arg in value.arguments), self.desired)

    def planned(self, argument_names):
        manifest = json.loads((ROOT / "src/game/ida/types.json").read_text(encoding="utf-8"))
        plan = SimpleNamespace(adapter=self.adapter, root=ROOT, manifest=manifest,
                               build=self.build, types={}, closure=lambda _: set())
        decisions = [("parameters", self.address, "draw_list", FUNCTION, self.desired, argument_names)]
        return plan, decisions

    def test_name_only_update_for_existing_user_typed_equal_abi_then_repeat(self):
        current = FakeType(FUNCTION)
        current.arguments[1].name = "custom_queue"
        self.adapter.annotations[self.address] = current
        self.adapter.user_annotations.add(self.address)
        names = bridge._argument_names(self.adapter, current)
        planned, decisions = self.planned(names)
        with patch.object(bridge, "_types", return_value=TYPE_BRIDGE):
            result = bridge._apply_annotations(decisions, planned, self.names, True, self.names.base)
            self.assertEqual(result, [("applied", self.address, "draw_list")])
            stored = self.adapter.annotations[self.address]
            self.assertEqual(bridge._argument_names(self.adapter, stored), ("object", "custom_queue", "clip"))
            planned, decisions = self.planned(bridge._argument_names(self.adapter, stored))
            repeated = bridge._apply_annotations(decisions, planned, self.names, True, self.names.base)
        self.assertEqual(repeated, [("already", self.address, "draw_list")])
        self.assertEqual(len(self.adapter.annotation_writes), 1)

    def test_name_or_abi_changed_after_plan_cannot_be_overwritten(self):
        for change in ("name", "type", "convention", "count"):
            current = FakeType(FUNCTION)
            self.adapter.annotations[self.address] = current
            planned, decisions = self.planned(bridge._argument_names(self.adapter, current))
            if change == "name":
                current.arguments[1].name = "NewCustomName"
            elif change == "type":
                current.node["return"] = {"kind": "scalar", "name": "float"}
            elif change == "convention":
                current.node["convention"] = "__cdecl"
            else:
                current.arguments.pop()
                current.node["params"].pop()
            with patch.object(bridge, "_types", return_value=TYPE_BRIDGE):
                result = bridge._apply_annotations(decisions, planned, self.names, True, self.names.base)
            self.assertEqual(result, [("changed_since_plan", self.address, "draw_list")])
        self.assertEqual(self.adapter.annotation_writes, [])


if __name__ == "__main__":
    unittest.main()
