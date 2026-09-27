"""Check the dependency patches against a fresh Windows-style checkout."""

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
    patches = [ROOT / "patches" / f"cnc-ddraw-{name}.patch" for name in ("desktop", "logs")]
    with tempfile.TemporaryDirectory(prefix="xfiles-ddraw-patch-") as temporary:
        work = Path(temporary) / "source"
        run("git", "clone", "--quiet", "--shared", "--no-checkout", str(source), str(work))
        run("git", "config", "core.autocrlf", "true", cwd=work)
        run("git", "checkout", "--quiet", "--detach", revision, cwd=work)
        for patch in patches:
            if b"\r" in patch.read_bytes():
                raise RuntimeError(f"{patch.name} must use LF line endings")
            crlf = Path(temporary) / patch.name
            crlf.write_bytes(patch.read_bytes().replace(b"\n", b"\r\n"))
            run("git", "apply", "--numstat", str(crlf), cwd=work)
        command = ("cmake", f"-DSOURCE_DIR={work}", f"-DPATCH_FILE={patches[0]}",
                   f"-DLOG_PATCH_FILE={patches[1]}", "-P", str(ROOT / "cmake/patch-ddraw.cmake"))
        run(*command)
        first = run("git", "diff", cwd=work)
        if not first:
            raise RuntimeError("The dependency patches made no changes")
        run(*command)
        if run("git", "diff", cwd=work) != first:
            raise RuntimeError("Applying the patches twice changed the result")
    print("Both dependency patches apply cleanly and can be reapplied safely")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.stderr.buffer.write(error.output)
        raise SystemExit(error.returncode)
