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
.\build.ps1 -Version 0.2.0
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

## Release

Pushing code runs the GitHub checks. After local testing, tag the release:

```sh
git tag -a v0.2.0 -m "v0.2.0"
git push origin v0.2.0
```

The tag sets the release version. If all checks pass, GitHub creates a draft
release with the ZIP and checksum. Review it before publishing. Versions below
1.0 are marked as prereleases.

Keep dependency licenses and matching FFmpeg source with redistributed packages.
Do not include game files, saves or locally installed fonts.
For native game structures and edition details, see [src/game](../src/game/README.md).
