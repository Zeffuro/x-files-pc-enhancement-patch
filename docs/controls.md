# Controls

## Keyboard

**F10** opens settings. **Alt+Enter** switches between windowed and borderless.
**F5** quick-saves during exploration. **F9** quick-loads during exploration or
from the main menu. Close movies, conversations and device screens first.
Loading discards unsaved progress.

## Controller

Controllers must be available through XInput. The PlayStation labels below show
the matching button positions; they don't mean native PlayStation support.

| Xbox / PlayStation | Action |
|---|---|
| A / Cross | Use or confirm |
| X / Square | Examine / right-click |
| B / Circle | Switch inventory or device focus; close a conversation |
| Y / Triangle | Return to the game view |
| Menu / Start | Open the menu; resume the game |
| View / Select | Skip a movie, when enabled in the game options |
| D-pad | Select menu, inventory, emotion or device controls |
| Left stick | Move the pointer |
| Right stick click / R3 | Switch between conversation choices and evidence icons |
| LB or RB + left/right | Select exits |
| RT + left/right | Select scene hotspots |
| LT | Equip the gun, when owned, and return the pointer to the scene |
| LT + D-pad left/right | Select action targets (experimental) |

The left stick moves the pointer by default. F10 can switch it to selecting
controls or enable the spring-centred pointer.

LT uses the gun's normal inventory action. A fires; entering the inventory
holsters it. The stick still aims while LT is held.
Combat and some menu controls are unfinished on both CD and DVD.

In conversations, up/down selects a response. Left/right or LB/RB switches
Talk/History. R3 switches to the evidence icons: select one with the D-pad and
press A to use it. Press R3 again to return to dialogue. Keyboard Tab switches
the same groups. Right-click closes the conversation.

On the PDA or workstation, B switches between toolbar/sidebar and content.
Y goes back. Press A on a login, search or save-name field to open the on-screen
keyboard. Use the D-pad to select a key, A to type, X to delete, and Y or Start to
close it. Changes go straight into the game field. A physical keyboard and mouse
still work.

## Saves and reports

Quick saves are stored in the game folder as `QUICKSAVE.x`. The previous
successful save is kept as `QUICKSAVE.previous.x`. Normal save slots are separate.

**F10 → Tools → Save to file** exports a save during exploration.
**Load from file** opens a PC `.x` save during exploration or from the main menu.
These leave your quick-save and normal slots alone. Choose a new filename when
exporting; existing saves aren't overwritten. Loading discards unsaved progress.
Saves include inventory and story state; the patch doesn't edit those flags.

Tools also opens the logs folder or saves a report ZIP, with an optional saved
game attached. The launcher offers the same report after a crash. Nothing is
uploaded. Check the ZIP before sharing it: logs may contain local file paths.

## Settings

Open **Tweaks** on the main menu or press **F10** for display, audio, controller
and subtitle settings. Gamepad support is on by default. Focus highlights appear
when using a controller and disappear when you move or click the mouse. F10 also
has Always and Off options.

The workstation login shortcut and menu animation skip are off by default.
The animation skip covers the logo and header. Game fonts come from your game
files. Subtitles default to Typist at the credits' base size.

Both 4:3 and 16:9 window sizes keep the original image proportions. 16:9 adds
side bars; it doesn't rearrange the interface. Direct3D 9 supports nearest-neighbour,
bilinear, bicubic and Lanczos scaling. Bicubic is the default.
