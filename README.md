# The X-Files PC Enhancement Patch

Play *The X-Files Game* on modern Windows without installing QuickTime.
Adds windowed play, controller support, a save browser and subtitle options.

Supports English PC CD/DVD and German, French, Spanish, Italian and Japanese PC CD editions.
You need your own copy of the game.

[Download](https://github.com/Zeffuro/x-files-pc-enhancement-patch/releases) ·
[Controls and settings](docs/controls.md) · [Developer tools](docs/developer-tools.md) · [What's new](CHANGELOG.md) · [Building](docs/building.md)

Trying Wine, Steam Proton or Lutris? See [Linux instructions](docs/linux.md).

**Early release. A full playthrough hasn't been tested.**

## Install

Choose the **xfiles-enhancement** ZIP to install or update the game patch.
The **xfiles-devtools** ZIP is a separate portable browser for your own game
databases, saves and assets. See [Developer tools](docs/developer-tools.md).

1. Extract the release ZIP and run **XFilesSetup.exe**.
2. Choose **Image...** for a DVD image, or **Folder...** for game files or a folder
   containing all seven CD images. Keep matching BIN and CUE files together.
3. Choose a new folder and click **Install**. Start the game with **XFilesPlay.exe**
   or the shortcut created by setup.

Setup's language can be changed independently of the game's language.
On first launch, the **Recommended** choices are already selected. Adjust them to suit how you play.
You can change these later with **F10**.

The discs aren't needed after installation. If setup cannot open a DVD image,
mount it in Windows and select the mounted drive with **Folder...**.
MDF/MDS images and the French DVD edition aren't supported yet.

Choose a writable folder outside Program Files. To uninstall, remove the game
folder and its shortcuts. Back up your saves first.

## Update

1. Close the game and back up the `saves` folder.
2. Extract the new release ZIP into your existing game folder. Replace files when asked.
3. Start **XFilesPlay.exe**. Don't run setup again.

Your saves and settings are kept. Use the release ZIP, not the Source code download.

## Playing

- **F10** opens enhancement settings.
- **Alt+Enter** switches between windowed and borderless play.
- **F5** quick-saves during exploration.
- **F9** quick-loads during exploration or from the main menu.
- Rolling autosaves keep five safe exploration checkpoints. At startup, the
  original **Previous** button loads your newest compatible save by default.
  During a paused game, **Return** resumes your current session.

See [Controls and settings](docs/controls.md) for saves, controllers and subtitles.
Caption availability depends on your game edition. Controller support is still
unfinished in combat and some menus. The game keeps its original proportions.

## Help and credits

For a bug report, use **F10 → Tools → Save report ZIP** and describe what happened.
You can include a save. Nothing is uploaded automatically.

[MIT license](LICENSE). Built with [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw),
[FFmpeg](https://ffmpeg.org/) and [zlib](https://zlib.net/).
See [third-party notices](THIRD_PARTY.md).

Thanks to [mthcore/x-files-game](https://github.com/mthcore/x-files-game) and
[Agrippa](https://github.com/xesf/agrippa) for their game format research.
