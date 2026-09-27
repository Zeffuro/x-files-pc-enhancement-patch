"""Check exported subtitle packs with an independent ZIP reader."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile

with tempfile.TemporaryDirectory(prefix="xfiles-subtitle-zip-") as directory:
    archive = pathlib.Path(directory) / "export.zip"
    subprocess.run([sys.argv[1], "--archive-fixture", str(archive)], check=True)
    with zipfile.ZipFile(archive) as pack:
        assert pack.testzip() is None
        assert set(pack.namelist()) == {"manifest.tsv", "skipped.txt", "XV/123.srt", "XV/456.srt"}
        assert pack.read("manifest.tsv").startswith(b"xfiles-subtitles-v1\n")
        assert b"Edited" in pack.read("XV/123.srt")
        assert b"Second clip" in pack.read("XV/456.srt")
        assert all(entry.compress_type == zipfile.ZIP_DEFLATED for entry in pack.infolist())
print("Subtitle pack interoperability passed")
