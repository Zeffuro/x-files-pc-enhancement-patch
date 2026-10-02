#!/usr/bin/env python3
"""Prepare the standalone tools ZIP and checksums from the checked patch ZIP."""

import argparse
import hashlib
import importlib.util
import pathlib
import sys
import tempfile
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("check_package", ROOT / "tools/check-package.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def prepare_packages(archive: pathlib.Path, version: str,
                     recipe: pathlib.Path = ROOT / "tools/build-ffmpeg.sh") -> list[pathlib.Path]:
    checker.check_package(archive, version, recipe)
    target = archive.with_name(f"xfiles-devtools-{version}-windows-x86.zip")
    sources = {name: name for name in checker.DEVTOOLS_REQUIRED}
    sources.update({"README.md": "docs/standalone-devtools.md",
                    "THIRD_PARTY.md": "docs/devtools-notices.md"})
    with zipfile.ZipFile(archive) as patch:
        ffmpeg_version, _ = checker.ffmpeg_pin(recipe.read_bytes())
        source = f"source/ffmpeg/ffmpeg-{ffmpeg_version}.tar.xz"
        sources[source] = source
        for component in checker.COMPONENTS:
            name = next(name for name in patch.namelist()
                        if name.startswith(component + "-") and name.endswith(".dll"))
            sources[name] = name
        with tempfile.TemporaryDirectory(prefix="xfiles-package-", dir=archive.parent) as folder:
            staged = pathlib.Path(folder) / target.name
            with zipfile.ZipFile(staged, "w", zipfile.ZIP_DEFLATED) as tools:
                for destination, source in sorted(sources.items()):
                    original = patch.getinfo(source)
                    entry = zipfile.ZipInfo(destination, original.date_time)
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry.external_attr = original.external_attr
                    tools.writestr(entry, patch.read(source))
            checker.check_package(staged, version, recipe, "devtools")
            staged.replace(target)
    packages = [archive, target]
    checksums = []
    for package in packages:
        with package.open("rb") as binary:
            digest = hashlib.sha256()
            for chunk in iter(lambda: binary.read(1024 * 1024), b""):
                digest.update(chunk)
        line = f"{digest.hexdigest()}  {package.name}\n"
        package.with_suffix(".zip.sha256").write_text(line, encoding="ascii")
        checksums.append(line)
    (archive.parent / "SHA256SUMS.txt").write_text("".join(checksums), encoding="ascii")
    return packages


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        packages = prepare_packages(args.archive, args.version)
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print(f"Release packaging failed: {error}", file=sys.stderr)
        return 1
    for package in packages:
        print(f"Checked release package: {package}")
    print(f"Checksums: {args.archive.parent / 'SHA256SUMS.txt'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
