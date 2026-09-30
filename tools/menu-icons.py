#!/usr/bin/env python3
"""Generate native alpha masks from the menu SVG icons.
Requires fonttools==4.60.1 and Pillow==11.3.0.
"""
from pathlib import Path
import xml.etree.ElementTree as ET
from fontTools.pens.basePen import BasePen
from fontTools.svgLib.path import parse_path
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SIZE = 18


class Outlines(BasePen):
    def __init__(self):
        super().__init__(None)
        self.contours = []
        self.row = []

    def _moveTo(self, point):
        self.row = [point]

    def _lineTo(self, point):
        self.row.append(point)

    def _curveToOne(self, first, second, end):
        begin = self._getCurrentPoint()
        for step in range(1, 17):
            t = step / 16
            self.row.append(tuple((1-t)**3 * begin[k] + 3*(1-t)**2*t * first[k]
                                 + 3*(1-t)*t*t * second[k] + t**3 * end[k]
                                 for k in (0, 1)))

    def _closePath(self):
        self.contours.append(self.row)
        self.row = []

    def _endPath(self):
        self._closePath()


def mask(name):
    svg = ET.parse(ROOT / "assets/menu" / f"{name}.svg").getroot()
    _, _, width, height = map(float, svg.attrib["viewBox"].split())
    scale = SIZE * 8 / max(width, height)
    size = SIZE * 8
    winding = [0] * (size * size)
    for path in svg.findall("{http://www.w3.org/2000/svg}path"):
        pen = Outlines()
        parse_path(path.attrib["d"], pen)
        for points in pen.contours:
            area = sum(a[0]*b[1] - b[0]*a[1]
                       for a, b in zip(points, points[1:] + points[:1]))
            image = Image.new("L", (size, size))
            ImageDraw.Draw(image).polygon(
                [(x*scale + (size-width*scale)/2, y*scale + (size-height*scale)/2)
                 for x, y in points], fill=1)
            sign = 1 if area >= 0 else -1
            winding = [a + sign*b for a, b in zip(winding, image.getdata())]
    image = Image.new("L", (size, size))
    image.putdata([255 if value else 0 for value in winding])
    return list(image.resize((SIZE, SIZE), Image.Resampling.LANCZOS).getdata())


if __name__ == "__main__":
    output = ["#pragma once", "#include <array>", "#include <cstdint>",
              "// Font Awesome Free 6.7.2 and custom X. See assets/menu/LICENSE.txt.",
              "namespace enhancements::menu_icons {",
              f"inline constexpr unsigned size = {SIZE};"]
    for name in ("floppy-disk", "folder-open", "comment", "x-mark", "gear", "bars"):
        pixels = mask(name)
        output.append(f"inline constexpr std::array<std::uint8_t, {SIZE * SIZE}> {name.replace('-', '_')}{{{{")
        for index in range(0, len(pixels), SIZE):
            output.append("    " + ", ".join(map(str, pixels[index:index + SIZE])) + ",")
        output.append("}};")
    output.append("}")
    (ROOT / "src/enhancements/ui/menu_icons.h").write_text("\n".join(output) + "\n")
