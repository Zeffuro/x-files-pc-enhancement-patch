"""Check fresh wrapper installs, cached upgrades and repeated patch application."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
PREVIOUS_REVISION = "541b5de218ec3fbd6ea91e606ebfadc07c1786b0"


def run(*args, cwd=ROOT):
    return subprocess.check_output(args, cwd=cwd, stderr=subprocess.STDOUT)


def main():
    source = Path(sys.argv[1]).resolve()
    revision = re.search(r"GIT_TAG ([0-9a-f]{40})", (ROOT / "cmake/ddraw.cmake").read_text())[1]
    patches = [ROOT / "patches" / f"cnc-ddraw-{name}.patch"
               for name in ("desktop", "logs", "dvd", "monitor", "mouse-lock")]
    with tempfile.TemporaryDirectory(prefix="xfiles-ddraw-patch-") as temporary:

        def checkout(name, ref=revision):
            work = Path(temporary) / name
            run("git", "clone", "--quiet", "--shared", "--no-checkout", str(source), str(work))
            run("git", "config", "core.autocrlf", "true", cwd=work)
            run("git", "config", "user.name", "Dependency regression test", cwd=work)
            run("git", "config", "user.email", "dependency-test@example.invalid", cwd=work)
            run("git", "checkout", "--quiet", "--detach", ref, cwd=work)
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
        modified = run("git", "diff", "--name-only", cwd=work).decode().splitlines()
        modified.append("inc/git.h")
        timestamps = {name: (work / name).stat().st_mtime_ns for name in modified}
        apply(work)
        if run("git", "diff", cwd=work) != first:
            raise RuntimeError("Applying the patches twice changed the result")
        if any((work / name).stat().st_mtime_ns != stamp for name, stamp in timestamps.items()):
            raise RuntimeError("Repeated patch application touched already patched source files")
        run("git", "restore", "--worktree", "--", ".", cwd=work)
        apply(work)
        if run("git", "diff", cwd=work) != first:
            raise RuntimeError("A stale patch stamp skipped missing patches")

        cached = checkout("cached")
        for patch in patches[:-1]:
            run("git", "apply", str(patch), cwd=cached)
        apply(cached)
        if run("git", "diff", cwd=cached) != first:
            raise RuntimeError("The cached wrapper upgrade differs from a fresh install")
        apply(cached)
        if run("git", "diff", cwd=cached) != first:
            raise RuntimeError("Repeating the cached wrapper upgrade changed the result")

        previous = checkout("previous", PREVIOUS_REVISION)
        for patch in patches:
            legacy = Path(temporary) / f"legacy-{patch.name}"
            text = patch.read_text()
            if patch.name == "cnc-ddraw-monitor.patch":
                current = ('@@ -15,6 +15,26 @@\n #include "versionhelpers.h"\n'
                           ' #include "delay_imports.h"\n')
                original = ('@@ -13,6 +13,26 @@\n #include "config.h"\n'
                            ' #include "versionhelpers.h"\n')
                if current not in text:
                    raise RuntimeError("The previous monitor patch fixture needs updating")
                text = text.replace(current, original, 1)
            legacy.write_bytes(text.encode())
            run("git", "apply", str(legacy), cwd=previous)
        sentinel = previous / "local-upgrade-data.bin"
        sentinel_data = b"preserve cached checkout contents\x00\x01\xff"
        sentinel.write_bytes(sentinel_data)

        project = Path(temporary) / "updater"
        project.mkdir()
        (project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.24)\n'
            'project(WrapperUpgrade NONE)\n'
            'include(ExternalProject)\n'
            'ExternalProject_Add(wrapper\n'
            f'  GIT_REPOSITORY "{source.as_posix()}"\n'
            f'  GIT_TAG {revision}\n'
            '  GIT_SUBMODULES ""\n'
            f'  SOURCE_DIR "{previous.as_posix()}"\n'
            '  PREFIX "${CMAKE_BINARY_DIR}/prefix"\n'
            '  CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_COMMAND "")\n')
        configured = project / "build"
        run("cmake", "-S", str(project), "-B", str(configured))
        updater = configured / "prefix/tmp/wrapper-gitupdate.cmake"
        for attempt in range(2):
            run("cmake", "-Dcan_fetch=NO", "-P", str(updater))
            if run("git", "rev-parse", "HEAD", cwd=previous).decode().strip() != revision:
                raise RuntimeError("The cached dependency did not advance to the pinned revision")
            if sentinel.read_bytes() != sentinel_data:
                raise RuntimeError("The dependency update lost an unrelated local file")
            if run("git", "stash", "list", cwd=previous):
                raise RuntimeError("The dependency update left a recovery stash")
            apply(previous)
            if run("git", "diff", cwd=previous) != first:
                raise RuntimeError(f"Previous-revision upgrade differs on attempt {attempt + 1}")
    print("Fresh patches, previous-revision cached upgrade and repeated application pass")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.stderr.buffer.write(error.output)
        raise SystemExit(error.returncode)
