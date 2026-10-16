import sys
from pathlib import Path
import tempfile
import unittest
import os
import signal
import subprocess
import time
from unittest.mock import Mock, patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_story.files import PreservedGame, set_ini
from native_story.driver import NativeGame


class PreservationTests(unittest.TestCase):
    def test_root_saves_and_game_database_are_also_restored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / 'game'
            game.mkdir()
            paths = [game / 'XFILES.GAM', game / 'legacy.x']
            for path in paths:
                path.write_bytes(b'original')
            backup = PreservedGame(game, root / 'results')
            for path in paths:
                path.write_bytes(b'changed')
            backup.restore()
            self.assertTrue(all(path.read_bytes() == b'original' for path in paths))

    def test_settings_and_generated_saves_survive_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'game'
            (game / 'saves').mkdir(parents=True)
            original = b'[Game]\r\nSkip Clips=0\r\n'
            (game / 'preferences.ini').write_bytes(original)
            (game / 'saves/original.x').write_bytes(b'original save')
            backup = PreservedGame(game, root / 'results')
            set_ini(game / 'preferences.ini', {'Game': {'Skip Clips': 1}})
            (game / 'saves/original.x').write_bytes(b'changed')
            (game / 'saves/QUICKSAVE.x').write_bytes(b'test save')
            self.assertTrue(backup.restore()['restored'])
            self.assertEqual((game / 'preferences.ini').read_bytes(), original)
            self.assertEqual((game / 'saves/original.x').read_bytes(), b'original save')
            self.assertFalse((game / 'saves/QUICKSAVE.x').exists())
            self.assertEqual((root / 'results/generated/saves/QUICKSAVE.x').read_bytes(), b'test save')

    def test_plain_preferences_never_gain_a_bom(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'preferences.ini'
            path.write_bytes(b'[Game]\nDescription="caf\xe9"\nSkip Clips=0\n')
            set_ini(path, {'Game': {'Skip Clips': 1}})
            self.assertTrue(path.read_bytes().startswith(b'[Game]'))
            self.assertIn(b'caf\xe9', path.read_bytes())
            self.assertIn(b'Skip Clips=1', path.read_bytes())

    def test_bom_in_legacy_test_preferences_is_removed(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'preferences.ini'
            path.write_bytes(b'\xef\xbb\xbf[Game]\nSkip Clips=0\n')
            set_ini(path, {'Game': {'Skip Clips': 1}})
            self.assertTrue(path.read_bytes().startswith(b'[Game]'))

    def test_corrupted_backup_does_not_replace_game_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'game'
            game.mkdir()
            path = game / 'patch.ini'
            path.write_bytes(b'original')
            backup = PreservedGame(game, root / 'results')
            (backup.originals / 'patch.ini').write_bytes(b'corrupted')
            path.write_bytes(b'current')
            with self.assertRaises(RuntimeError):
                backup.restore()
            self.assertEqual(path.read_bytes(), b'current')

    def test_all_backups_are_verified_before_any_restore_mutation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'game'
            (game / 'saves').mkdir(parents=True)
            for name in ('a.ini', 'b.ini'):
                (game / name).write_bytes(b'original')
            backup = PreservedGame(game, root / 'results')
            (backup.originals / 'b.ini').write_bytes(b'corrupt')
            (game / 'a.ini').write_bytes(b'current')
            (game / 'saves/new.x').write_bytes(b'new')
            with self.assertRaises(RuntimeError):
                backup.restore()
            self.assertEqual((game / 'a.ini').read_bytes(), b'current')
            self.assertTrue((game / 'saves/new.x').exists())

    def test_preservation_can_recover_after_interrupted_runner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'game'
            game.mkdir()
            (game / 'prefs.ini').write_bytes(b'original')
            PreservedGame(game, root / 'results')
            (game / 'prefs.ini').write_bytes(b'changed')
            restored = PreservedGame.recover(game, root / 'results')
            self.assertTrue(restored.restore()['restored'])
            self.assertEqual((game / 'prefs.ini').read_bytes(), b'original')
            other = root / 'other'
            other.mkdir()
            with self.assertRaises(ValueError):
                PreservedGame.recover(other, root / 'results')

    def test_utf16_preferences_are_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'preferences.ini'
            original = '[Game]\nSkip Clips=0\n'.encode('utf-16')
            path.write_bytes(original)
            with self.assertRaises(ValueError):
                set_ini(path, {'Game': {'Skip Clips': 1}})
            self.assertEqual(path.read_bytes(), original)

    def test_results_cannot_overlap_installation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / 'game'
            game.mkdir()
            with self.assertRaises(ValueError):
                PreservedGame(game, game / 'results')
            with self.assertRaises(ValueError):
                PreservedGame(game, root)


class DriverTests(unittest.TestCase):
    def test_wine_uses_native_display_wrapper_and_required_address_space(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            (prefix / 'system.reg').touch()
            with patch.dict(os.environ, {'DISPLAY': ':215', 'WINEDLLOVERRIDES': 'ddraw=b'}):
                game = NativeGame(prefix, prefix, prefix, 'wine', prefix)
            self.assertEqual(game.env['WINEDLLOVERRIDES'], 'ddraw=n,b')
            self.assertEqual(game.env['WINE_LARGE_ADDRESS_AWARE'], '0')
            self.assertEqual(game.env['PROTON_FORCE_LARGE_ADDRESS_AWARE'], '0')

    def game(self):
        game = NativeGame(Path('selected'), Path('tools'), Path('results'))
        game.pid = 456
        game.launcher = SimpleNamespace(poll=lambda: 0)
        game.click = Mock(side_effect=RuntimeError('UI unavailable'))
        game.event = Mock()
        return game

    def test_close_proves_native_child_exit_after_launcher_exited(self):
        game = self.game()
        game.command = Mock(side_effect=[SimpleNamespace(returncode=code, stderr='')
                                         for code in (0, 0, 3)])
        game.close()
        self.assertEqual([call.args[1] for call in game.command.call_args_list],
                         ['--status', '--close', '--status'])
        game.event.assert_called_with('native_process_closed', pid=456)

    def test_failed_native_exit_proof_blocks_cleanup(self):
        game = self.game()
        game.command = Mock(return_value=SimpleNamespace(returncode=1, stderr='wrong PID path'))
        with self.assertRaises(RuntimeError):
            game.close()
        self.assertEqual(game.command.call_count, 1)

    def test_unclaimed_child_is_checked_only_after_owned_launcher_stops(self):
        for child_status in (3, 0):
            with self.subTest(child_status=child_status):
                game = self.game()
                game.pid = None
                order = []
                launcher = Mock()
                launcher.poll.return_value = None
                launcher.terminate.side_effect = lambda: order.append('terminate')

                def stopped(timeout):
                    self.assertEqual(timeout, 10)
                    order.append('wait')
                    launcher.poll.return_value = 0

                def find(*arguments, **options):
                    self.assertEqual(arguments[1], '--find')
                    order.append('find')
                    return SimpleNamespace(returncode=child_status, stderr='')

                launcher.wait.side_effect = stopped
                game.launcher = launcher
                game.command = Mock(side_effect=find)
                if child_status == 3:
                    game.close()
                else:
                    with self.assertRaisesRegex(RuntimeError, 'unclaimed game may remain'):
                        game.close()
                self.assertEqual(order, ['terminate', 'wait', 'find'])
                launcher.kill.assert_not_called()

    def test_hidden_inventory_does_not_receive_physical_click(self):
        game = self.game()
        snapshot = dict(command_queue_count=0, input_address=0, script_controls=[], world_targets=[], inventory=[
            dict(id=9, visible=False, rect=[10, 10, 20, 20]),
            dict(id=9, visible=True, rect=[30, 30, 50, 50])])
        def wait(description, predicate):
            snapshot["input_address"] = 123
            self.assertFalse(predicate(snapshot))
            snapshot["input_address"] = 0
            snapshot["command_queue_count"] = 1
            self.assertFalse(predicate(snapshot))
            snapshot["command_queue_count"] = 0
            self.assertTrue(predicate(snapshot))
            return snapshot
        game.wait = wait
        game.click = Mock()
        game.target(9)
        game.click.assert_called_once_with(40, 40)

    def test_unavailable_state_has_bounded_diagnostics(self):
        game = self.game()
        game.command = Mock(return_value=SimpleNamespace(
            returncode=1, stderr='read failure', stdout='{"snapshot":{"status":"unavailable"}}'))
        self.assertIsNone(game.state())
        self.assertIsNone(game.state())
        self.assertEqual(game.event.call_count, 1)
        self.assertEqual(game.event.call_args.args[0], 'state_unavailable')

    def test_prefix_cleanup_never_stops_an_unowned_prefix(self):
        game = self.game()
        with patch('native_story.driver.subprocess.run') as run:
            game.finish_prefix()
        run.assert_not_called()

    def test_owned_prefix_cleanup_kills_and_waits_with_bounds(self):
        game = self.game()
        game.wine = 'wine'
        game.prefix_owned = True
        game.env['WINEPREFIX'] = 'private-test-prefix'
        with patch('native_story.driver.shutil.which', return_value='wineserver'), \
                patch('native_story.driver.subprocess.run') as run:
            game.finish_prefix()
        self.assertEqual([call.args[0][-1] for call in run.call_args_list], ['-k', '-w'])
        self.assertTrue(all(call.kwargs['timeout'] == 20 for call in run.call_args_list))
        self.assertTrue(all(call.kwargs['env']['WINEPREFIX'] == 'private-test-prefix'
                            for call in run.call_args_list))
        self.assertFalse(game.prefix_owned)

    def test_idle_wine_prefix_is_persisted_before_helper_preflight(self):
        game = self.game()
        game.wine = 'wine'
        game.env['WINEPREFIX'] = 'private-test-prefix'
        game.command = Mock()
        with patch('native_story.driver.Path.iterdir', return_value=iter([])), \
                patch.object(game, 'wine_server', return_value=Path('wineserver')), \
                patch('native_story.driver.subprocess.run') as run:
            game.preflight()
        self.assertTrue(game.prefix_owned)
        self.assertEqual(run.call_args.args[0], ['wineserver', '-p'])
        self.assertEqual(run.call_args.kwargs['timeout'], 30)
        self.assertTrue(all(call.kwargs['timeout'] == 120 for call in game.command.call_args_list))

    def test_command_preserves_output_and_checked_exit_status(self):
        game = NativeGame(Path('selected'), Path('tools'), Path('results'))
        code = 'import sys; print("helper output"); print("helper error", file=sys.stderr); sys.exit(7)'
        result = game.command(sys.executable, '-c', code, check=False)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout.strip(), 'helper output')
        self.assertEqual(result.stderr.strip(), 'helper error')
        with self.assertRaises(subprocess.CalledProcessError) as failure:
            game.command(sys.executable, '-c', code)
        self.assertEqual(failure.exception.returncode, 7)
        self.assertEqual(failure.exception.stdout.strip(), 'helper output')
        self.assertEqual(failure.exception.stderr.strip(), 'helper error')

    @unittest.skipUnless(os.name == 'posix', 'Inherited descriptor integration runs on Linux.')
    def test_command_returns_while_child_keeps_inherited_output_open(self):
        game = NativeGame(Path('selected'), Path('tools'), Path('results'))
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / 'child.pid'
            code = ('import subprocess,sys; from pathlib import Path; '
                    'child=subprocess.Popen([sys.executable,"-c","import time; time.sleep(5)"],'
                    'stdout=sys.stdout,stderr=sys.stderr); '
                    'Path(sys.argv[1]).write_text(str(child.pid)); print("parent exited",flush=True)')
            try:
                started = time.monotonic()
                result = game.command(sys.executable, '-c', code, pid_file, timeout=2)
                self.assertLess(time.monotonic() - started, 2)
                self.assertEqual(result.stdout.strip(), 'parent exited')
                os.kill(int(pid_file.read_text()), 0)
            finally:
                if pid_file.exists():
                    try:
                        os.kill(int(pid_file.read_text()), signal.SIGTERM)
                    except ProcessLookupError:
                        pass


if __name__ == '__main__':
    unittest.main()
