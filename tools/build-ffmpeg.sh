#!/usr/bin/env bash
set -euo pipefail

# Run in an x86 MSVC tools environment, with MSYS make and bash on PATH.
script=$(realpath "$0")
cache=$(cygpath -au "${1:?Supply a cache directory without spaces}")
prefix=$(cygpath -au "${2:?Supply an installation directory without spaces}")
case "$cache:$prefix" in
    *' '*) echo 'FFmpeg build paths must not contain spaces.' >&2; exit 1 ;;
esac
if [ "${VSCMD_ARG_TGT_ARCH:-}" != x86 ]; then
    echo 'Select the x86 MSVC target before building FFmpeg.' >&2
    exit 1
fi
export PATH="$(cygpath -u "${VCToolsInstallDir:?Run from an MSVC tools prompt}")/bin/Hostx64/x86:$PATH"

version=9.0.2
archive="ffmpeg-$version.tar.xz"
checksum=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
mkdir -p "$cache" "$prefix"
cd "$cache"
download=$(mktemp "$cache/.ffmpeg-download.XXXXXX")
build=$(mktemp -d "$cache/.ffmpeg-build.XXXXXX")
trap 'cd "$cache"; rm -f "$download"; rm -rf "$build"' EXIT
if [ ! -f "$archive" ]; then
    curl --fail --location --retry 3 --output "$download" "https://ffmpeg.org/releases/$archive"
    printf '%s  %s\n' "$checksum" "$download" | sha256sum --check
    mv "$download" "$archive"
fi
printf '%s  %s\n' "$checksum" "$archive" | sha256sum --check
tar -xf "$archive" -C "$build"
cd "$build/ffmpeg-$version"

./configure --toolchain=msvc --arch=x86 --target-os=win32 \
    --disable-asm --disable-autodetect --disable-programs --disable-doc --disable-debug \
    --disable-everything --disable-avdevice --disable-avfilter --disable-network \
    --disable-static --enable-shared --disable-gpl --disable-nonfree \
    --enable-decoder=qdmc,qdm2,pcm_s8,pcm_u8,pcm_s16be,pcm_s16le,pcm_dvd,adpcm_ima_qt,cinepak,mjpeg,mpeg2video,rpza \
    --enable-demuxer=mov,mpegps --enable-parser=mpegvideo,mpegaudio --enable-protocol=file \
    --prefix="$prefix"
"${MAKE:-make}" -r -j8
"${MAKE:-make}" -r install

cp COPYING.LGPLv2.1 "$prefix/FFmpeg.LICENSE"
mkdir -p "$prefix/source"
cp "$cache/$archive" "$prefix/source/"
cp "$script" "$prefix/source/build-ffmpeg.sh"
