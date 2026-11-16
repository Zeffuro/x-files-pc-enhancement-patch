"""Generate a setup catalog from an extracted PC disc set."""

import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--french-dvd-title", type=Path,
                    help="Original French CD XV/56001.XMV for the French DVD catalog")
args = parser.parse_args()
root = args.source.resolve()
game = root if (root / "XFILES.EXE").exists() else root / "MININST"
extensions = {".amv", ".dmv", ".hot", ".mus", ".nmv", ".pff", ".xmv", ".xtx", ".vob"}
core = {'xfilesc.dll', 'xfiles.exe', 'jrn.ttr', 'dlg.ttr', 'xfiles.gam', 'xfilest.dll', 'phn.ttr', 'xfilese.dll', 'xfiless.dll', 'hcd.ttr', 'xfiles.hdb'}
files = {}
for layer in (game, root, root / "MEDINST"):
    if not layer.is_dir():
        continue
    for path in layer.iterdir():
        if path.is_file() and (path.suffix.lower() in extensions or path.name.lower() in core):
            files.setdefault(path.name.lower(), path)
    for folder in ("XG", "XN", "XS", "XT", "XV", "vob"):
        for path in (layer / folder).rglob("*"):
            if path.is_file() and path.suffix.lower() in extensions:
                files.setdefault(path.relative_to(layer).as_posix().lower(), path)
if args.french_dvd_title:
    teaser = files.pop("xv/19668tst.xmv", None)
    if teaser is None:
        parser.error("French DVD source is missing XV/19668tst.xmv")
    title = args.french_dvd_title
    expected = {
        teaser: "69cbc6568a0532ec31c904db8a3b53772f2c37d20dd622c48943efb154e43c6d",
        title: "25d3cf3ad8cbc095caabdd340dfade033081672fcd6d193e300da9cc76b37d42",
    }
    for path, digest in expected.items():
        with path.open("rb") as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != digest:
                parser.error(f"Not the verified original French movie: {path}")
    files["xv/19668.xmv"] = teaser
    files["xv/56001.xmv"] = title
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("w", newline="\n") as output:
    for name, path in sorted(files.items()):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        output.write(f"{name}\t{path.stat().st_size}\t{digest}\n")
print(f"Wrote {len(files)} media records.")
