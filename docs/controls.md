# Controls and settings

## Keyboard

| Key | Action |
|---|---|
| F10 | Open enhancement settings |
| F8 | Open the dialogue transcript |
| Alt+Enter | Switch between windowed and borderless |
| F5 | Quick-save during exploration |
| F9 | Quick-load during exploration or from the main menu |
| Hold backtick | Fast-forward a movie |

Close movies, conversations and device screens before saving or loading.
Loading replaces your current progress.

Open the top-right menu in game and choose **Transcript** to read dialogue from the current play
session. Selected conversation choices appear beside available caption text,
including captions hidden during playback. Use Previous/Next, the arrow keys
or the mouse wheel to move through the pages. Escape or Done returns to the game.
With the default assignments, the controller D-pad turns pages and B closes the transcript.
Turn **Dialogue transcript** on or off in F10 or the welcome screen.

Enable **Quick menu** in F10 or the welcome screen to open Save, Load, Transcript,
Tweaks and the game's main menu from the top-right corner. It has its own toggle,
separate from **Dialogue transcript** and **Save browser with thumbnails**.
Open **Quick menu...** in F10 to enable it and choose which items appear.
Hidden items leave no gaps. Turning it off keeps your item choices.
If every item is hidden, the original corner menu remains available.
Hover in the corner to reveal the menu button. Once opened, the menu stays
visible until you close it or choose an action.
You can also open it during conversations. Save and Load are disabled there.

The transcript stays in memory until you close the game. Loading a save does
not restore earlier dialogue. Non-English editions generally lack spoken
dialogue captions, so their transcript may contain only selected choices and
occasional sound cues. Installed subtitle packs can supply missing dialogue.

Release backtick to return to normal speed and sound. After pausing, seeking or
switching windows, release it before pressing again. This applies to moving
QuickTime scenes with sound and supported DVD movies.
Keep holding it to continue fast-forwarding into the next movie.
Choose **Fast-forward** in F10 for **2x**, **3x** or **4x** speed. Sound is muted by
default. Turn off **Mute fast-forward** to hear accelerated, higher-pitched audio.
To change the key, set `MovieSpeedKey` under `[Input]` in `patch.ini` to a Windows
virtual-key number. The default is `192`. Set it to `0` to disable the shortcut.

## Controller

Use an Xbox-compatible controller. PlayStation controllers need to be set up as
Xbox-compatible first. The PlayStation names below refer to matching button positions.
The descriptions below use the default assignments.

| Xbox / PlayStation | Action |
|---|---|
| A / Cross | Use or confirm |
| X / Square | Examine or right-click |
| B / Circle | Switch inventory or device focus, or close a conversation |
| Y / Triangle | Return to the game view |
| Menu / Start | Open the menu or resume |
| View / Select | Skip a movie, if enabled in the game options |
| D-pad | Select controls |
| Left stick | Move the pointer |
| Hold left stick click / L3 | Fast-forward a movie |
| Right stick click / R3 | Switch between conversation choices and evidence |
| LB or RB + left/right | Select exits |
| RT + left/right | Select hotspots |
| LT | Equip the gun, if owned |
| LT + D-pad left/right | Select action targets (experimental) |

With several controllers connected, using another controller makes it active.
After reconnecting or leaving settings, release the buttons and centre the stick.
Combat and some menus still need work.

Optional **Controller hints** in F10 show button positions in a four-button
diagram. The filled circle is the button to press, regardless of its printed
letter or symbol. Hints appear while using the controller and hide when using
the mouse. Hints follow your assignments. Other buttons appear by name.

Open **F10 → Controller → Configure controller...** to change the controls above.
Choose an action and select a button, or choose **Capture button**, release all
controls, and press the button you want. Assignments swap so every action stays
available. The D-pad keeps its navigation controls.

The live readout helps you adjust stick deadzone, pointer sensitivity, response
curve and trigger threshold. You can invert either stick axis.
**Reset recommended** restores the original controls and calibration.
Choose **OK** in both dialogs to save. **Cancel** in the main settings discards
changes accepted in the controller dialog.

In conversations, up/down selects a response. Left/right or LB/RB switches
Talk/History. R3 switches to evidence icons, then A uses the selected item.
Keyboard Tab also switches groups. Right-click closes the conversation.

On the PDA or workstation, B switches between controls and content. Y goes back.
Selecting a text field opens an on-screen keyboard. Use the D-pad to select a
key, A to type, X to delete, and Y or Start to close it. You can also type normally.

With the gun equipped, A fires. Entering inventory holsters it.
**Controller vibration** in F10 covers gunfire and supported action scenes.
It is on by default and uses the active controller. Vibration stops when you
pause, skip the scene, open a menu or switch away from the game.

## Settings

