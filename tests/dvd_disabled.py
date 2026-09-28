import pathlib
import shutil
import subprocess
import sys
import tempfile


with tempfile.TemporaryDirectory(prefix="xfiles-dvd-disabled-") as directory:
    root = pathlib.Path(directory)
    executable = root / "dvd-abi-test.exe"
    shutil.copyfile(sys.argv[1], executable)
    shutil.copyfile(sys.argv[3], root / "teaser.vob")
    (root / "patch.ini").write_text("[Video]\nDVDMovies=0\n", encoding="ascii")
    subprocess.run([str(executable), sys.argv[2], str(root / "teaser.vob"), "--disabled"],
                   check=True, timeout=20)
