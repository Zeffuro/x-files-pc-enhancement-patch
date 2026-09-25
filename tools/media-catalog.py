"""Generate a setup catalog from an extracted English PC disc set."""

import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
root = args.source.resolve()
game = root if (root / "XFILES.EXE").exists() else root / "MININST"
extensions = {".amv", ".dmv", ".hot", ".mus", ".nmv", ".pff", ".xmv", ".xtx"}
core = {'xfilesc.dll', 'xfiles.exe', 'jrn.ttr', 'dlg.ttr', 'xfiles.gam', 'xfilest.dll', 'phn.ttr', 'xfilese.dll', 'xfiless.dll', 'hcd.ttr', 'xfiles.hdb'}
files = {}
for layer in (game, root, root / "MEDINST"):
    if not layer.is_dir():
        continue
    for path in layer.iterdir():
        if path.is_file() and (path.suffix.lower() in extensions or path.name.lower() in core):
            files.setdefault(path.name.lower(), path)
    for folder in ("XG", "XN", "XS", "XT", "XV"):
        for path in (layer / folder).rglob("*"):
            if path.is_file() and path.suffix.lower() in extensions:
                files.setdefault(path.relative_to(layer).as_posix().lower(), path)
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("w", newline="\n") as output:
    for name, path in sorted(files.items()):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        output.write(f"{name}\t{path.stat().st_size}\t{digest}\n")
print(f"Wrote {len(files)} media records.")
