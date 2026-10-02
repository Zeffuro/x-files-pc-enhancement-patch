# The X-Files developer tools

A portable browser for databases, saves and assets from *The X-Files Game*.
You need your own game files. The game does not need to be running.

Extract the complete `xfiles-devtools-VERSION-windows-x86.zip` into a writable
folder and open **xfiles-devtools.exe**. Keep its DLLs beside the executable.
Choose **Open folder...** for extracted assets or **Open file...** for a database,
save, image archive or individual asset. You can also drop a file or folder onto
the executable.

Browse records and their links, inspect fields and raw bytes, read text and
localization strings, and preview images, fonts, movies and audio. Image archives
can be exported or edited, then saved as a separate copy.
See `docs/developer-tools.md` in this ZIP or the
[developer tools guide](https://github.com/Zeffuro/x-files-pc-enhancement-patch/blob/main/docs/developer-tools.md)
for the browser controls.

Live game state, variable editing, watches, history and the subtitle editor are
available through the enhancement patch's in-game tools. Press **Ctrl+F11** while
playing with the patch. Use the `xfiles-enhancement-VERSION-windows-x86.zip` download
to install or update the game patch.

To update this browser, close it and extract the newer tools ZIP into its folder.
To remove it, delete that folder. It can also be tried with Wine on Linux.

The tools use the MIT license. See `LICENSE` and `THIRD_PARTY.md` in this ZIP.
Matching FFmpeg source and build instructions are included in `source/ffmpeg`.
