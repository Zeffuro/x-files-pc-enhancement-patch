# What's new

## 0.4.6

- Added Russian patch menus and support for the Russian unofficial DVD repack from 2008.
- Translated quick-menu hover labels for every supported patch language.
- Fixed patch entries disappearing from the window's right-click menu.
- Unified IDA imports for verified names, parameters and native types, with expanded Artificial Intuition and timer coverage.

## 0.4.5

- Dedicated Easter eggs shortcut in the developer tools, with a searchable guide and direct use of the original PDA cheats.
- Live hidden scores and relationship thresholds, with links to their game variables.
- Fixed Easter egg actions being rejected when returning to the game with the quick menu enabled.
- Direct HyperBole Studios shortcut and corrected developer-office instructions.
- Fixed PDA keyboard navigation intercepting cheat submission and correction keys.
- Fixed still-image scenes playing through other frames after returning from the game menu.
- Numpad Enter calls the entered phone number.
- Fixed Wine rejecting the bomb's two-minute countdown with Artificial Intuition enabled.
- Expanded the IDA naming script with verified table, function and data labels.

## 0.4.4

- Developer tools show incoming variable, function and resource references.
- Developer tools detect nearby game files and create and rebuild numeric save recipes without Python.
- In-game tools can load saves or recipes directly. New outputs can be saved in the game folder.
- Bundled numeric starting points for all seven CD discs, with matching DVD checkpoints.

## 0.4.3

- Optional remembering of Load categories, pages and selected slots during the current session.
- A reveal-key choice of Hold, Toggle or Hold or tap.
- Optional mouse confinement during windowed gameplay, with release for menus, tools and switching windows.
- Fixed mouse-wheel scrolling in ING Field Notes.
- Added mouse-wheel scrolling in Photo Viewer.
- Fixed save/load screens overlapping the game-over movie.
- Fixed a freeze when quickly loading a save from the menu after being shot.
- Corrected and completed inventory labels in the developer tools.

## 0.4.2

- Fixed a Wine startup crash when no audio device is available. Movies continue silently.
- The launcher checks for missing or incompatible game files before starting.
- Fixed Library freezes and improved browsing large clip lists.
- In-game NMV previews now let you browse every frame and video track.
- Resize the Library and preview areas by dragging their dividers or using the keyboard.
- An optional reveal-key toggle keeps interaction cues visible until you press again.
- Up/Down navigation now reaches opened evidence and conversation responses hidden below the screen.
- Updated compression support and improved compatibility checks when loading the patch.

## 0.4.1

- Natural-pitch sound during 2x, 3x and 4x movie fast-forward on CD and DVD.
- Hold a key or controller button to reveal interactions and exits, with optional labels.
- Mouse-wheel scrolling in conversations, PDA and workstation screens keeps the pointer still.
- Click PDA or workstation messages for a larger reader with adjustable text size.
- Keyboard navigation for conversations, evidence, inventory and devices.
  Enter also confirms save names.
- Controller D-pad selection of hotspots while holding Targets.
- Fixed a crash when loading original audio files.
- Restored missing English DVD retry and laptop lockout messages.

## 0.4.0

- A separate portable developer-tools download for browsing databases, saves
  and assets without running the game.
- A searchable database and asset browser with remembered views and detail tabs.
- A searchable game-state view with snapshots, change filters and optional
  guarded editing of supported live values.
- Pinned state variables and optional change history with available native
  action and caller details.
- More reliable variable editing, ASCII character input and remembered database
  detail tabs.
- Window maximize and borderless switching stay on the current monitor, including
  restoring after moving a maximized window to another monitor.
- Five rolling autosaves during safe exploration, with a setting to turn them off.
- Optional loading of the newest compatible manual, quick, auto or older save through
  the original Previous button at startup. Return resumes the current session during play.
- Separate manual, existing, quicksave and autosave views in the save browser.
- In-game dialogue transcript with selected conversation choices and available
  captions from the current play session.
- A sliding in-game menu for Save, Load, Transcript, Tweaks and Menu, with native hover text
  and a setting to choose which items appear.
- Settings organized into tabs, with controller remapping and stick calibration.

## 0.3.0

- Optional DVD movie playback for the opening movies and one English game-over
  scene, with a deinterlace setting.
- Movie fast-forward at 2x, 3x or 4x from the keyboard or controller.
- Controller button hints and vibration for gunfire and supported action scenes.
- L2 equips the gun with inventory hidden and refreshes the crosshair in place.
- Optional movie color adjustments with a silent side-by-side preview.
- Cleaner inventory pictures, menu backgrounds and credits.
- Experimental loading of supported older CD saves on the DVD edition.
  Conversion uses a separate copy and leaves the original save unchanged.
- Instructions for trying the Windows release with Wine, Steam Proton or Lutris.
- Proton launch settings to avoid a black screen before the opening movies.
- More reliable loading of the game's fonts in the save browser and enhanced
  views under Wine.
- Fixed parts of the main menu appearing over the save browser after switching
  between fullscreen and windowed mode.

DVD movie playback, older save conversion and Linux play remain experimental.
Controller support still needs work in combat and some menus. A full playthrough
has not been tested.

## 0.2.0

- German, French, Spanish, Italian and Japanese CD support.
- A save browser with thumbnails, silent scene previews and named saves.
- First-launch choices with a Recommended preset.
- Setup and enhancement settings in six languages.
- More window sizes, including the original 640×480.
- Editable subtitle packs and more subtitle appearance options.
- Developer tools for browsing clips, editing subtitles and inspecting the game.
- Better controller switching and reconnect handling.
- Localized Tweaks buttons with hover descriptions.
- Tidier game folders and improved bug reports.

To update, extract the release ZIP into your game folder and start XFilesPlay.
Your saves and settings are kept. Don't run setup again.

This is still an early release. Combat controller support is unfinished, and a
full playthrough hasn't been tested. Vibration and controller button hints will
come later.
