"""Preserve an existing test installation while native scenarios run."""

import hashlib
import json
from pathlib import Path
import shutil


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def mutable_files(game):
    files = {p for p in game.iterdir() if p.is_file() and
             p.suffix.lower() in ('.ini', '.for', '.gam', '.x')}
    if (game / 'saves').is_dir():
        files.update(p for p in (game / 'saves').rglob('*') if p.is_file())
    return {p.relative_to(game): p for p in files}


class PreservedGame:
    def __init__(self, game, output):
        self.game = Path(game).resolve(strict=True)
        self.output = Path(output).resolve()
        if self.output.is_relative_to(self.game) or self.game.is_relative_to(self.output):
            raise ValueError('Results must be outside the game installation.')
        self.output.mkdir(parents=True, exist_ok=False)
        self.originals = self.output / 'originals'
        self.originals.mkdir()
        self.before = {}
        for relative, path in mutable_files(self.game).items():
            if not path.resolve().is_relative_to(self.game):
                raise ValueError('Test files must stay inside the game installation.')
            self.before[relative] = digest(path)
            backup = self.originals / relative
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, backup)
            if digest(backup) != self.before[relative] or digest(path) != self.before[relative]:
                raise RuntimeError('A test file changed during preservation. No settings were changed.')
        (self.output / 'preservation.json').write_text(json.dumps(
            {'game': str(self.game), 'files': {str(k): v for k, v in self.before.items()}},
            indent=2), encoding='utf-8')

    @classmethod
    def recover(cls, game, output):
        saved = cls.__new__(cls)
        saved.game = Path(game).resolve(strict=True)
        saved.output = Path(output).resolve(strict=True)
        if saved.output.is_relative_to(saved.game) or saved.game.is_relative_to(saved.output):
            raise ValueError('Recovery results must be outside the game installation.')
        manifest = json.loads((saved.output / 'preservation.json').read_text(encoding='utf-8'))
        if Path(manifest['game']).resolve(strict=True) != saved.game:
            raise ValueError('The preservation manifest belongs to another installation.')
        saved.originals = saved.output / 'originals'
        saved.before = {Path(key): value for key, value in manifest['files'].items()}
        saved.validate_backups()
        return saved

    def validate_backups(self):
        if not self.originals.resolve().is_relative_to(self.output):
            raise ValueError('Unsafe original backup directory.')
        for relative, expected in self.before.items():
            if relative.is_absolute() or '..' in relative.parts or not relative.parts:
                raise ValueError('Unsafe preserved file name.')
            backup = self.originals / relative
            if (not backup.resolve().is_relative_to(self.originals.resolve()) or
                    not (self.game / relative).resolve().is_relative_to(self.game)):
                raise ValueError('Unsafe preserved file path.')
            if digest(backup) != expected:
                raise RuntimeError('A preserved original changed. Restore was stopped before mutation.')

    def restore(self):
        self.validate_backups()
        generated = []
        for relative, path in mutable_files(self.game).items():
            if relative not in self.before:
                destination = self.output / 'generated' / relative
                if (not path.resolve().is_relative_to(self.game) or
                        not destination.resolve().is_relative_to(self.output) or destination.exists()):
                    raise ValueError('Unsafe generated-file preservation path.')
                generated.append((path, destination))
        for path, destination in generated:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(path), str(destination))
        for relative in self.before:
            destination = self.game / relative
            if not destination.resolve().is_relative_to(self.game):
                raise ValueError('Unsafe configuration restore path.')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(self.originals / relative, destination)
        current = mutable_files(self.game)
        if set(current) != set(self.before) or any(
                digest(current[name]) != expected for name, expected in self.before.items()):
            raise RuntimeError('Test saves or settings were not restored exactly.')
        return {'protected_files': len(self.before), 'restored': True}


def set_ini(path, changes):
    import configparser
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    parser.optionxform = str
    raw = path.read_bytes() if path.exists() else b''
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')):
        raise ValueError('UTF-16 preferences are unsupported by the native regression writer.')
    encoding = 'utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'latin1'
    if raw:
        parser.read_string(raw.decode(encoding))
    for section, values in changes.items():
        if not parser.has_section(section):
            parser.add_section(section)
        for name, value in values.items():
            parser.set(section, name, str(value))
    with path.open('w', encoding='utf-8' if encoding == 'utf-8-sig' else encoding) as output:
        parser.write(output, space_around_delimiters=False)
