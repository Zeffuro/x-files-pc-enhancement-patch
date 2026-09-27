"""Render localized menu labels using the artwork's Industria typeface."""

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont


LABELS = {"en": "Tweaks", "de": "Extras", "fr": "Réglages", "es": "Ajustes", "it": "Modifiche", "ja": "調整"}
DESCRIPTIONS = {
    "en": "Opens the enhancement\nconfiguration window",
    "de": "Öffnet die Einstellungen\nfür Erweiterungen",
    "fr": "Ouvre les réglages\ndes améliorations",
    "es": "Abre la configuración\nde las mejoras",
    "it": "Apre le impostazioni\ndelle migliorie",
    "ja": "拡張機能の設定画面を\n開きます",
}


def lettering(value, font_path, japanese):
    font = ImageFont.truetype(str(font_path), 56 if japanese else 64)
    spacing = 8 if japanese else 13
    widths = [font.getlength(char) for char in value]
    width = round(sum(widths) + spacing * (len(value) - 1))
    mask = Image.new("L", (width + 8, 70))
    draw = ImageDraw.Draw(mask)
    x = 4
    for char, advance in zip(value, widths):
        draw.text((x, 2), char, font=font, fill=255)
        x += advance + spacing
    mask = mask.crop(mask.getbbox())
    if mask.width > 286:
        mask = mask.resize((286, mask.height), Image.Resampling.LANCZOS)
    result = Image.new("L", (336, 86))
    result.paste(mask, (311 - mask.width, (86 - mask.height) // 2))
    return result


def description(value, font_path):
    font = ImageFont.truetype(str(font_path), 25)
    mask = Image.new("L", (336, 86))
    draw = ImageDraw.Draw(mask)
    bounds = draw.multiline_textbbox((0, 0), value, font=font, spacing=3)
    width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    if width > 312:
        font = ImageFont.truetype(str(font_path), int(25 * 312 / width))
        bounds = draw.multiline_textbbox((0, 0), value, font=font, spacing=3)
        width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    draw.multiline_text((324 - width - bounds[0], (86 - height) // 2 - bounds[1]),
                        value, font=font, fill=255, spacing=3, align="center")
    image = Image.composite(Image.new("RGB", mask.size, (90, 153, 185)),
                            Image.new("RGB", mask.size), mask)
    halo = mask.filter(ImageFilter.GaussianBlur(2.4))
    return ImageChops.add(image, Image.composite(Image.new("RGB", mask.size, (15, 36, 50)),
                                               Image.new("RGB", mask.size), halo))


def render(mask, hover):
    sprite = Image.new("RGB", (336, 86 * 21))
    edge = ImageChops.subtract(mask.filter(ImageFilter.MaxFilter(5)), mask)
    for frame in range(21):
        intensity = frame / 20
        image = Image.new("RGB", mask.size)
        for radius, strength in ((6, 0.75), (2.4, 0.6)):
            halo = mask.filter(ImageFilter.GaussianBlur(radius))
            halo = halo.point(lambda v: round(v * (strength + intensity * 0.45)))
            image = ImageChops.add(image, Image.composite(
                Image.new("RGB", mask.size, (46, 105, 140)), Image.new("RGB", mask.size), halo))
        image = Image.composite(Image.new("RGB", mask.size, (33, 75, 100)), image, edge)
        image = Image.composite(Image.new("RGB", mask.size, (1, 5, 8)), image, mask)
        transition = intensity * intensity * (3 - 2 * intensity)
        image = Image.blend(image.filter(ImageFilter.GaussianBlur(0.8)), hover, transition)
        sprite.paste(image, (0, frame * 86))
    return sprite


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--latin-font", type=Path, required=True)
    parser.add_argument("--japanese-font", type=Path, required=True)
    parser.add_argument("--description-font", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for language, value in LABELS.items():
        japanese = language == "ja"
        mask = lettering(value, args.japanese_font if japanese else args.latin_font, japanese)
        hover = description(DESCRIPTIONS[language],
                            args.japanese_font if japanese else args.description_font)
        filename = "tweaks.bmp" if language == "en" else f"tweaks-{language}.bmp"
        render(mask, hover).save(args.output / filename)


if __name__ == "__main__":
    main()
