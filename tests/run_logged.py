"""Include the patch log in CTest output when a dispatched call terminates the process."""

import os
from pathlib import Path
import subprocess
import sys


def main() -> int:
    log = Path(sys.argv[1]).resolve()
    log.parent.mkdir(parents=True, exist_ok=True)
    log.write_text("", encoding="utf-8")
    environment = dict(os.environ, XFILES_PATCH_LOG=str(log))
    result = subprocess.run(sys.argv[2:], env=environment)
    if result.returncode:
        print(f"Test exited with 0x{result.returncode & 0xffffffff:08X}", flush=True)
        with log.open("rb") as stream:
            stream.seek(max(0, log.stat().st_size - 32768))
            print(stream.read().decode("utf-8", errors="replace"), flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
