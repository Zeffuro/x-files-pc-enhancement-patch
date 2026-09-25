# Building

Requires MSVC C++ build tools with x86 support and a Windows SDK, Git for Windows, CMake 3.24+
and Python 3.9+ for tests. The game and patch are 32-bit.

## Dependencies

`build.ps1` locates MSVC and Git Bash, downloads a checksum-verified GNU make
when needed, and builds the pinned FFmpeg version automatically. Dependencies
stay in your user cache and are reused until the build recipe changes.
The first build needs internet access and takes longer. No FFmpeg installation
or special developer terminal is needed.

For a manual dependency build, use an x86 MSVC tools prompt with MSYS bash and make on PATH:

```sh
bash tools/build-ffmpeg.sh C:/dependencies/ffmpeg-source C:/dependencies/ffmpeg
```

Use paths without spaces. The script builds a fresh extraction of the pinned
FFmpeg source and installs the shared libraries, license and source archive.
Rebuild it after changing the script; use a new prefix when changing versions. CMake fetches pinned
cnc-ddraw and zlib sources and applies the local cnc-ddraw fixes.

## Build and package

Close the game, then run from the repository root:

```powershell
.\build.ps1
```

The script checks C++ formatting with CI's pinned clang-format 22.1.3,
configures Release/x86 with static analysis, builds, runs the tests,
packages the ZIP and verifies its contents. It writes a `.zip.sha256` checksum
beside the ZIP in `build/packages` and stops at the first failure.
It installs the formatter in an isolated environment under `build/format-tools`
on first use. It does not install the patch or publish a release.

FFmpeg is found through `FFMPEG_ROOT`, the existing CMake cache,
`~/.cache/xfiles-enhancement/ffmpeg-install`, or `C:/dependencies/ffmpeg`.
You can supply a different location and request a clean rebuild:

```powershell
.\build.ps1 -FFmpegRoot C:/dependencies/ffmpeg -Clean
```

Other options are `-Version 0.1.0`, `-Jobs 4`, `-BuildDirectory build-release`
and `-RebuildFFmpeg` to force a fresh dependency build.
The script works from any current directory. If PowerShell blocks local scripts,
run `powershell -ExecutionPolicy Bypass -File .\build.ps1`; this applies to that
process only.

The existing CMake generator is reused. For a new directory, CMake selects an
installed Visual Studio generator. VS 2026 needs CMake 4.2+.
Use a different build directory when switching generators.

Local builds use `XFILES_VERSION` from `CMakeLists.txt`. Override it with
`-Version 0.1.0`. The GitHub workflow passes the
numeric part of its tag, for example `0.1.0` from `v0.1.0`.
The script passes the same version to configuration and package verification.

The build script configures CMake to use its pinned formatter. After configuring, run:

```powershell
cmake --build build --config Release --target format-check
```

Use `--target format` instead of `format-check` to apply formatting.
Configure with `-DXFILES_CODE_ANALYSIS=ON` for MSVC static analysis.
Compiler and analysis warnings fail the build.

Default CTest checks use synthetic fixtures, not a full game installation.
The local suite includes audio-device playback. Hosted CI excludes the
`audio-hardware` label, while still testing movie timing, callbacks, subtitles,
video rendering and decoding without an audio device. The display test remains
enabled and sizes its test windows to the available desktop work area.
Failed playback tests print their runtime log; CI also uploads failure logs.
To test setup against your own media, use a new destination folder:

```powershell
.\build\Release\setup-test.exe "D:\XFiles-media" "C:\Games\XFiles-setup-test"
```

A DVD ISO can be passed directly instead of a folder. Direct ISO import uses
ISO9660; mount UDF-only images in Windows and pass the mounted drive instead.
This also tests cancelled and failed imports. It does not play through the game.
Test at least one real DVD image and a seven-CD set before claiming both work.

Release ZIPs put `patch.ini` and `ddraw.ini` under `defaults/`. Setup copies them
to the game folder for a new install; the launcher creates only missing live
files. Don't put defaults back at the ZIP root: that would reset settings when
users extract an update. Local build directories still have root INIs for tests.

For an update smoke test, change a setting and save a game, close it, then extract
the new CPack ZIP into that installation. Check the settings and save still work,
and that the version at the bottom of F10 settings changed. Don't rerun Setup.

## GitHub releases

Push to `main` or `master`, or open a pull request, to run source checks and the
Windows build with static analysis and tests. The workflow checks the package
and uploads the ZIP with `SHA256SUMS.txt`.

After testing the extracted package, tag the commit and push the tag:

```sh
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

The tag supplies the version. A separate job creates a draft release only after
all checks pass. Versions below 1.0 are marked as prereleases. Review the notes
and attached ZIP before publishing; the workflow never publishes automatically.

## Source and tools

`src/setup` handles installation; `src/launcher` starts the game.
`src/enhancements` contains gameplay changes and `ui/` contains the settings and
overlays. `src/media`, `src/playback`, `src/quickdraw` and `src/picture` replace
QuickTime. Tests are in `tests/`.

Enable `-DXFILES_BUILD_TOOLS=ON` for `xfiles-probe`, `xfiles-media` and
`xfiles-checkpoint`. They aren't included in release packages.

`xfiles-probe probe <source> <new-folder> --fresh --media <media-folder>` runs a
15-second startup check, stopping on desktop or cursor changes. Use `play`
instead of `probe` for an interactive run; normal play has no desktop watchdog.

`xfiles-checkpoint <save.x> <installed-game-folder>` puts a save in the quick-load
slot. Close the game first, then launch it and press F9. Existing quick-saves get
numbered backups; normal save slots and the source file are left alone. Use
working saves, not ones made by changing a scene ID: inventory and story flags
can be wrong. No checkpoints are bundled.

`quicktime.log` rotates at 2 MiB and keeps one previous file. Crash snapshots
are kept across normal launches.

## Media and redistribution

Keep game files, SDKs, saves, build outputs and analysis databases out of Git.
Keep dependency licenses and the matching FFmpeg source/build script when
repackaging a release.

`data/media-*.tsv` contains filenames, sizes and SHA-256 hashes, not game content.
Regenerate a catalog with `tools/media-catalog.py` from an extracted disc set.
Review the result before adding support for another set.
