"""Check fresh wrapper installs, cached upgrades and repeated patch application."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, stderr=subprocess.STDOUT)


def main():
    source = Path(sys.argv[1]).resolve()
    revision = re.search(r"GIT_TAG ([0-9a-f]{40})", (ROOT / "cmake/ddraw.cmake").read_text())[1]
    patches = [ROOT / "patches" / f"cnc-ddraw-{name}.patch"
               for name in ("desktop", "logs", "dvd", "monitor", "mouse-lock")]
    with tempfile.TemporaryDirectory(prefix="xfiles-ddraw-patch-") as temporary:

        def checkout(name):
            work = Path(temporary) / name
            run("git", "clone", "--quiet", "--shared", "--no-checkout", str(source), str(work))
            run("git", "config", "core.autocrlf", "true", cwd=work)
            run("git", "checkout", "--quiet", "--detach", revision, cwd=work)
            return work

        def apply(work):
            run("cmake", f"-DSOURCE_DIR={work}", f"-DPATCH_FILE={patches[0]}",
                f"-DLOG_PATCH_FILE={patches[1]}", f"-DDVD_PATCH_FILE={patches[2]}",
                f"-DMONITOR_PATCH_FILE={patches[3]}", f"-DMOUSE_PATCH_FILE={patches[4]}",
                "-P", str(ROOT / "cmake/patch-ddraw.cmake"))

        work = checkout("fresh")
        for patch in patches:
            if b"\r" in patch.read_bytes():
                raise RuntimeError(f"{patch.name} must use LF line endings")
            crlf = Path(temporary) / patch.name
            crlf.write_bytes(patch.read_bytes().replace(b"\n", b"\r\n"))
            run("git", "apply", "--numstat", str(crlf), cwd=work)
        apply(work)
        first = run("git", "diff", cwd=work)
        if not first:
            raise RuntimeError("The dependency patches made no changes")
        apply(work)
        if run("git", "diff", cwd=work) != first:
            raise RuntimeError("Applying the patches twice changed the result")

        cached = checkout("cached")
        for patch in patches[:-1]:
            run("git", "apply", str(patch), cwd=cached)
        apply(cached)
        if run("git", "diff", cwd=cached) != first:
            raise RuntimeError("The cached wrapper upgrade differs from a fresh install")
        apply(cached)
        if run("git", "diff", cwd=cached) != first:
            raise RuntimeError("Repeating the cached wrapper upgrade changed the result")
    print("Fresh wrapper patches, cached upgrades and repeated application pass")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.stderr.buffer.write(error.output)
        raise SystemExit(error.returncode)
