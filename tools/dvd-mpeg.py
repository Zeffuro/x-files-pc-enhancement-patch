#!/usr/bin/env python3
"""Enable or disable experimental DVD playback in an isolated installation."""

import argparse
import base64
import configparser
import hashlib
import io
import json
import os
import pathlib
import tempfile

DVD_HASH = "1c7385b15bc11f6ff46b5a30ca383441df96be5be1c1dec31eae650f2243a2ad"
STATE = ".xfiles-mpeg-registration.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace(path, data):
    handle, name = tempfile.mkstemp(prefix=".dvd-mpeg-", dir=path.parent)
    try:
        with os.fdopen(handle, "wb") as output:
            output.write(data)
        os.replace(name, path)
    finally:
        pathlib.Path(name).unlink(missing_ok=True)


def register(directory, disable=False):
    root = pathlib.Path(directory).resolve(strict=True)
    ini = root / "dlmpg.ini"
    journal = root / STATE
    if ini.is_symlink() or journal.is_symlink():
        raise ValueError("DVD registration files must not be links")
    if journal.exists():
        state = json.loads(journal.read_text(encoding="utf-8"))
        original = None if state["original"] is None else base64.b64decode(state["original"], validate=True)
        current = ini.read_bytes() if ini.exists() else None
        if current == original:
            journal.unlink()
            if not disable:
                register(root)
            return
        if current is None or digest(current) != state["installed_sha256"]:
            raise ValueError("dlmpg.ini changed after registration. Preserve those edits before restoring it")
        if disable:
            if state["original"] is None:
                ini.unlink()
            else:
                replace(ini, original)
            journal.unlink()
        return
    if disable:
        raise ValueError("This installation has no saved DVD registration")
    if digest((root / "XFiles.exe").read_bytes()) != DVD_HASH:
        raise ValueError("DVD playback requires the supported DVD executable")
    if not (root / "XFilesMpeg.dll").is_file():
        raise ValueError("XFilesMpeg.dll is missing. Stage the current patch first")
    original = ini.read_bytes() if ini.exists() else None
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    if original is not None:
        config.read_string(original.decode("cp1252"))
    section = next((s for s in config.sections() if s.lower() == "interface"), "interface")
    if not config.has_section(section):
        config.add_section(section)
    for key in list(config[section]):
        if key.lower() == "dll":
            del config[section][key]
    config[section]["dll"] = "XFilesMpeg.dll"
    output = io.StringIO()
    config.write(output, space_around_delimiters=False)
    installed = output.getvalue().replace("\n", "\r\n").encode("cp1252")
    state = {"original": None if original is None else base64.b64encode(original).decode("ascii"),
             "installed_sha256": digest(installed)}
    replace(journal, json.dumps(state).encode("utf-8"))
    try:
        replace(ini, installed)
    except BaseException:
        journal.unlink()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--disable", action="store_true")
    args = parser.parse_args()
    try:
        register(args.directory, args.disable)
    except (OSError, ValueError, configparser.Error) as error:
        parser.exit(1, f"{error}\n")
    print("Original DVD configuration restored." if args.disable else
          "Experimental DVD playback enabled. Unsupported scenes keep QuickTime playback.")


if __name__ == "__main__":
    main()
