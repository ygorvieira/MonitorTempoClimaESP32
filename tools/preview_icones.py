#!/usr/bin/env python3

import argparse
import math
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Pillow nao instalado. Instale com: pip install pillow", file=sys.stderr)
    raise SystemExit(1)

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "icones.h"

FONT_CANDIDATES = [
    Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
    Path("/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf"),
    Path("C:/Windows/Fonts/consola.ttf"),
    Path("C:/Windows/Fonts/cour.ttf"),
]

CONTACT_COLUMNS = 4
CONTACT_SCALE = 8
SCREEN_SCALE = 6


def find_font():
    for path in FONT_CANDIDATES:
        if path.exists():
            return path
    return None


FONT_PATH = find_font()


def load_font(size):
    if FONT_PATH is not None:
        return ImageFont.truetype(str(FONT_PATH), size)
    return ImageFont.load_default()


def parse_icons():
    text = HEADER.read_text(encoding="utf-8")
    pattern = re.compile(
        r"static\s+const\s+unsigned\s+char\s+(\w+)\s*\[\s*(\d+)\s*\]\s*PROGMEM\s*=\s*\{(.*?)\}\s*;",
        re.S,
    )

    icons = []
    for match in pattern.finditer(text):
        name = match.group(1)
        count = int(match.group(2))
        body = match.group(3)
        data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))

        if len(data) != count:
            raise ValueError(
                f"{name}: esperados {count} bytes, encontrados {len(data)}"
            )

        if name.endswith("_pequeno") or name.endswith("_pequena"):
            width = height = 8
        else:
            width = height = 16

        if len(data) * 8 != width * height:
            raise ValueError(
                f"{name}: {len(data)} bytes nao correspondem a {width}x{height}"
            )

        icons.append((name, data, width, height))

    if not icons:
        raise ValueError(f"Nenhum icone encontrado em {HEADER}")

    return icons


