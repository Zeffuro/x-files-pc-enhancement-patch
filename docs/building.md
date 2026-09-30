# Building the patch

These instructions are for building from source. To play, use the
[release download](https://github.com/Zeffuro/x-files-pc-enhancement-patch/releases).

## What you need

- Windows and Visual Studio C++ build tools with x86 support and a Windows SDK.
- Git for Windows.
- CMake 3.24 or newer. Visual Studio 2026 needs CMake 4.2 or newer.
- Python 3.9 or newer.

## Build

From the repository folder, run:

```powershell
.\build.ps1
```

The script downloads and builds dependencies, checks the code, runs tests and
creates a release ZIP in `build/packages`. The first build needs internet access
and takes longer. Later builds reuse downloaded dependencies.
A checksum file is written beside the ZIP. Nothing is installed or published.

Useful options:

```powershell
.\build.ps1 -Version 0.3.0
.\build.ps1 -BuildDirectory build-release -Jobs 4
.\build.ps1 -FFmpegRoot C:/dependencies/ffmpeg -Clean
```

Use `-RebuildFFmpeg` to rebuild that dependency. Use a different build folder
when changing Visual Studio versions.

To format C++ files after the first build:

```powershell
cmake --build build --config Release --target format
```

## Test

The build script runs automated tests. These do not replace testing with the game.
To include tests that play sound or change the display, close the game and run:

```powershell
.\build.ps1 -InteractiveTests
```

To check installation with your own game files, choose a new test folder:

```powershell
.\build\Release\setup-test.exe "D:\XFiles-media" "C:\Games\XFiles-test"
```

Before releasing, test the ZIP as both a new installation and an update. Check
that existing settings and saves are kept, then try saving, loading and playing.

To fuzz the movie, picture and save parsers on Ubuntu with Clang sanitizers:

```sh
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install clang libclang-rt-dev g++-multilib zlib1g-dev:i386 cmake
cmake -S tests/fuzz -B build/fuzz -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS=-m32 -DCMAKE_EXE_LINKER_FLAGS=-m32 \
  -DZLIB_LIBRARY=/usr/lib/i386-linux-gnu/libz.so -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/fuzz --parallel
python3 tests/fuzz/seeds.py build/fuzz/corpus
UBSAN_OPTIONS=halt_on_error=1 build/fuzz/movie build/fuzz/corpus/movie \
  -max_total_time=60 -timeout=5 -rss_limit_mb=1024 -max_len=65536
```

Repeat the last command with `picture` and `save` in place of `movie`.
The 32-bit build matches the game's data layouts. Keep generated corpora and
crash files outside tracked source. The seed generator uses synthetic data.

To inspect extracted DVD MPEG files, install FFmpeg's `ffprobe` command and run:

```powershell
python tools/probe-dvd-media.py "D:\XFiles-media\English" "build/dvd-media.json"
```

The report contains file hashes and media properties. It does not copy the
movies. A failed probe is recorded in the report and returns a failing exit code.
Use `--extension xmv` to inspect QuickTime assets instead.

To run the synthetic Windows checks under Wine on Linux, build on Windows first,
then make `build/Release` available to Linux. Install Wine with 32-bit program
support and Xvfb, then run from the source folder:

```sh
xvfb-run -a python3 tools/wine-smoke.py build/Release build/wine-smoke
```

The output folder must be new. The script copies test binaries and dependencies,
creates a separate prefix, and saves results and file hashes in `summary.json`.
Use `--wine` and `--wineserver` together to select a different Wine build.
No original game files are needed. These checks do not verify Proton, game
progression, physical controllers, sound output or display pacing.

For a game playtest in WSL, keep existing installations named `Playtest-CD` and
`Playtest-DVD` and an initialized Wine prefix. Install `Xvfb` and `xdotool`, then run:

```sh
python3 tools/wine-playtest.py cd --playtests /mnt/f/Games/Fox --mute-audio
python3 tools/wine-playtest.py dvd --playtests /mnt/f/Games/Fox --mute-audio
```

Run one at a time. The script uses a private display and the existing test folders.
Use `--prefix` to choose another test prefix and `--display` to choose an unused
display number above 99. The display number and process IDs are saved in the
installation's `logs/wine-playtest.json`. Audio muting applies only to that run
and preserves saved options. Omit `--mute-audio` to hear sound.

For release testing on Linux, also try setup, launch, saving and loading, L2 with
inventory shown and hidden, held L3 across movies, Alt+Tab and normal exit in an
isolated installation. Repeat through Steam Proton and Lutris's managed runner
on a real Linux desktop. Record the runner version and patch ZIP checksum.
See [Linux instructions](linux.md) for launch settings and bug reports.

To try experimental DVD playback in a separate test installation:

```powershell
python tools/dvd-mpeg.py "C:\Games\XFiles-test"
python tools/dvd-mpeg.py "C:\Games\XFiles-test" --disable
```

Close the game first. The second command restores the previous configuration.
This selects the DVD Dolby sequence, teaser and one English game-over scene
when subtitles are set to On or Off. The warehouse binocular scene and other
scenes keep QuickTime playback.

## Release

Pushing code runs the GitHub checks. After local testing, tag the release:

```sh
git tag -a v0.3.0 -m "v0.3.0"
git push origin v0.3.0
```

The tag sets the release version. If all checks pass, GitHub creates a draft
release with the ZIP and checksum. Review it before publishing. Versions below
1.0 are marked as prereleases.

Keep dependency licenses and matching FFmpeg source with redistributed packages.
Do not include game files, saves or locally installed fonts.
For native game structures and edition details, see [src/game](../src/game/README.md).
