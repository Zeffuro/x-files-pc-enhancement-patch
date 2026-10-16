import sys
from pathlib import Path
import tempfile
import unittest
import json
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_story import scenarios
from native_story import __main__ as runner
from native_story.files import digest


class SaveObservationTests(unittest.TestCase):
    def test_loading_cache_does_not_require_a_story_flag_yet(self):
        actor = Mock()
        actor.variable.side_effect = RuntimeError('State variable is not cached yet')
        state = {'scene_active': 1, 'command_queue_count': 0, 'input_address': 0,
                 'world_targets': [], 'variables': []}
        self.assertFalse(scenarios.office_inventory(actor, state, True))
        actor.variable.assert_not_called()

    def test_playable_office_still_requires_the_story_flag(self):
        actor = Mock()
        actor.variable.side_effect = RuntimeError('State variable is not cached yet')
        state = {'scene_active': 1, 'command_queue_count': 0, 'input_address': 0,
                 'world_targets': [{'id': 6980}], 'variables': []}
        with self.assertRaises(RuntimeError):
            scenarios.office_inventory(actor, state, True)

    def test_existing_matching_save_cannot_count_as_new_save(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'QUICKSAVE.x'
            path.write_bytes(b'already present')
            previous = scenarios.file_generation(path)
            with patch.object(scenarios, 'saved_state') as read, \
                    patch.object(scenarios.time, 'monotonic', side_effect=[0, 0, 2]), \
                    patch.object(scenarios.time, 'sleep'):
                with self.assertRaises(TimeoutError):
                    scenarios.wait_for_save(Mock(), path, previous, lambda state: True, timeout=1)
            read.assert_not_called()

    def test_new_generation_must_also_have_the_expected_story_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'QUICKSAVE.x'
            path.write_bytes(b'new generation')
            states = [{'variables': {'3969': 0}}, {'variables': {'3969': 1}}]
            with patch.object(scenarios, 'saved_state', side_effect=states) as read, \
                    patch.object(scenarios.time, 'sleep'):
                state = scenarios.wait_for_save(Mock(), path, None,
                                                lambda state: state['variables']['3969'] == 1)
            self.assertEqual(state['variables']['3969'], 1)
            self.assertEqual(read.call_count, 2)

    def test_busy_scene_blocks_world_target_readiness(self):
        state = {'world_targets': [{'id': 6980}], 'modal': {'text': 'Try again?'},
                 'command_queue_count': 0, 'input_address': 0, 'scene_active': 1}
        self.assertFalse(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))
        state['modal'] = None
        state['scene_active'] = 0
        self.assertFalse(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))
        state['scene_active'] = 1
        self.assertTrue(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))
        state['command_queue_count'] = 1
        self.assertFalse(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))
        state['command_queue_count'] = 0
        state['input_address'] = 123
        self.assertFalse(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))
        state['input_address'] = 0
        state['movie_active'] = True
        self.assertFalse(scenarios.has_target(state, scenarios.YardTarget.FACE_WORKBENCH))


class RunnerOutcomeTests(unittest.TestCase):
    def test_separate_story_checkpoint_is_loaded_and_used_for_rollback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game, output = root / 'game', root / 'results'
            game.mkdir()
            (game / 'XFILES.GAM').write_bytes(b'original')
            fixture, story = root / 'bomb.x', root / 'story.x'
            fixture.write_bytes(b'original bomb timer')
            story.write_bytes(b'longer story timer')
            args = SimpleNamespace(game=game, tools=game, output=output, fixture=fixture,
                                   fixture_sha256=digest(fixture), story_fixture=story,
                                   story_fixture_sha256=digest(story), edition='DVD', retries=2,
                                   skip_clips=False, wine=None, prefix=None)
            order = []
            def key(value):
                order.append(('load', (game / 'saves/QUICKSAVE.x').read_bytes()))
            def wait(description, predicate):
                order.append(('wait', description))
                return {'edition': 'DVD', 'profile': 'test'}
            actor = SimpleNamespace(game=game, output=output, preflight=Mock(), start=Mock(),
                                    wait=wait, key=key, click=Mock(), event=Mock(),
                                    close=Mock(), finish_prefix=Mock())
            metadata = {'view': 6977, 'variables': {'1939': 1, '1992': 0, '3969': 0}}
            with patch.object(runner.argparse.ArgumentParser, 'parse_args', return_value=args), \
                    patch.object(runner, 'NativeGame', return_value=actor), \
                    patch.object(runner, 'saved_state', return_value=metadata), \
                    patch.object(runner, 'bomb_retry'), patch.object(runner, 'cutters'), \
                    patch.object(runner, 'story_roundtrip') as roundtrip, patch('builtins.print'):
                runner.main()
            self.assertLess(order.index(('wait', 'original main menu before the story checkpoint')),
                            order.index(('load', story.read_bytes())))
            self.assertEqual([data for name, data in order if name == 'load'],
                             [fixture.read_bytes(), story.read_bytes()])
            self.assertEqual(roundtrip.call_args.args[1].read_bytes(), story.read_bytes())
            report = json.loads((output / 'summary.json').read_text())
            self.assertEqual(report['status'], 'passed')
            self.assertEqual(report['story_fixture_sha256'], digest(story))

    def test_interruption_restores_files_and_cannot_report_success(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game, output = root / 'game', root / 'results'
            game.mkdir()
            (game / 'XFILES.GAM').write_bytes(b'original')
            fixture = root / 'fixture.x'
            fixture.write_bytes(b'private checkpoint')
            args = SimpleNamespace(game=game, tools=game, output=output, fixture=fixture,
                                   fixture_sha256=digest(fixture), edition='DVD', retries=1,
                                   story_fixture=None, story_fixture_sha256=None,
                                   skip_clips=False, wine=None, prefix=None)
            actor = SimpleNamespace(game=game, output=output, preflight=Mock(), start=Mock(),
                                    wait=Mock(return_value={'edition': 'DVD', 'profile': 'test'}),
                                    key=Mock(), close=Mock(), finish_prefix=Mock())
            metadata = {'view': 6977, 'variables': {'1939': 1, '1992': 0, '3969': 0}}
            with patch.object(runner.argparse.ArgumentParser, 'parse_args', return_value=args), \
                    patch.object(runner, 'NativeGame', return_value=actor), \
                    patch.object(runner, 'saved_state', return_value=metadata), \
                    patch.object(runner, 'bomb_retry', side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    runner.main()
            report = json.loads((output / 'summary.json').read_text())
            self.assertEqual(report['status'], 'failed')
            self.assertEqual(report['error'], 'KeyboardInterrupt')
            self.assertTrue(report['preservation']['restored'])
            actor.close.assert_called_once()
            actor.finish_prefix.assert_called_once()
            self.assertEqual((game / 'XFILES.GAM').read_bytes(), b'original')


if __name__ == '__main__':
    unittest.main()
