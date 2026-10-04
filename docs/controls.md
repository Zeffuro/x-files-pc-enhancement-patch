# Controls and settings

## Keyboard and mouse

| Key | Action |
|---|---|
| F10 | Open enhancement settings |
| F8 | Open the dialogue transcript |
| Hold Left Alt | Reveal interactions and exits |
| Alt+Enter | Switch between windowed and borderless |
| F5 | Quick-save during exploration |
| F9 | Quick-load during exploration or from the main menu |
| Arrow keys | Select conversation responses, evidence, inventory or device controls |
| Enter | Use the selected control |
| Tab | Switch conversation/evidence, inventory/scene or device groups |
| Backspace | Leave inventory, close a conversation or go back on a device |
| Hold backtick | Fast-forward a movie |

Maximizing or switching to borderless keeps the game on its current monitor.
Returning to windowed restores its previous position and size.
Close movies, conversations and devices before saving or loading.
Loading replaces your current progress.

In conversations, Up/Down selects a response and Left/Right switches Talk/History.
Tab switches to evidence and Enter uses the selected icon. In inventory, either
arrow pair selects an item and Tab returns to the scene. On devices, Tab switches
controls/content and Up/Down scrolls messages. Press once per step.
Native text fields keep their normal typing and editing keys.

Use the wheel over PDA notes, PDA messages or workstation messages to scroll
while keeping the pointer in place.
In conversations, it scrolls the Talk or History list under the pointer without
choosing a response.

Click PDA or workstation message text to open the larger reader.
Scroll with the wheel, arrows, Page Up/Down or Home/End. Use **−/+** or Ctrl+wheel
to resize text. Escape or **Done** closes it. Enable **Readable documents** in F10
or the welcome screen. Image-only pages keep their original view.

### Interaction reveal

Hold **Left Alt** or the controller's **Targets** button (RT by default).
Corner marks show **View**, **Talk**, **Use** or **Interact** without hovering.
Diamonds show **Target** for separate actions. Circles mark an unclear interaction.
Movement arrows show exits. Marks and arrows do not guarantee an action will work.
Hold **Targets** and use the D-pad in any direction to select a hotspot.

In **F10 → Game**, choose all interactions, exits only or off, and choose Left Alt,
H, R or controller only. **Show hotspot labels** hides or shows captions below
targets while keeping the marks and legend. Hold through exploration scene changes.
Release to hide the cues. They hide during movies. After another screen or switching
windows, release and press again.

### Transcript and quick menu

Press **F8** or choose **Transcript** in the top-right menu to read dialogue from
the current session. It includes selected responses and available captions, even
when captions were hidden during playback. Previous/Next, arrows or the wheel turn
pages. Escape or **Done** closes it. With default controller assignments, use the
D-pad for pages and B to close. Enable **Dialogue transcript** in F10 or the welcome screen.

The transcript lasts until you close the game. Loading a save does not restore
older dialogue. Non-English editions may contain only choices and sound cues
unless an installed subtitle pack supplies dialogue.

Enable **Quick menu** in F10 or the welcome screen for Save, Load, Transcript,
Tweaks and the game's menu. Hover in the top-right corner to reveal its button.
The opened menu stays visible until you close it or choose an action.
It also works in conversations, where Save and Load are disabled.

**F10 → Quick menu...** selects visible items. Hidden items leave no gaps.
Disabling the menu keeps your choices. Hiding every item leaves the original
corner menu available. Quick menu, Dialogue transcript and Save browser have
separate toggles.

### Fast-forward

Hold **backtick** or **L3** during moving QuickTime scenes with sound or supported
DVD movies. Release for normal speed and sound. Hold through movie changes to
continue. After pausing, seeking or switching windows, release before pressing again.

Choose **2x**, **3x** or **4x** in **F10 → Fast-forward**. Sound is muted by default.
Turn off **Mute** to hear dialogue at its natural pitch at any of these speeds,
on both CD and DVD movies.

To change the key, set `MovieSpeedKey` under `[Input]` in `patch.ini` to a Windows
virtual-key number. The default is `192`. Set `0` to disable the shortcut.

## Controller

Use an Xbox-compatible controller. Set up PlayStation controllers as
Xbox-compatible first. The PlayStation names below refer to matching button positions.
These are the default assignments.

| Xbox / PlayStation | Action |
|---|---|
| A / Cross | Use or confirm |
| X / Square | Examine or right-click |
| B / Circle | Switch inventory/device focus or close a conversation |
| Y / Triangle | Return to the game view |
| Menu / Start | Open the menu or resume |
| View / Select | Skip a movie, if enabled in the game options |
| D-pad | Select controls |
| Left stick | Move the pointer |
| Hold left stick click / L3 | Fast-forward a movie |
| Right stick click / R3 | Switch conversation choices/evidence |
| LB or RB + left/right | Select exits |
| RT + left/right | Select hotspots |
| Hold RT | Reveal interactions and exits |
| LT | Equip the gun, if owned |
| LT + D-pad left/right | Select action targets (experimental) |

Using another connected controller makes it active. After reconnecting or leaving
settings, release the buttons and centre the stick. Combat and some menus remain unfinished.

**Controller hints** in F10 show button positions. The filled circle marks the
button to press, regardless of its symbol. Hints follow your assignments, appear
with controller use and hide with mouse use. Other buttons appear by name.

### Configure controls

Open **F10 → Controller → Configure controller...**. Choose an action and button,
or choose **Capture button**, release all controls and press the desired button.
Assignments swap to keep every action available. D-pad navigation stays fixed.

