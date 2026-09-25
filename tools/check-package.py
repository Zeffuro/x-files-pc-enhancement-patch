#!/usr/bin/env python3
"""Check a CPack release ZIP before uploading it."""

import argparse
import hashlib
import pathlib
import re
import struct
import sys
import zipfile


class PackageError(ValueError):
    pass


ROOT = pathlib.Path(__file__).resolve().parents[1]
RUNTIME = {"XFilesPlay.exe", "XFilesSetup.exe", "QuickTime.qts", "ddraw.dll"}
REQUIRED = RUNTIME | {
    "LICENSE", "THIRD_PARTY.md", "README.md", "cnc-ddraw.LICENSE", "FFmpeg.LICENSE",
    "zlib.LICENSE", "defaults/ddraw.ini", "defaults/patch.ini", "docs/controls.md", "docs/building.md",
    "source/ffmpeg/build-ffmpeg.sh",
}
COMPONENTS = ("avcodec", "avutil", "swresample", "swscale")


def ffmpeg_pin(script: bytes) -> tuple[str, str]:
    text = script.decode("utf-8").replace("\r\n", "\n")
    version = re.search(r"^version=([0-9]+\.[0-9]+\.[0-9]+)$", text, re.MULTILINE)
    checksum = re.search(r"^checksum=([0-9a-f]{64})$", text, re.MULTILINE)
    if not version or not checksum:
        raise PackageError("Cannot read the FFmpeg version and checksum from the build script")
    return version[1], checksum[1]


def check_pe(header: bytes, name: str) -> None:
    if len(header) < 64 or header[:2] != b"MZ":
        raise PackageError(f"{name} is not a Windows binary")
    offset = struct.unpack_from("<I", header, 60)[0]
    if offset < 64 or offset + 26 > len(header) or header[offset:offset + 4] != b"PE\0\0":
        raise PackageError(f"{name} has an invalid PE header")
    machine = struct.unpack_from("<H", header, offset + 4)[0]
    magic = struct.unpack_from("<H", header, offset + 24)[0]
    if machine != 0x14C or magic != 0x10B:
        raise PackageError(f"{name} must be a 32-bit x86 PE binary")


def check_package(path: pathlib.Path, version: str, script_path: pathlib.Path) -> None:
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise PackageError("The package version must have the form 0.1.0")
    expected_name = f"xfiles-enhancement-{version}-windows-x86.zip"
    if path.name != expected_name:
        raise PackageError(f"Expected package name {expected_name}, got {path.name}")
    recipe = script_path.read_bytes()
    ffmpeg_version, source_hash = ffmpeg_pin(recipe)
    source_name = f"source/ffmpeg/ffmpeg-{ffmpeg_version}.tar.xz"
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(names) != len({name.casefold() for name in names}):
            raise PackageError("The package contains duplicate or case-colliding paths")
        for entry in entries:
            name = entry.filename
            parts = pathlib.PurePosixPath(name).parts
            if (not name or name.startswith("/") or "\\" in name or ":" in name
                    or any(part in (".", "..") for part in name.rstrip("/").split("/"))
                    or not parts or "//" in name):
                raise PackageError(f"Unsafe package path: {name}")
            if entry.flag_bits & 1 or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise PackageError(f"Encrypted or symlink entry: {name}")
        files = {entry.filename for entry in entries if not entry.is_dir()}
        required = REQUIRED | {source_name}
        runtime = set(RUNTIME)
        for component in COMPONENTS:
            matches = {name for name in files if re.fullmatch(rf"{component}-[0-9]+\.dll", name)}
            if len(matches) != 1:
                raise PackageError(f"Expected exactly one {component} DLL")
            required |= matches
            runtime |= matches
        missing, extra = required - files, files - required
        if missing or extra:
            raise PackageError(f"Missing files: {sorted(missing)}; unexpected files: {sorted(extra)}")
        allowed_dirs = {str(parent) + "/" for name in required
                        for parent in pathlib.PurePosixPath(name).parents if str(parent) != "."}
        if any(entry.is_dir() and entry.filename not in allowed_dirs for entry in entries):
            raise PackageError("The package contains an unexpected directory")
        if any(not entry.is_dir() and not 0 < entry.file_size <= 256 * 1024 * 1024 for entry in entries):
            raise PackageError("A package file is empty or unexpectedly large")
        if sum(entry.file_size for entry in entries) > 512 * 1024 * 1024:
            raise PackageError("The patch package is unexpectedly large")
        damaged = archive.testzip()
        if damaged:
            raise PackageError(f"CRC check failed: {damaged}")
        for name in runtime:
            with archive.open(name) as binary:
                check_pe(binary.read(1024 * 1024), name)
        # Accept checkout line-ending conversion, but not a different build recipe.
        bundled = archive.read("source/ffmpeg/build-ffmpeg.sh")
        if bundled.replace(b"\r\n", b"\n") != recipe.replace(b"\r\n", b"\n"):
            raise PackageError("The bundled FFmpeg build script differs from this checkout")
        with archive.open(source_name) as source:
            digest = hashlib.sha256()
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
            actual_hash = digest.hexdigest()
        if actual_hash != source_hash:
            raise PackageError("The FFmpeg source archive does not match its pinned checksum")
        for name in ("ddraw.ini", "patch.ini"):
            expected = (ROOT / "config" / name).read_bytes().replace(b"\r\n", b"\n")
            if archive.read("defaults/" + name).replace(b"\r\n", b"\n") != expected:
                raise PackageError(f"The bundled defaults/{name} differs from this checkout")
        for notice in ("LICENSE", "THIRD_PARTY.md"):
            expected = (ROOT / notice).read_bytes().replace(b"\r\n", b"\n")
            if archive.read(notice).replace(b"\r\n", b"\n") != expected:
                raise PackageError(f"The bundled {notice} differs from this checkout")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=pathlib.Path)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        check_package(args.archive, args.version, ROOT / "tools/build-ffmpeg.sh")
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
        print(f"Package check failed: {error}", file=sys.stderr)
        return 1
    print(f"Checked {args.archive.name}: files, x86 headers, notices, FFmpeg source and CRCs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
