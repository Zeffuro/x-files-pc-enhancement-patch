#!/usr/bin/env python3
"""Run an existing CD or DVD playtest on a private Linux display."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("edition", choices=("cd", "dvd"))
    parser.add_argument("--prefix", type=Path, default=Path.home() / "xfiles-wine")
    parser.add_argument("--playtests", type=Path, default=Path("/mnt/f/Games/Fox"))
    parser.add_argument("--mute-audio", action="store_true")
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--display", type=int, default=101)
    args = parser.parse_args()
    if os.name != "posix" or args.timeout < 1 or args.display < 100:
        parser.error("Run on Linux with a positive timeout")
    for name in ("wine", "wineserver", "Xvfb", "xdotool"):
        if not shutil.which(name):
            parser.error(f"Missing {name}")
    root = args.playtests / ("Playtest-CD" if args.edition == "cd" else "Playtest-DVD")
    if not (root / "XFilesPlay.exe").is_file() or not (args.prefix / "system.reg").is_file():
        parser.error("The existing playtest and Wine prefix must be available")
    prefix = args.prefix.resolve()
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            environment = (proc / "environ").read_bytes().split(b"\0")
            command = (proc / "cmdline").read_bytes().lower()
        except (PermissionError, FileNotFoundError, ProcessLookupError):
            continue
        if f"WINEPREFIX={prefix}".encode() in environment and b".exe" in command:
            parser.error("Close existing applications in this test prefix first")
    logs = root / "logs"
    logs.mkdir(exist_ok=True)
    metadata = logs / "wine-playtest.json"
    env = os.environ.copy()
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WINEDLLOVERRIDES", "WINEDLLPATH"):
        env.pop(key, None)
    env.update(WINEPREFIX=str(prefix), WINEDEBUG="-all", WINEDLLOVERRIDES="ddraw=n,b",
               WINE_LARGE_ADDRESS_AWARE="0", PROTON_FORCE_LARGE_ADDRESS_AWARE="0")
    if args.mute_audio:
        env["XFILES_TEST_MUTE_AUDIO"] = "1"
    else:
        env.pop("XFILES_TEST_MUTE_AUDIO", None)
    env["DISPLAY"] = f":{args.display}"
    probe = ["xdotool", "getdisplaygeometry"]
    if subprocess.run(probe, env=env, capture_output=True).returncode == 0:
        parser.error("The requested private display is already running")
    game = display = None
    record = {"game": str(root), "prefix": str(prefix), "mute_audio": args.mute_audio}
    with (logs / "wine-playtest-display.log").open("wb") as display_log, \
            (logs / "wine-playtest-launch.log").open("wb") as game_log:
        try:
            display = subprocess.Popen(
                ["Xvfb", env["DISPLAY"], "-pn", "-screen", "0", "1600x1200x24", "-nolisten", "tcp"],
                stdout=display_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + 10
            while display.poll() is None and time.monotonic() < deadline:
                if subprocess.run(probe, env=env, capture_output=True).returncode == 0:
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Private display did not start")
            game = subprocess.Popen(["wine", "XFilesPlay.exe"], cwd=root, env=env,
                                    stdout=game_log, stderr=subprocess.STDOUT)
            record.update(display=env["DISPLAY"], display_pid=display.pid, launcher_pid=game.pid,
                          active=True)
            metadata.write_text(json.dumps(record, indent=2) + "\n")
            print(json.dumps(record), flush=True)
            game.wait(timeout=args.timeout)
        finally:
            try:
                if game:
                    subprocess.run(["wineserver", "-k"], env=env, timeout=15, check=True,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    game.wait(timeout=15)
            finally:
                if display:
                    display.terminate()
                    try:
                        display.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        display.kill()
                        display.wait()
                record["active"] = False
                metadata.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    main()
