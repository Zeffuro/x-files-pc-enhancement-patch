import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix="xfiles-report-") as folder:
    subprocess.run([sys.argv[1], folder], check=True)
    with zipfile.ZipFile(pathlib.Path(folder) / "report.zip") as archive:
        assert archive.testzip() is None
        assert set(archive.namelist()) == {
            "report.txt", "quicktime.log", "launcher.log", "desktop.log", "crash-desktop.log"
        }
        assert archive.read("crash-desktop.log") == b"previous crash desktop"
        desktop = archive.read("desktop.log")
        assert len(desktop) == 2 * 1024 * 1024 and desktop.endswith(b"desktop tail")
        assert archive.read("report.txt") == b"test summary"
        assert archive.read("launcher.log") == b"launcher evidence"
        log = archive.read("quicktime.log")
        assert len(log) == 2 * 1024 * 1024 and log.endswith(b"tail")
    with zipfile.ZipFile(pathlib.Path(folder) / "with-save.zip") as archive:
        assert archive.testzip() is None
        assert archive.read("reproduction.x") == (pathlib.Path(folder) / "selected.x").read_bytes()
        assert "PRIVATE.x" not in archive.namelist()
print("Independent ZIP reader verified CRCs, contents, size bounds and exclusions.")
