"""Run a caption rendering variant with its own game settings file."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4 or sys.argv[3] not in {"below", "background"}:
        raise SystemExit("Pass playback-test, QuickTime.qts and below or background")
    executable = Path(sys.argv[1]).resolve(strict=True)
    quicktime = Path(sys.argv[2]).resolve(strict=True)
    mode = sys.argv[3]
    temporary_root = Path(tempfile.gettempdir()).resolve(strict=True)
    folder = Path(tempfile.mkdtemp(prefix="xfiles-caption-variant-")).resolve(strict=True)
    try:
        target_executable = folder / executable.name
        target_quicktime = folder / quicktime.name
        shutil.copy2(executable, target_executable)
        shutil.copy2(quicktime, target_quicktime)
        for dependency in quicktime.parent.glob("*.dll"):
            shutil.copy2(dependency, folder / dependency.name)
        (folder / "patch.ini").write_text(
            "[Accessibility]\nCaptions=0\nCaptionFont=2\nCaptionScale=100\n"
            f"CaptionBackground={int(mode == 'background')}\n"
            f"CaptionPlacement={int(mode == 'below')}\n",
            encoding="ascii",
        )
        result = subprocess.run(
            [str(target_executable), str(target_quicktime), "--no-audio",
             f"--caption-{mode}"],
            capture_output=True,
            text=True,
            timeout=120,
            check=False,
        )
        if result.returncode:
            print(result.stdout + result.stderr)
        else:
            print(f"Caption {mode} rendering passed")
        return result.returncode
    finally:
        if folder != temporary_root and folder.is_relative_to(temporary_root):
            shutil.rmtree(folder)


if __name__ == "__main__":
    raise SystemExit(main())