Use the live readout to adjust deadzone, pointer sensitivity, response curve and
trigger threshold. Either stick axis can be inverted. **Reset recommended** restores
controls and calibration. Choose **OK** in both dialogs to save. **Cancel** in the
main settings discards controller-dialog changes too.

### Conversations, devices and combat

In conversations, Up/Down selects responses. Left/Right or LB/RB switches
Talk/History. R3 switches to evidence, then A uses the selected icon.
Keyboard Tab also switches groups. Right-click closes the conversation.

On devices, B switches controls/content and Y goes back. Selecting a text field
opens an on-screen keyboard. Use the D-pad to select a key, A to type, X to delete
and Y or Start to close. Normal typing also works.

With the gun equipped, A fires. Inventory holsters it. **Controller vibration**
is on by default for gunfire and supported action scenes. It uses the active
controller and stops on pause, skip, menus or switching away from the game.

## Settings

Choose **Tweaks** on the main menu or press **F10**. The tabs are **Display and audio**,
**Controller**, **Subtitles** and **Game**. Ctrl+Page Up/Down switches tabs without
losing changes. **OK** saves and **Cancel** discards them. Settings include window
size, scaling filters, audio device, controller options and subtitles.
Wider windows keep the picture's original proportions with side bars.

**Deinterlace DVD video** smooths interlaced movies. Turning it off keeps original
fields. It applies to the next DVD movie and leaves QuickTime unchanged.
DVD playback is experimental, covering startup movies and one English game-over
scene. The game-over scene needs subtitles set to **On** or **Off**.
**Game preference** uses QuickTime instead.

**Use DVD movies** is available on the supported DVD edition with DVD assets
installed. Turn it off for QuickTime versions. It applies to the next movie
when experimental DVD playback is enabled.

**Movie colors** defaults to **Original (off)**. **Reviewed scene grades** applies
fixed adjustments to reviewed files and leaves other files unchanged.
**Contrast +15%** and **Contrast +25%** affect all moving QuickTime Cinepak movies
and may lose shadow detail. Menus, stills, subtitles and DVD MPEG keep original colors.

**Movie preview** shows original/selected colors side by side. Choose an early
scene and **Play preview** for a silent loop while gameplay stays paused.
**Use selection**, then **OK**, applies it. Scaling filters change the game picture
behind F10 immediately. **Cancel** restores the previous filter. Differences may
be small at original size. The color preview uses fixed scaling.

First launch selects **Recommended** choices: controller support, vibration,
button hints, analog pointer, black menu backgrounds, shorter menu animations,
save browser, DVD movies, deinterlacing and muted fast-forward. Adjust any checkbox,
fast-forward speed, captions or movie colors before starting.

Subtitles have font, size and optional background colour/opacity settings.

- **On** shows installed subtitles or captions included in the movie.
- **Game preference** follows the original caption setting.
- **Off** hides captions.

Many English scenes include dialogue captions. German, French and Spanish editions
generally lack them. Enabling captions does not create or translate missing text.

## Saves

Saves are in the game's `saves` folder. Back up the whole folder when moving to
another computer. Older patch saves are kept.

Enable **Save browser with thumbnails** in **F10 → Game**. The main menu's **Save**
and **Load** open separate screens. Choose a slot and optional name, then save.
Overwriting/deleting asks for confirmation. Hover a save for a silent scene preview,
when available.

Previous/Next, wheel or Page Up/Down changes pages, wrapping at the ends.
Arrows or D-pad select slots/buttons. Enter or A confirms, Escape or B goes back,
and Tab or X selects the next control. LB/RB changes pages.

Load cycles through **Numbered slots**, **Existing files**, **Quicksave** and
**Autosaves**. Existing files lists older saves. Quicksaves and autosaves can be
loaded but cannot be overwritten/deleted here. **F10 → Tools** can also load a
file or export a save. Quick-saving backs up the previous quick-save.

**Rolling safe autosaves** defaults to on in **F10 → Game**. It keeps five checkpoints
after returning to safe exploration, waits for the scene to settle and leaves at
least 30 seconds between writes. Manual saves and quicksaves are separate.
Turn it off to stop autosaving.

**Previous loads latest save at startup** also defaults to on in **F10 → Game**.
At startup, **Previous** loads the newest compatible manual save, quicksave,
autosave or older file. Older files use their file date. Unreadable/incompatible
saves are skipped. This setting is independent of autosaving. Turn it off for the
original startup behavior. During a paused game, **Return** resumes the current
session, including unsaved progress.

On DVD, supported older CD saves use experimental conversion. A warning explains
limits before loading a separate copy. The original stays unchanged. Some PDA
history and password counters use defaults. Check progress and save to a new file.

## Subtitle packs

Use **F10 → Tools → Export subtitles** to create a ZIP or folder. Edit SRT files
as UTF-8 with a text/subtitle editor. Keep filenames, folders and `manifest.tsv` together.

Choose **Install subtitles**, select the ZIP/folder and set captions to **On**.
A new pack replaces the previous pack and leaves movies unchanged. Share packs
by zipping the exported files/folders. To remove one, close the game and delete
its `subtitles` folder.

For translations, export from the edition you play and check wording/timing
against its audio. The patch includes no translations.

## Developer tools and bug reports

Press **Ctrl+F11** or open **F10 → Tools**. See [Developer tools](developer-tools.md)
for previews, state editing, watches, change history and database/asset browsing.

Use **F10 → Tools → Save report ZIP** for bugs. You can include a save.
Nothing uploads automatically. Check local paths in reports before sharing.
Logs are in the game's `logs` folder.
