# Developer tools

Press **Ctrl+F11**, use **F10 → Tools**, or right-click the game's title bar and
choose **Developer tools**.

## Movies and subtitles

- **Live movies** shows clips currently open in the game.
- **Library** lets you browse clips, filter by place or captions, and search text.
  Expand the tree below the preview to see clip details, authoring labels and playback information.
- **Play preview** plays a clip independently of the game.
- **Subtitles** lets you edit captions, set their timing and move between clips.
  Edits appear in the preview as you type. **Save** applies them without restarting.
- **Your label** and **Your notes** let you describe clips in your own words.

## Game state

**Game state** shows copied game values, named story variables and inventory.
Search the variable list, filter by source or registration, and select a row
to see its full name, value and type. **Ctrl+F** focuses search and **Ctrl+C**
copies the selected row. Use **Nonzero only** to hide zero values.
Use **Reset baseline**, then **Changed only** to find values changed since then.

Clear **Live updates** to hold the snapshot, then use **Refresh snapshot** to
capture it again. Variables named `WhereAreWe` keep their original names and
raw values. They do not identify your current location.

**Show interaction targets** marks places the player can interact with, including
while the visible game is unfocused.

### Watches and history

**Pin variable** keeps a variable in the **Watched only** filter through searches,
sorting and inspector reopening. Watches clear when the native game session changes.

**Capture history (this session)** records up to 512 recent observations, with
old and new values, time and available writer information. Capture starts off
and stops when you close the inspector. Filter history to the selected or watched
variables, select an entry for its details, or use **Copy history** to export it.

Native action entries identify a captured action ID and operation. They cover
cached targets and can include preparatory writes within that action. Inspector
edits are labeled separately. Changes detected between refreshed snapshots have
an unknown writer, and intermediate writes can be missed.

### Editing values

To edit a supported state variable, clear **Live updates**, select
**Enable editing (this session)**, enter a decimal value or ASCII character and apply it.
Enter `A` or `'A'` for the letter A. Quote digits and punctuation, such as `'1'`
for character code 49. Plain `1` sets the number 1. Printable ASCII equivalents
appear in the selected variable's details.
Editing changes the running game and may affect progress. Stale values require
a fresh snapshot before they can be changed.

## Database browser

**Database & assets** browses database records, files, media labels and native reads.
**Database tables** and **Asset files** on the left choose what you are browsing.
Record types and counts stay visible on the left. Select a type to browse its records
and read what it is used for. **Database overview** restores the complete list and file overview.

Select a record, then inspect **Overview**, **Fields**, **Links** or **Raw bytes**.
**Fields** puts values beside explanations. Select a field to read the full value
and meaning below the table. Unknown meanings are identified. **Copy table** copies
the displayed fields for pasting into a spreadsheet.

Scroll through the complete list. Search and sorting cover all records in the chosen view.
Each view remembers its search, filter, selection and detail tab.
Press **Ctrl+F** within the browser to focus search and select the current query.
Double-click a link or press **Enter** to follow it. **Back** restores the previous view and record.

Choose **Asset files** to browse installed files and referenced missing files. **Open preview**
opens supported media in the Library. **Auto-refresh** updates cached fields every
two seconds. Use **Refresh** to rescan files or before and after a game action,
then select **Changed variables** to compare values. Database browsing is read-only.

**Database tables** also exposes definitions that the game has not cached. These show
saved definitions and decoded fields rather than current runtime values.
Select an action or trigger list to see its ordered IDs on **Overview** and follow
available records on **Links**.
Stored actions show their encoded conditions and supported Statements. Search
their operands or follow available variable definitions on **Links**.

## Standalone browser

The separate `xfiles-devtools-VERSION-windows-x86.zip` release download includes
the browser and its libraries. Extract the complete ZIP into a writable folder
and open **xfiles-devtools.exe**. You need your own game files.
Live variables, watches, history and subtitle editing use the in-game tools.

To browse without running the game, open **xfiles-devtools.exe** from the patch folder.
Choose **Open folder...** for extracted assets, even without an HDB, or **Open file...**
for an HDB, GAM, saved game (`.x`), PFF or individual asset. You can also drop a folder
or file onto the executable. Both pickers start in the current source folder.
The standalone browser includes stored records, media labels, text candidates and assets.
Select a text candidate and open **Text** to read its source with line breaks.
Candidates can include incomplete HTML or unused text. They are unverified fragments.
Choose **Assets** to browse game media, data, fonts and localization DLLs. Use the format
filter or search a filename. Tabs follow the selection. XT text files have a **Read text**
button and a **Text** tab. **Show font** displays a sample of a TTR or TTF font.
GAM files and saved games show stored records and variable names and values.
Choose **Assets → Localization**, select a DLL and choose **Show strings** to browse
its text. Use the filter in the **Strings** tab to find text, a resource ID or a
language ID. Select a row to read it or choose **Copy full string**. Unusually long
strings have a shortened preview. Copied text includes the complete string, with
control characters shown as escapes.
**Raw bytes** shows the start of any asset file.
Select a `.HOT` asset to inspect its clickable rectangles and action IDs, or follow
its matching media file on **Links**. **Open preview** starts supported media paused.
For `.PFF` files, use **Assets → Image archives**, select a file and choose **Open archive**.
Opening a PFF directly also opens its image browser. **Export asset...** saves PNG images
or original PICT data. **Import asset...** replaces the selected entry in memory.
**Save archive copy...** saves to a new filename and keeps the installed archive unchanged.
PNG imports currently need an opaque background.

## Media previews

Use **Fit** to fill the preview area or **Actual size** to view one image pixel per screen
pixel. Larger images can be scrolled. NMV previews include **Previous**, **Next** and a
frame list for browsing their still images. Choose a video track to see its frames.
Selecting a frame pauses playback.
Sound-only captions are not supported yet.
For game structures and file details, see
[the technical reference](https://github.com/Zeffuro/x-files-pc-enhancement-patch/blob/main/src/game/README.md).
