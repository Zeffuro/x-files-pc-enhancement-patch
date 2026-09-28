"""Inventory extracted MPEG/DVD assets without copying media into the report."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys


DEFAULT_EXTENSIONS = {".vob", ".mpg", ".mpeg", ".m2v"}
STREAM_FIELDS = (
    "index,codec_name,codec_type,profile,id,width,height,pix_fmt,field_order,"
    "sample_aspect_ratio,display_aspect_ratio,r_frame_rate,avg_frame_rate,"
    "time_base,start_pts,start_time,duration_ts,duration,sample_rate,channels,"
    "channel_layout,bits_per_raw_sample"
)


def linked(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(
        getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    )


def media_paths(root, extensions):
    paths = []
    for directory, folders, files in os.walk(root, onerror=raise_walk_error):
        folders[:] = [name for name in folders if not linked(Path(directory) / name)]
        for name in files:
            path = Path(directory) / name
            if path.suffix.lower() in extensions and not linked(path) and path.is_file():
                paths.append(path)
    return sorted(paths, key=lambda path: path.relative_to(root).as_posix())


def raise_walk_error(error):
    raise error


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect(path, root, ffprobe, timeout):
    record = {"path": path.relative_to(root).as_posix()}
    try:
        before = path.stat()
        record.update(size_bytes=before.st_size, sha256=sha256(path))
        command = [
            ffprobe, "-v", "error", "-protocol_whitelist", "file", "-show_entries",
            f"format=format_name,start_time,duration:stream={STREAM_FIELDS}",
            "-of", "json", str(path),
        ]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout)
        if result.returncode:
            raise ValueError(result.stderr.strip() or f"ffprobe exit {result.returncode}")
        metadata = json.loads(result.stdout)
        streams = metadata.get("streams", [])
        if not streams or not any(s.get("codec_type") in ("video", "audio") for s in streams):
            raise ValueError("No audio or video streams found")
        record["format"] = metadata.get("format", {})
        record["streams"] = streams
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            raise ValueError("File changed while hashing or probing")
        if result.stderr.strip():
            record["diagnostics"] = result.stderr.strip().replace(str(root), "<source>")
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        message = "ffprobe timed out" if isinstance(error, subprocess.TimeoutExpired) else str(error)
        record["error"] = message.replace(str(root), "<source>")
    return record


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Extracted DVD directory")
    parser.add_argument("output", type=Path, help="Metadata JSON destination")
    parser.add_argument("--ffprobe", default="ffprobe", help="ffprobe executable")
    parser.add_argument("--extension", action="append",
                        help="Extension to scan, repeatable, replaces default MPEG extensions")
    parser.add_argument("--timeout", type=float, default=60, help="Seconds per probe")
    args = parser.parse_args(argv)
    root = args.source.resolve()
    if not root.is_dir() or not 0 < args.timeout < float("inf"):
        parser.error("Source must be a directory and timeout must be positive and finite")
    extensions = DEFAULT_EXTENSIONS if not args.extension else {
        "." + value.lower().lstrip(".") for value in args.extension
    }
    try:
        paths = media_paths(root, extensions)
        if not paths:
            raise ValueError("No matching media files found")
        if args.output.resolve() in paths or (args.output.exists() and any(
                args.output.samefile(path) for path in paths)):
            raise ValueError("Output must not overwrite a source media file")
        version = subprocess.run([args.ffprobe, "-version"], capture_output=True,
                                 text=True, check=True, timeout=args.timeout).stdout.splitlines()[0]
        records = [inspect(path, root, args.ffprobe, args.timeout) for path in paths]
        failures = sum("error" in record for record in records)
        report = {"schema_version": 1, "ffprobe": version, "files": records,
                  "file_count": len(records), "error_count": failures}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"Probed {len(records)} files, {failures} errors. Wrote {args.output}")
        return 1 if failures else 0
    except (OSError, ValueError, IndexError, subprocess.SubprocessError) as error:
        print(f"Media inventory failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
