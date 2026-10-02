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
    "autosave-state", "save-recent", "continue-menu", "autosave-runtime",
    "game-story-state", "game-state-tree", "game-state-variables", "game-story-edit", "game-state-history",
    "game-state-capture", "native-writes", "database-flow",
    "database-usability",
    "controller-delivery", "controller-state", "controller-navigation",
    "controller-click", "navigation", "native-gun", "options-navigation", "rumble",
    "fast-forward", "dvd-clock", "save-compatibility", "save-conversion",
    "save-storage", "save-slots", "installation", "welcome", "menu-colors",
    "ui-colors", "movie", "audio", "compressed-audio", "video", "canvas-presentation",
    "transcript-history", "transcript-page", "transcript-capture", "transcript-view",
    "browser-session", "scene-overlay", "menu-control", "quick-menu", "quick-menu-navigation",
    "quick-menu-dialog", "settings-tabs", "settings-dialog", "controller-profile", "controller-dialog",
    "controller-profile-portable",
    "database", "database-io", "database-native", "database-browser", "database-model",
    "database-payload", "database-story", "database-trigger", "database-assets", "database-offline", "database-stored-browser",
    "hotspots", "standalone-preview", "pff", "pff-image", "asset-io", "xt-text", "asset-browser",
    "font-preview", "database-gam", "database-stored-trigger", "resource-strings", "resource-browser",
    "database-text-browser",
    "database-stored-list", "database-stored-list-browser",
    "database-stored-action", "database-stored-action-browser",
    "database-stored-asset-list", "database-stored-asset-list-browser",
    "database-stored-asset-ref", "database-stored-asset-ref-browser",
    "database-stored-fields", "database-stored-fields-browser",
    "database-stored-object-list", "database-stored-object-list-browser",
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
    parser.add_argument("--timeout", type=int, default=180, help="per-check seconds")
    parser.add_argument("--display-monitors", action="store_true",
                        help="also test monitor toggles and moved-window restore in the private display")
    args = parser.parse_args()
    if platform.system() != "Linux" or args.timeout < 1:
        parser.error("Run on Linux with a positive timeout, using xvfb-run if headless")
    wine, server = shutil.which(args.wine), shutil.which(args.wineserver)
    if not wine or not server:
        parser.error("Wine and its matching wineserver must be installed")
    binaries = args.binaries.resolve(strict=True)
    checks = CHECKS + (("display",) if args.display_monitors else ())
    executables = [binaries / (name + "-test.exe") for name in checks]
    if any(not path.is_file() for path in executables):
        parser.error("Build all selected test executables first")
    display_config = [binaries / "ddraw.ini"] if args.display_monitors else []
    if any(not path.is_file() for path in display_config):
        parser.error("Monitor checks need the built ddraw.ini")
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
        for source in executables + list(binaries.glob("*.dll")) + display_config:
            target = stage / source.name
            shutil.copyfile(source, target)
            report["files"][source.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        report["version"] = subprocess.check_output(
            [wine, "--version"], env=environment, text=True, timeout=15).strip()
        code = run([wine, "wineboot", "-u"], stage, environment,
                   logs / "wineboot.log", max(120, args.timeout))
        if code:
            raise RuntimeError(f"Wine initialization failed ({code})")
        cases = [(name, edition) for name in checks
                 for edition in (range(4) if name == "native-writes" else (None,))]
        for name, edition in cases:
            label = f"{name}-{edition}" if edition is not None else name
            started = time.monotonic()
            try:
                arguments = ["--visible"] if name == "standalone-preview" else []
                if edition is not None:
                    arguments = [str(edition)]
                if name in ("resource-browser", "database-usability"):
                    arguments = ["--clipboard"]
                if name == "display":
                    arguments = [str(stage / "ddraw.dll"), "--monitors-only"]
                check_environment = environment.copy()
                if name == "display":
                    check_environment["WINEDLLOVERRIDES"] = "ddraw=n,b"
                code = run([wine, str(stage / (name + "-test.exe"))] + arguments, stage, check_environment,
                           logs / (label + ".log"), max(240, args.timeout) if name == "display" else args.timeout)
                status = "passed" if code == 0 else "failed"
            except subprocess.TimeoutExpired:
                code, status = None, "timeout"
            report["checks"].append({"name": label, "status": status, "exit_code": code,
                                     "seconds": round(time.monotonic() - started, 3)})
            print(f"{label}: {status}", flush=True)
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
