"""Export installed Save/Load PICT art for local visual inspection."""

import argparse
import html
from io import BytesIO
from pathlib import Path
import struct

from PIL import Image


ASSETS = {
    "savegameMAIN": 694,
    "loadBUTTON": 695,
    "loadTOPpatch": 696,
    "ScancelRO": 697,
    "SloadRO": 698,
    "SsaveRO": 699,
    "HIGHLIGHT": 701,
    "dialog-up": 245,
    "dialog-folder-history": 246,
    "dialog-folder-talk": 247,
    "dialog-scrollbar": 248,
    "dialog-thumb": 249,
    "dialog-down": 250,
    "options-main": 395,
    "options-mask": 396,
    "SysCancel": 605,
    "SysNo": 606,
    "SysYes": 607,
    "SysOk": 608,
    "SysQuit": 609,
    "dialog-history-patch": 711,
}


def unpack(data: bytes, expected: int) -> bytes:
    result = bytearray()
    at = 0
    while at < len(data):
        control = data[at]
        at += 1
        if control == 128:
            continue
        count = control + 1 if control < 128 else 257 - control
        if control < 128:
            result.extend(data[at:at + count])
            at += count
        else:
            result.extend(data[at:at + 1] * count)
            at += 1
    if len(result) != expected:
        raise ValueError(f"Packed row is {len(result)} bytes, expected {expected}")
    return bytes(result)


def pict_image(data: bytes) -> Image.Image:
    marker = data.find(b"\x00\x9a")
    jpeg = data.find(b"\xff\xd8\xff")
    if jpeg >= 0 and (marker < 0 or jpeg < marker):
        return Image.open(BytesIO(data[jpeg:])).convert("RGBA")
    if marker < 0:
        raise ValueError("No supported bitmap in PICT")
    at = marker + 2 + 4
    row_bytes = struct.unpack_from(">H", data, at)[0] & 0x3fff
    at += 2
    top, left, bottom, right = struct.unpack_from(">hhhh", data, at)
    at += 8
    _, packing = struct.unpack_from(">HH", data, at)
    at += 4 + 12
    pixel_type, depth, components, component_size = struct.unpack_from(">HHHH", data, at)
    at += 8 + 12 + 8 + 8 + 2
    width, height = right - left, bottom - top
    if not (0 < width <= 2048 and 0 < height <= 2048 and pixel_type == 16
            and depth == 32 and components in (3, 4) and component_size == 8
            and packing == 4 and row_bytes >= width * 4):
        raise ValueError("Unsupported PICT direct bitmap")
    output = bytearray(width * height * 4)
    for y in range(height):
        length = struct.unpack_from(">H" if row_bytes > 250 else ">B", data, at)[0]
        at += 2 if row_bytes > 250 else 1
        row = unpack(data[at:at + length], width * components)
        at += length
        for x in range(width):
            pixel = 4 * (y * width + x)
            start = width if components == 4 else 0
            output[pixel:pixel + 4] = bytes((row[start + x], row[start + width + x],
                                               row[start + 2 * width + x],
                                               row[x] if components == 4 else 255))
    return Image.frombytes("RGBA", (width, height), bytes(output))


def archive_entry(archive: bytes, index: int) -> bytes:
    if archive[:4] != b"PFF ":
        raise ValueError("Invalid PFF signature")
    count = struct.unpack_from("<I", archive, 4)[0]
    if index >= count:
        raise ValueError(f"Missing PFF entry {index}")
    begin, end = struct.unpack_from("<II", archive, 8 + index * 4)
    if not (8 + (count + 1) * 4 <= begin < end <= len(archive)):
        raise ValueError(f"Invalid PFF entry {index}")
    return archive[begin:end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", action="append", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cards = []
    for game in args.game:
        archive = (game / "X.PFF").read_bytes()
        group = game.name.rsplit("-", 2)[-2] + "-" + game.name.rsplit("-", 1)[-1]
        for name, index in ASSETS.items():
            image = pict_image(archive_entry(archive, index))
            filename = f"{group}-{index}-{name}.png"
            image.save(args.out / filename)
            cards.append((group, name, index, image.size, filename))
    sections = []
    for group, name, index, size, filename in cards:
        sections.append(
            f'<figure><a href="{html.escape(filename)}"><img src="{html.escape(filename)}" '
            f'alt="{html.escape(group + " " + name)}"></a><figcaption>'
            f'{html.escape(group)}: {html.escape(name)} (PFF {index}, {size[0]} × {size[1]})'
            "</figcaption></figure>"
        )
    page = ("<!doctype html><meta charset=utf-8><title>Installed Save/Load art</title>"
            "<style>body{background:#1c2228;color:#edf4fa;font:16px system-ui}"
            "main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0;padding:12px;"
            "background:#303a44}img{max-width:640px;max-height:480px;image-rendering:auto}"
            "figcaption{margin-top:8px}</style><h1>Installed Save/Load art</h1><main>"
            + "\n".join(sections) + "</main>")
    (args.out / "index.html").write_text(page, encoding="utf-8")


if __name__ == "__main__":
    main()
