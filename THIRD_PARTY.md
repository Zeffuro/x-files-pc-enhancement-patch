# Licenses

The patch's own code is covered by [MIT](LICENSE). This does not relicense
its dependencies or any part of *The X-Files Game*. The patch is unofficial;
you need your own game files.

## cnc-ddraw

[cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) is used under MIT.
The pinned revision is in `cmake/ddraw.cmake`. Our changes are in
`patches/cnc-ddraw-desktop.patch`; the source checkout retains the upstream
notice in `LICENSES/cnc-ddraw.txt`. Release packages include `cnc-ddraw.LICENSE`,
with the upstream notice and the notices for its Detours and LodePNG code.

## FFmpeg

This software uses libraries from [FFmpeg](https://ffmpeg.org/) under
LGPL-2.1-or-later. The DLLs are built without GPL or nonfree components and
are loaded separately from the patch. See `FFmpeg.LICENSE` in the release ZIP
or installed game folder for the license text.

The matching source archive and build script are in `source/ffmpeg` in the
[patch release ZIP](https://github.com/Zeffuro/x-files-pc-enhancement-patch/releases).
Setup leaves those development files in the extracted patch folder; it copies
the notices into the game folder. Keep the matching source with the DLLs when
redistributing a patch package. The pinned version and source checksum are in
`tools/build-ffmpeg.sh`.

## zlib

[zlib](https://zlib.net/) is statically linked into the patch under the zlib
license. The pinned version is in `cmake/zlib.cmake`; `zlib.LICENSE` in the
release ZIP and installed game folder contains its notice.