def render_bitmap(data, width, height):
    image = Image.new("1", (width, height), 0)
    pixels = image.load()
    stride = (width + 7) // 8

    for y in range(height):
        for x in range(width):
            byte = data[y * stride + x // 8]
            if (byte >> (x % 8)) & 1:
                pixels[x, y] = 1

    return image


def print_ascii(name, data, width, height):
    print(f"\n{name} ({width}x{height})")
    stride = (width + 7) // 8
    for y in range(height):
        line = ""
        for x in range(width):
            byte = data[y * stride + x // 8]
            line += "#" if (byte >> (x % 8)) & 1 else "."
        print(line)


def make_contact_sheet(icons, outdir):
    cell_width = 16 * CONTACT_SCALE + 24
    cell_height = 16 * CONTACT_SCALE + 44
    rows = math.ceil(len(icons) / CONTACT_COLUMNS)
    title_height = 36

    canvas = Image.new(
        "RGB",
        (CONTACT_COLUMNS * cell_width, title_height + rows * cell_height),
        "black",
    )
    draw = ImageDraw.Draw(canvas)
    title_font = load_font(22)
    label_font = load_font(14)

    draw.text((12, 8), "Icones - Clima ESP32", fill="white", font=title_font)

    for index, (name, data, width, height) in enumerate(icons):
        column = index % CONTACT_COLUMNS
        row = index // CONTACT_COLUMNS
        x = column * cell_width + 12
        y = title_height + row * cell_height + 8

        draw.rectangle(
            [x - 2, y - 2, x + 16 * CONTACT_SCALE + 2, y + 16 * CONTACT_SCALE + 2],
            outline="#444444",
        )

        icon_image = render_bitmap(data, width, height).convert("RGB")
        icon_image = icon_image.resize(
            (width * CONTACT_SCALE, height * CONTACT_SCALE),
            Image.NEAREST,
        )

        icon_x = x + (16 * CONTACT_SCALE - width * CONTACT_SCALE) // 2
        icon_y = y + (16 * CONTACT_SCALE - height * CONTACT_SCALE) // 2
        canvas.paste(icon_image, (icon_x, icon_y))

        label = name[6:] if name.startswith("icone_") else name
        draw.text(
            (x, y + 16 * CONTACT_SCALE + 6),
            label,
            fill="white",
            font=label_font,
        )

    path = outdir / "icones.png"
    canvas.save(path)
    return path


def paste_icon(canvas, icon, x, y, scale):
    _, data, width, height = icon
    image = render_bitmap(data, width, height).convert("RGB")
    image = image.resize((width * scale, height * scale), Image.NEAREST)
    canvas.paste(image, (x * scale, y * scale))


def draw_text(draw, x, y, text, size, scale):
    base_size = {1: 9, 2: 20, 4: 40}.get(size, size * 8)
    font = load_font(base_size * scale)

    try:
        draw.text(
            (x * scale, y * scale),
            text,
            fill="white",
            font=font,
            anchor="lt",
        )
    except (TypeError, ValueError):
        draw.text((x * scale, y * scale), text, fill="white", font=font)


def make_climate_screen(
    outdir,
    filename,
    weather_icon,
    time_icon,
    time_text,
    temperature,
    condition,
    sensation,
    wind,
):
    scale = SCREEN_SCALE
    canvas = Image.new("RGB", (128 * scale, 64 * scale), "black")
    draw = ImageDraw.Draw(canvas)

    paste_icon(canvas, weather_icon, 112, 0, scale)
    paste_icon(canvas, time_icon, 0, 56, scale)

    draw_text(draw, 0, 0, "Rio de Janeiro", 1, scale)
    draw_text(draw, 0, 10, temperature, 2, scale)
    draw_text(draw, 0, 28, condition, 1, scale)
    draw_text(draw, 0, 38, sensation, 1, scale)
    draw_text(draw, 0, 48, wind, 1, scale)
    draw_text(draw, 10, 56, time_text, 1, scale)

    path = outdir / filename
    canvas.save(path)
    return path


def make_clock_screen(outdir, filename, clock_icon, clock_text):
    scale = SCREEN_SCALE
    canvas = Image.new("RGB", (128 * scale, 64 * scale), "black")
    draw = ImageDraw.Draw(canvas)

    paste_icon(canvas, clock_icon, 56, 2, scale)
    draw_text(draw, 4, 22, clock_text, 4, scale)

    path = outdir / filename
    canvas.save(path)
    return path


def make_screen_sheet(screens, outdir):
    gap = 20
    label_height = 32
    first_image = Image.open(screens[0][1])
    width, height = first_image.size

    canvas = Image.new(
        "RGB",
        (2 * width + 3 * gap, 2 * (height + label_height) + 3 * gap),
        "black",
    )
    draw = ImageDraw.Draw(canvas)
    font = load_font(24)

    for index, (label, path) in enumerate(screens):
        column = index % 2
        row = index // 2
        x = gap + column * (width + gap)
        y = gap + row * (height + label_height + gap)
        draw.text((x, y), label, fill="white", font=font)
        with Image.open(path) as image:
            canvas.paste(image, (x, y + label_height))

    path = outdir / "telas.png"
    canvas.save(path)
    return path


def main():
    parser = argparse.ArgumentParser(
        description="Gera previsoes PNG dos icones e das telas do Clima_ESP32."
    )
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT / "preview",
        help="Diretorio de saida das imagens.",
    )
    parser.add_argument(
        "--no-ascii",
        action="store_true",
        help="Nao imprime os icones em ASCII no terminal.",
    )
    args = parser.parse_args()

    outdir = args.outdir
    outdir.mkdir(parents=True, exist_ok=True)

    icons = parse_icons()
    by_name = {name: (name, data, width, height) for name, data, width, height in icons}

    if not args.no_ascii:
        for name, data, width, height in icons:
            print_ascii(name, data, width, height)

    contact_path = make_contact_sheet(icons, outdir)

    screens = []

    screens.append(
        (
            "Clima - dia",
            make_climate_screen(
                outdir,
                "tela_clima_dia.png",
                by_name["icone_parcial"],
                by_name["icone_sol_pequeno"],
                "Atual 13:45:30",
                "28.9C",
                "Parc. nublado",
                "Sens 34.0C  Umi 75%",
                "Vento 15.7 km/h",
            ),
        )
    )

    screens.append(
        (
            "Clima - noite",
            make_climate_screen(
                outdir,
                "tela_clima_noite.png",
                by_name["icone_lua"],
                by_name["icone_lua_pequena"],
                "Atual 22:15:07",
                "21.4C",
                "Ceu limpo",
                "Sens 21.0C  Umi 82%",
                "Vento 8.3 km/h",
            ),
        )
    )

    screens.append(
        (
            "Relogio - dia",
            make_clock_screen(
                outdir,
                "tela_relogio_dia.png",
                by_name["icone_sol"],
                "14:35",
            ),
        )
    )

    screens.append(
        (
            "Relogio - noite",
            make_clock_screen(
                outdir,
                "tela_relogio_noite.png",
                by_name["icone_lua"],
                "22:15",
            ),
        )
    )

    sheet_path = make_screen_sheet(screens, outdir)

    print(f"\nContato de icones: {contact_path}")
    print(f"Telas simuladas: {sheet_path}")
    for _, path in screens:
        print(f"  {path}")


if __name__ == "__main__":
    main()
