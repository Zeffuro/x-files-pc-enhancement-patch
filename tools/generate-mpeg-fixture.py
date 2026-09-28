#!/usr/bin/env python3
"""Regenerate the synthetic DVD decoder fixture with an external FFmpeg CLI."""

import hashlib
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "tests/fixtures"


def run(*args):
    return subprocess.check_output(["ffmpeg", "-hide_banner", "-v", "error", "-nostdin", *args])


def bwdif_reference(movie):
    frames = run("-i", str(movie), "-map", "0:v:0",
                 "-vf", "bwdif=mode=send_frame:parity=auto:deint=interlaced",
                 "-fps_mode", "passthrough", "-f", "framemd5", "-").decode()
    hashes = [line.split(",")[-1].strip() for line in frames.splitlines()
              if line and not line.startswith("#")]
    (OUTPUT / "dvd-stream-bwdif.txt").write_text("\n".join(hashes) + "\n", encoding="ascii")


def main():
    OUTPUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xfiles-mpeg-fixture-") as temp:
        movie = pathlib.Path(temp) / "stream.mpg"
        run("-f", "lavfi", "-i", "testsrc2=size=64x48:rate=30000/1001:duration=3.003",
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=3.003",
            "-vf", "setpts=PTS+2,setsar=1,setfield=tff", "-c:v", "mpeg2video", "-g", "6", "-bf", "2",
            "-fps_mode", "passthrough", "-flags", "+ildct+ilme", "-b:v", "200k", "-c:a", "pcm_dvd",
            "-ar", "48000", "-ac", "2", "-f", "dvd", str(movie))
        frames = run("-i", str(movie), "-map", "0:v:0", "-fps_mode", "passthrough",
                     "-f", "framemd5", "-").decode()
        audio = run("-i", str(movie), "-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "-")
        hashes = [line.split(",")[-1].strip() for line in frames.splitlines()
                  if line and not line.startswith("#")]
        (OUTPUT / "dvd-stream.mpg").write_bytes(movie.read_bytes())
        (OUTPUT / "dvd-stream.txt").write_text(
            f"{len(hashes)} {len(audio) // 4} {hashlib.md5(audio).hexdigest()}\n"
            + "\n".join(hashes) + "\n", encoding="ascii")
        bwdif_reference(movie)
        print(subprocess.check_output(["ffmpeg", "-version"]).decode().splitlines()[0])
        print(f"Generated {len(hashes)} video frames and {len(audio) // 4} stereo samples")


if __name__ == "__main__":
    main()
