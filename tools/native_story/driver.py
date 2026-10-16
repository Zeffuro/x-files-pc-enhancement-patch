"""Drive the original game through an owned, hash-checked native helper."""

import json
import os
from pathlib import Path
import subprocess
import time
import re
import shutil
import locale
import tempfile


class NativeGame:
    def __init__(self, game, tools, output, wine=None, prefix=None, skip=False):
        self.game, self.tools, self.output = map(Path, (game, tools, output))
        self.wine = wine
        self.env = os.environ.copy()
        self.env['XFILES_TEST_MUTE_AUDIO'] = '1'
        if wine:
            if not prefix or not (Path(prefix) / 'system.reg').is_file():
                raise ValueError('An initialized, idle Wine prefix is required.')
            self.env.update(WINEPREFIX=str(Path(prefix).resolve()), WINEDEBUG='-all',
                            WINEDLLOVERRIDES='ddraw=n,b', WINE_LARGE_ADDRESS_AWARE='0',
                            PROTON_FORCE_LARGE_ADDRESS_AWARE='0')
            for key in ('WAYLAND_DISPLAY', 'WINEDLLPATH', 'WINEARCH'):
                self.env.pop(key, None)
            if not re.fullmatch(r':[1-9][0-9]{2,}(?:\.0)?', self.env.get('DISPLAY', '')):
                raise ValueError('Run native Wine scenarios inside a private Xvfb display.')
        self.skip = skip
        self.pid = None
        self.launcher = None
        self.events = self.output / 'events.jsonl'
        self.skipped_input = None
        self.last_snapshot = None
        self.held_keys = set()
        self.last_unavailable = None
        self.last_unavailable_time = 0
        self.prefix_owned = False

    def windows_path(self, path):
        path = Path(path).resolve()
        return 'Z:' + str(path).replace('/', '\\') if self.wine else str(path)

    def command(self, executable, *arguments, check=True, timeout=15):
        command = ([self.wine] if self.wine else []) + [self.windows_path(executable)]
        command += list(map(str, arguments))
        with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
            def text(stream):
                stream.seek(0)
                return stream.read().decode(locale.getpreferredencoding(False), errors='replace')
            try:
                result = subprocess.run(command, env=self.env, stdout=output, stderr=errors,
                                        timeout=timeout, check=False)
            except subprocess.TimeoutExpired as error:
                error.output, error.stderr = text(output), text(errors)
                raise
            completed = subprocess.CompletedProcess(command, result.returncode,
                                                     text(output), text(errors))
        if check:
            completed.check_returncode()
        return completed

    def preflight(self):
        if self.wine:
            prefix = ('WINEPREFIX=' + self.env['WINEPREFIX']).encode()
            for process in Path('/proc').iterdir():
                if not process.name.isdigit():
                    continue
                try:
                    environment = (process / 'environ').read_bytes().split(b'\0')
                    command = (process / 'cmdline').read_bytes().lower()
                except (PermissionError, FileNotFoundError, ProcessLookupError):
                    continue
                if prefix in environment and b'.exe' in command:
                    raise RuntimeError('Close existing applications in the selected Wine prefix first.')
            self.prefix_owned = True
            subprocess.run([str(self.wine_server()), '-p'], env=self.env, timeout=30, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.command(self.tools / 'xfiles-native-input.exe', '--idle',
                     self.windows_path(self.game), timeout=120)
        self.command(self.tools / 'xfiles-probe.exe', 'check',
                     self.windows_path(self.game), timeout=120)

    def wine_server(self):
        wine = Path(shutil.which(self.wine) or self.wine).resolve()
        server = wine.with_name('wineserver')
        if not server.is_file():
            server = shutil.which('wineserver')
        if not server:
            raise RuntimeError('Cannot find wineserver to close the owned test prefix.')
        return server

    def finish_prefix(self):
        if not self.prefix_owned:
            return
        for option in ('-k', '-w'):
            subprocess.run([str(self.wine_server()), option], env=self.env, timeout=20, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.prefix_owned = False

    def event(self, name, **values):
        with self.events.open('a', encoding='utf-8') as output:
            output.write(json.dumps({'time': time.time(), 'event': name, **values}) + '\n')

    def start(self):
        running = self.command(self.tools / 'xfiles-native-input.exe', '--find',
                               self.windows_path(self.game), check=False, timeout=60)
        if running.returncode == 0:
            raise RuntimeError('Close the selected test game before starting a regression.')
        if running.returncode != 3:
            raise RuntimeError('Cannot establish that the selected test game is absent: ' + running.stderr)
        log = (self.output / 'launcher.log').open('wb')
        command = ([self.wine] if self.wine else []) + [self.windows_path(self.game / 'XFilesPlay.exe')]
        try:
            self.launcher = subprocess.Popen(command, cwd=self.game, env=self.env,
                                             stdout=log, stderr=subprocess.STDOUT)
        finally:
            log.close()
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            result = self.command(self.tools / 'xfiles-native-input.exe', '--find',
                                  self.windows_path(self.game), check=False, timeout=60)
            if result.returncode == 0:
                self.pid = int(result.stdout.strip())
                self.event('launched', pid=self.pid)
                return
            if result.returncode != 3:
                raise RuntimeError('Cannot identify the newly launched game: ' + result.stderr)
            if self.launcher.poll() is not None:
                raise RuntimeError('The test launcher exited before the game opened.')
            time.sleep(.2)
        raise TimeoutError('The original game did not open.')

    def state(self):
        result = self.command(self.tools / 'xfiles-native-state.exe', self.pid,
                              self.windows_path(self.game), check=False)
        try:
            envelope = json.loads(result.stdout)
        except ValueError as error:
            raise RuntimeError('The native state helper did not return JSON: ' + result.stderr) from error
        snapshot = envelope.get('snapshot', {})
        if result.returncode or snapshot.get('status') != 'ok':
            detail = snapshot.get('reason', envelope.get('reason'))
            reason = (result.returncode, snapshot.get('status'), detail, result.stderr[-1024:])
            now = time.monotonic()
            if reason != self.last_unavailable or now - self.last_unavailable_time >= 5:
                self.event('state_unavailable', returncode=result.returncode,
                           status=snapshot.get('status'), reason=detail, stderr=result.stderr[-1024:])
                self.last_unavailable, self.last_unavailable_time = reason, now
            return None
        if envelope.get('pid') != self.pid:
            raise RuntimeError('Native snapshot PID changed.')
        self.last_snapshot = snapshot
        return snapshot

    def input(self, *arguments):
        self.command(self.tools / 'xfiles-native-input.exe', self.pid,
                     self.windows_path(self.game), *arguments)

    def click(self, x, y, right=False):
        self.input('click', x, y, *(['right'] if right else []))
        self.event('click', x=x, y=y, right=right)

    def key(self, key, mode='tap'):
        self.input('key', key, mode)
        if mode == 'down':
            self.held_keys.add(key)
        elif mode == 'up':
            self.held_keys.discard(key)
        self.event('key', key=key, mode=mode)

    @staticmethod
    def variable(snapshot, identity):
        matches = [item for item in snapshot['variables'] if item['id'] == identity
                   and item['state_database']]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one cached state variable {identity}, found {len(matches)}.')
        return matches[0]['raw_value']

    def accelerate(self, snapshot):
        identity = snapshot.get('input_address')
        if self.skip and snapshot.get('skip_available') is True and identity != self.skipped_input:
            self.click(320, 210, right=True)
            self.skipped_input = identity
            self.event('native_movie_skip', input=identity)
        elif snapshot.get('skip_available') is False:
            self.skipped_input = None

    def wait(self, description, predicate, timeout=180, accelerate=True):
        deadline = time.monotonic() + timeout
        previous_state = None
        while time.monotonic() < deadline:
            if self.launcher.poll() is not None:
                raise RuntimeError('The original game exited while waiting for ' + description)
            snapshot = self.state()
            if snapshot is not None:
                progress = {key: snapshot.get(key) for key in
                            ('command_queue_count', 'input_address', 'input_vtable_rva',
                             'scene_active', 'movie_active', 'skip_available')}
                progress['world_targets'] = [item['id'] for item in snapshot.get('world_targets', [])]
                progress['modal'] = (snapshot.get('modal') or {}).get('text')
                if progress != previous_state:
                    self.event('waiting', description=description, **progress)
                    previous_state = progress
                if predicate(snapshot):
                    self.event('assertion', description=description,
                               queue=snapshot['command_queue_count'], profile=snapshot['profile'],
                               input_address=snapshot.get('input_address'),
                               scene_active=snapshot.get('scene_active'))
                    return snapshot
                if accelerate:
                    self.accelerate(snapshot)
            time.sleep(.15)
        self.event('timeout', description=description, last_snapshot=self.last_snapshot)
        raise TimeoutError(description)

    def target(self, identity):
        def controls(snapshot):
            if (snapshot.get('modal') or snapshot.get('main_menu') or
                    snapshot.get('movie_active') or snapshot.get('pending_load') or
                    snapshot.get('command_queue_count') != 0 or
                    snapshot.get('input_address') != 0):
                return []
            return (snapshot['script_controls'] + snapshot['world_targets'] +
                    [item for item in snapshot.get('inventory', []) if item.get('visible') is True])

        def available(snapshot):
            return any(item['id'] == identity for item in controls(snapshot))
        snapshot = self.wait(f'click target {identity}', available)
        rows = controls(snapshot)
        targets = [item for item in rows if item['id'] == identity]
        if len(targets) != 1:
            raise RuntimeError(f'Ambiguous native target {identity}.')
        left, top, right, bottom = targets[0]['rect']
        if not (0 <= left < right <= 640 and 0 <= top < bottom <= 480):
            raise RuntimeError('Native target has invalid screen bounds.')
        self.click((left + right) // 2, (top + bottom) // 2)

    def close(self):
        if not self.launcher:
            return
        if self.pid:
            try:
                for key in sorted(self.held_keys):
                    self.key(key, 'up')
                self.click(620, 20)
                time.sleep(.4)
                self.click(590, 410)
                time.sleep(.3)
                self.click(365, 278)
            except (RuntimeError, subprocess.SubprocessError):
                pass
            status = self.command(self.tools / 'xfiles-native-input.exe', '--status', self.pid,
                                  self.windows_path(self.game), check=False)
            if status.returncode == 0:
                closed = self.command(self.tools / 'xfiles-native-input.exe', '--close', self.pid,
                                      self.windows_path(self.game), check=False)
                if closed.returncode not in (0, 3):
                    raise RuntimeError('Owned native game closure failed. Do not restore files: ' + closed.stderr)
            elif status.returncode != 3:
                raise RuntimeError('Cannot verify the owned native game before restoration: ' + status.stderr)
            status = self.command(self.tools / 'xfiles-native-input.exe', '--status', self.pid,
                                  self.windows_path(self.game), check=False)
            if status.returncode != 3:
                raise RuntimeError('The owned native game is not proven closed. Do not restore files.')
            self.event('native_process_closed', pid=self.pid)
        else:
            if self.launcher.poll() is None:
                self.launcher.terminate()
                try:
                    self.launcher.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    self.launcher.kill()
                    self.launcher.wait(timeout=5)
            found = self.command(self.tools / 'xfiles-native-input.exe', '--find',
                                 self.windows_path(self.game), check=False)
            if found.returncode != 3:
                raise RuntimeError('An unclaimed game may remain. Do not restore files.')
        if self.launcher.poll() is None:
            self.launcher.terminate()
            try:
                self.launcher.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.launcher.kill()
                self.launcher.wait(timeout=5)
