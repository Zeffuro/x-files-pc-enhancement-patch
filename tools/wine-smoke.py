#!/usr/bin/env python3
"""Run synthetic Windows checks in a new, isolated Wine prefix on Linux."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import time


CHECKS = (
    "controller-delivery", "controller-state", "controller-navigation",
    "controller-click", "navigation", "native-gun", "options-navigation", "rumble",
    "fast-forward", "dvd-clock", "save-compatibility", "save-conversion",
    "save-storage", "save-slots", "installation", "welcome", "menu-colors",
    "ui-colors", "movie", "audio", "compressed-audio", "video", "canvas-presentation",
)


def run(command, directory, environment, log, timeout):
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=directory, env=environment, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", type=Path, help="Windows build/Release folder")
    parser.add_argument("output", type=Path, help="new folder for copies, prefix and logs")
    parser.add_argument("--wine", default="wine", help="Wine executable")
    parser.add_argument("--wineserver", default="wineserver", help="matching Wine server")
    parser.add_argument("--timeout", type=int, default=90, help="per-check seconds")
    args = parser.parse_args()
    if platform.system() != "Linux" or args.timeout < 1:
        parser.error("Run on Linux with a positive timeout, using xvfb-run if headless")
    wine, server = shutil.which(args.wine), shutil.which(args.wineserver)
    if not wine or not server:
        parser.error("Wine and its matching wineserver must be installed")
    binaries = args.binaries.resolve(strict=True)
    executables = [binaries / (name + "-test.exe") for name in CHECKS]
    if any(not path.is_file() for path in executables):
        parser.error("Build all selected test executables first")
    output = args.output.resolve()
    if output.exists():
        parser.error("The output folder must not already exist")
    output.mkdir(parents=True)
    stage, logs = output / "checks", output / "logs"
    stage.mkdir()
    logs.mkdir()
    environment = os.environ.copy()
    # Never reuse the caller's game prefix or loader overrides.
    for key in ("WINEARCH", "WINEDLLOVERRIDES", "WINEDLLPATH"):
        environment.pop(key, None)
    environment.update(WINEPREFIX=str(output / "prefix"), WINEDEBUG="-all")
    report = {
        "system": platform.platform(), "wine": wine,
        "files": {}, "checks": [],
        "scope": "Synthetic checks only. No game, physical controller, display pacing or Proton validation.",
    }
    result = 0
    try:
        for source in executables + list(binaries.glob("*.dll")):
            target = stage / source.name
            shutil.copyfile(source, target)
            report["files"][source.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        report["version"] = subprocess.check_output(
            [wine, "--version"], env=environment, text=True, timeout=15).strip()
        code = run([wine, "wineboot", "-u"], stage, environment,
                   logs / "wineboot.log", max(120, args.timeout))
        if code:
            raise RuntimeError(f"Wine initialization failed ({code})")
        for name in CHECKS:
            started = time.monotonic()
            try:
                code = run([wine, str(stage / (name + "-test.exe"))], stage, environment,
                           logs / (name + ".log"), args.timeout)
                status = "passed" if code == 0 else "failed"
            except subprocess.TimeoutExpired:
                code, status = None, "timeout"
            report["checks"].append({"name": name, "status": status, "exit_code": code,
                                     "seconds": round(time.monotonic() - started, 3)})
            print(f"{name}: {status}", flush=True)
            result |= status != "passed"
            if status == "timeout":
                break
    except (OSError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as error:
        report["error"] = str(error) or type(error).__name__
        result = 1
    finally:
        # This prefix was created above and cannot contain an existing game session.
        try:
            if (output / "prefix").exists():
                subprocess.run([server, "-k"], env=environment, timeout=15, check=True,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except (OSError, subprocess.SubprocessError) as error:
            report["cleanup_error"] = str(error)
            result = 1
        (output / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Results: {output / 'summary.json'}")
    return int(result)


if __name__ == "__main__":
    raise SystemExit(main())