Choose **Tweaks** on the main menu or press **F10**. You can change the window
size, picture sharpness, audio device, controller options and subtitles.
Settings are grouped into **Display and audio**, **Controller**, **Subtitles**
and **Game** tabs. Use **Ctrl+Page Down** or **Ctrl+Page Up** to switch tabs.
Changes stay in place while you switch. **OK** saves them and **Cancel** discards them.
Wider windows keep the original picture proportions and add side bars.

**Deinterlace DVD video** smooths interlaced DVD movies. Turn it off to keep
the original fields. The change applies to the next DVD movie and does not
affect QuickTime movies. DVD playback is currently experimental and limited to
the startup movies and one English game-over scene. The game-over scene requires
subtitles set to **On** or **Off**. **Game preference** uses QuickTime instead.

Turn off **Use DVD movies** to use the QuickTime versions instead. This option
is available on the supported DVD edition when DVD movie assets are installed.
It applies to the next movie when experimental DVD playback is enabled.

**Movie colors** starts at **Original (off)**. **Reviewed scene grades** applies
fixed adjustments to reviewed movie files. Other files keep their original
colors. **Contrast +15%** and **Contrast +25%** apply the same contrast increase
to all moving QuickTime Cinepak movies. Dark scenes may lose shadow detail.
Menus, still images, subtitles and DVD MPEG movies keep their original colors.

**Movie preview** compares the original and selected colors side by side. Choose
a named early scene and **Play preview** to loop a short, silent excerpt while
gameplay stays paused. **Use selection**, then **OK**, applies your choice to the game.
Scaling filters change the enlarged game picture behind F10 immediately.
**Cancel** restores the previous filter. At original size their differences
can be small. The separate color preview uses its own fixed image scaling.

The first-launch screen starts with **Recommended** choices already selected.
These include controller support, vibration, button hints, the analog pointer,
black menu backgrounds, shorter menu animations and the save browser. DVD movies,
deinterlacing and muted fast-forward are also enabled. You can choose fast-forward
speed, captions and movie colors, or change each checkbox before starting.

For subtitles, choose a font and size, and optionally add a dark background.
You can adjust the background's colour and opacity.

- **On** shows installed subtitles or captions included in the movie.
- **Game preference** follows the original game's caption setting.
- **Off** hides captions.

English editions include dialogue captions in many scenes. German, French and
Spanish editions generally lack dialogue captions. Turning captions on does
not create or translate missing text.

## Saves

Your saves are in the game's `saves` folder. Back up the whole folder when moving
to another computer. Existing saves from older patch versions are kept.

Enable **Save browser with thumbnails** in **F10 → Game**. The main menu's
**Save** and **Load** buttons open separate screens. Choose a slot, optionally
enter a name, then save. Overwriting or deleting a save asks for confirmation.
Hover over a save to see a silent scene preview when available.

Use **Previous** and **Next** to change pages. Pages wrap around. The mouse wheel
and Page Up/Page Down work too. Arrow keys or the controller D-pad move between
slots and buttons. Enter or A confirms, Escape or B goes back, and Tab or X moves
to the next control. LB/RB changes pages.

**Existing save files** on the Load screen lists older saves. **F10 → Tools** also
lets you load from a file or export a save. Quick-saving keeps a backup of the
previous quick-save.

On the DVD edition, supported older CD saves can be loaded through experimental
conversion. A warning explains the limits before loading a separate copy. The
original file stays unchanged. Some PDA history and password counters use defaults,
so check your progress and save to a new file after loading.

## Subtitle packs

Use **F10 → Tools → Export subtitles** to save a ZIP or folder of editable subtitles.
Edit the SRT files with a text editor or subtitle editor and save them as UTF-8.
Keep the exported filenames, folders and `manifest.tsv` together.

Choose **Install subtitles** and select the ZIP or folder, then set captions to
**On**. A new pack replaces the previous pack without changing the original movies.
To share your pack, ZIP the exported files and folders together.
To remove it, close the game and delete the game's `subtitles` folder.

For translations, export from the edition you want to play and check the wording
and timing against its audio. The patch doesn't include translations.

## Developer tools

Press **Ctrl+F11**, use **F10 → Tools**, or right-click the game's title bar and
choose **Developer tools**.

- **Live movies** shows clips currently open in the game.
- **Library** lets you browse clips, filter by place or captions, and search text.
- **Play preview** plays a clip independently of the game.
- **Subtitles** lets you edit captions, set their timing and move between clips.
  Edits appear in the preview as you type. **Save** applies them without restarting.
- **Your label** and **Your notes** let you describe clips in your own words.
- **Game state** shows the current game values. **Show interaction targets** marks
  places the player can interact with.

Some files contain several still images. Their preview shows the image selected
by the game. Sound-only captions are not supported yet.
For game structures and file details, see [the technical reference](../src/game/README.md).

## Bug reports

Use **F10 → Tools → Save report ZIP**. You can include a save to help reproduce
an issue. Nothing is uploaded automatically. Reports can contain local file
paths, so check them before sharing. Logs are kept in the game's `logs` folder.
