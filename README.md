# The X-Files PC Enhancement Patch

A patch for the English PC CD and DVD versions of *The X-Files Game*.
Replaces QuickTime and adds windowed/borderless display, controller support,
quick saves and subtitle settings.

[Download](https://github.com/Zeffuro/x-files-pc-enhancement-patch/releases) ·
[Controls](docs/controls.md) · [Building](docs/building.md)

**Early release. A full playthrough hasn't been tested.**

## Install

1. Extract the release ZIP and run **XFilesSetup.exe**.
2. Use **ISO...** for your DVD image, or **Folder...** for DVD files, an extracted
   CD set or all seven English PC CD ISOs in one folder.
3. Choose a new folder, such as `C:\Games\The X-Files`, then click **Install**.
   After setup, use **XFilesPlay.exe** or your shortcut.

You need your own game. Setup copies and checks the files; the source discs
aren't needed afterward. UDF-only DVD images must be mounted first: select the
mounted drive with **Folder...**. Setup doesn't overwrite existing installations
or import old saves.

Choose a writable folder, not Program Files. Setup creates missing parent folders.
To uninstall, delete the installed folder and shortcuts. Back up your saves first.

## Update

1. Close the game and back up your saves.
2. Extract the **contents** of the new release ZIP into the existing folder
   containing **XFilesPlay.exe**. Replace files when asked.
3. Start **XFilesPlay.exe**. Don't run Setup again.

Use the release ZIP, not GitHub's Source code download.

Saves and settings are kept. The ZIP stores its default settings in `defaults/`,
separate from your live INI files. Updates are manual; nothing downloads automatically.

## Playing

**F10** opens settings. **Alt+Enter** switches windowed/borderless.
**F5** quick-saves during exploration; **F9** quick-loads during exploration or
from the main menu. See [Controls](docs/controls.md) for controller bindings.

Controller support is unfinished, especially combat and some menus.
16:9 adds side bars, not a widescreen interface. DVD MPEG playback isn't
implemented; the DVD version uses its QuickTime video files.

For bugs, attach a ZIP from **F10 → Tools → Save report ZIP** and explain how to
reproduce the problem. Saves are optional. Check logs for local paths before sharing.

[MIT](LICENSE). Uses [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw),
[FFmpeg](https://ffmpeg.org/) (LGPL-2.1-or-later) and [zlib](https://zlib.net/).
Dependency licenses and matching FFmpeg source are in the release ZIP.
See [third-party notices](THIRD_PARTY.md).
