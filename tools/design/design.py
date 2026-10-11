"""Single source of truth for the esp-oralb pixel design.

Defines the fonts (big 3x5 block digits + Press Start 2P 8x8 text with Cyrillic),
pixel-art faces and weather icons, the teeth-zone chart and the screen layouts
for the 320x240 landscape display. Renders mockups to PNG; the firmware assets
are generated from the same data.

    uv run --no-project --with pillow python design.py mock OUTDIR
    uv run --no-project --with pillow python design.py gen      # -> include/gfx_assets.h
    uv run --no-project --with pillow python design.py golden   # -> test/data/golden_*.rle
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).parent
W, H = 320, 240

# -- palette ---------------------------------------------------------------------
BG = (0, 0, 0)
FG = (214, 214, 214)  # main text: grey close to white
DIM = (110, 110, 110)  # secondary text
OFF = (48, 48, 48)  # empty squares
ACCENT = (42, 212, 224)  # cyan: current zone, faces
WARN = (235, 70, 60)  # too much pressure
SUN = (242, 193, 78)

# -- big digits: 3x5 blocks (the first terminal design) ----------------------------
BIG = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "001", "001", "001"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
    ":": ("0", "1", "0", "1", "0"),
    "-": ("000", "000", "111", "000", "000"),
    " ": ("0", "0", "0", "0", "0"),
}


def big_width(text: str, px: int) -> int:
    return sum(len(BIG[c][0]) * px for c in text) + px * (len(text) - 1)


def draw_big(img: Image.Image, text: str, x: int, y: int, px: int, color, gap: float = 0.12) -> None:
    """Each font pixel is a px*px square with a thin gap, so the blocks read as squares."""
    d = ImageDraw.Draw(img)
    g = max(1, round(px * gap))
    for ch in text:
        rows = BIG[ch]
        for r, row in enumerate(rows):
            for c, bit in enumerate(row):
                if bit == "1":
                    x0, y0 = x + c * px, y + r * px
                    d.rectangle([x0, y0, x0 + px - 1 - g, y0 + px - 1 - g], fill=color)
        x += len(rows[0]) * px + px


# -- text font: Press Start 2P rendered at its native 8 px grid ----------------------
_PS2P = ImageFont.truetype(str(HERE / "PressStart2P-Regular.ttf"), 8)
TEXT_CHARS = (
    " !\"%'()+,-./0123456789:;?°"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "АБВГҐДЕЄЖЗИІЇЙКЛМНОПРСТУФХЦЧШЩЬЮЯабвгґдеєжзиіїйклмнопрстуфхцчшщьюя’"
)


def glyph(ch: str) -> list[str]:
    """8x8 bitmap rows for one character."""
    im = Image.new("1", (8, 8), 0)
    ImageDraw.Draw(im).text((0, 0), ch, font=_PS2P, fill=1)
    return ["".join("1" if im.getpixel((x, y)) else "0" for x in range(8)) for y in range(8)]


_GLYPHS = {c: glyph(c) for c in TEXT_CHARS}


def text_width(s: str, scale: int) -> int:
    return len(s) * 8 * scale


def draw_text(img: Image.Image, s: str, x: int, y: int, scale: int, color) -> None:
    d = ImageDraw.Draw(img)
    for ch in s:
        for r, row in enumerate(_GLYPHS.get(ch, _GLYPHS["?"])):
            for c, bit in enumerate(row):
                if bit == "1":
                    d.rectangle([x + c * scale, y + r * scale, x + (c + 1) * scale - 1, y + (r + 1) * scale - 1], fill=color)
        x += 8 * scale


def draw_text_c(img, s, cx, y, scale, color) -> None:
    draw_text(img, s, cx - text_width(s, scale) // 2, y, scale, color)


# -- pixel art -----------------------------------------------------------------------
FACE_HAPPY = [
    "................",
    "................",
    "....##....##....",
    "...####..####...",
    "...####..####...",
    "....##....##....",
    "................",
    "................",
    "................",
    "..##........##..",
    "..###......###..",
    "...###....###...",
    "....########....",
    ".....######.....",
    "................",
    "................",
]
FACE_SAD = FACE_HAPPY[:9] + [
    ".....######.....",
    "....########....",
    "...###....###...",
    "..###......###..",
    "..##........##..",
] + ["................"] * 2

# 16x16 weather icons; letters pick the colour: S sun, C cloud, R rain, W snow, B bolt, F fog
_SUN = [
    ".......S........",
    ".......S........",
    "..S.........S...",
    "...S..SSS..S....",
    ".....SSSSS......",
    "....SSSSSSS.....",
    "SS..SSSSSSS..SS.",
    "....SSSSSSS.....",
    ".....SSSSS......",
    "...S..SSS..S....",
    "..S.........S...",
    ".......S........",
    ".......S........",
    "................",
    "................",
    "................",
]
_CLOUD = [
    "................",
    "................",
    "................",
    "................",
    "......CCCC......",
    "....CCCCCCCC....",
    "...CCCCCCCCCC...",
    "..CCCCCCCCCCCCC.",
    ".CCCCCCCCCCCCCCC",
    ".CCCCCCCCCCCCCCC",
    "..CCCCCCCCCCCCC.",
    "................",
    "................",
    "................",
    "................",
    "................",
]


def _overlay(base: list[str], top: list[str]) -> list[str]:
    return ["".join(t if t != "." else b for b, t in zip(br, tr)) for br, tr in zip(base, top)]


def _shift(art: list[str], dx: int, dy: int) -> list[str]:
    rows = ["." * 16] * max(dy, 0) + art[max(-dy, 0):]
    rows = rows[:16] + ["." * 16] * (16 - len(rows))
    return [("." * dx + r)[:16] if dx >= 0 else (r[-dx:] + "." * -dx) for r in rows]


_LOW_CLOUD = _shift(_CLOUD, 0, 1)
def _moon() -> list[str]:
    """Crescent: a disc minus an offset disc, plus three small stars."""
    rows = []
    for y in range(16):
        row = ""
        for x in range(16):
            inside = (x - 6.5) ** 2 + (y - 7.5) ** 2 <= 6.2**2
            bite = (x - 11.0) ** 2 + (y - 4.5) ** 2 <= 5.6**2
            row += "M" if inside and not bite else "."
        rows.append(row)
    for x, y in ((13, 2), (14, 9), (11, 13)):
        rows[y] = rows[y][:x] + "T" + rows[y][x + 1:]
    return rows


_MOON = _moon()

WEATHER = {
    "clear": _SUN,
    "partly": _overlay(_shift(_SUN, -3, -3), _shift(_CLOUD, 1, 3)),
    "cloudy": _shift(_CLOUD, 0, 1),
    "fog": ["................"] * 4 + [
        "..FFFFFFFFFFFF..", "................", "FFFFFFFFFFFFFF..", "................",
        "...FFFFFFFFFFFFF", "................", ".FFFFFFFFFFFF...", "................",
    ] + ["................"] * 4,
    "rain": _overlay(_shift(_CLOUD, 0, -2), ["................"] * 11 + [
        "...R...R...R....", "..R...R...R.....", "................", ".R...R...R......", "................"]),
    "snow": _overlay(_shift(_CLOUD, 0, -2), ["................"] * 11 + [
        "...W....W....W..", "................", ".W....W....W....", "................", "....W....W......"]),
    "storm": _overlay(_shift(_CLOUD, 0, -2), ["................"] * 9 + [
        "........BB......", ".......BB.......", "......BBBB......", "........B.......", ".......B........",
        "......B.........", "................"]),
    # night variants (Open-Meteo is_day == 0); the other icons have no sun in them
    "clear_night": _MOON,
    "partly_night": _overlay(_shift(_MOON, -2, -2), _shift(_CLOUD, 1, 4)),
}
MOON = (232, 222, 170)
WEATHER_COLORS = {"S": SUN, "C": FG, "R": ACCENT, "W": (255, 255, 255), "B": SUN, "F": DIM, "M": MOON, "T": FG}


def draw_art(img, art: list[str], x: int, y: int, px: int, color=None, gap: int = 0) -> None:
    d = ImageDraw.Draw(img)
    for r, row in enumerate(art):
        for c, ch in enumerate(row):
            if ch != ".":
                col = color or WEATHER_COLORS.get(ch, FG)
                d.rectangle([x + c * px, y + r * px, x + (c + 1) * px - 1 - gap, y + (r + 1) * px - 1 - gap], fill=col)


# -- teeth chart: mirror view (the user's left on the left) ----------------------------
# Upper jaw drawn as an arch (front on top), lower jaw as a U (front at the bottom).
# Sides: three vertical strips (outer, chewing, inner) 1 cell wide x 2 tall.
# Front: two horizontal strips (outer, inner) 3 cells wide x 1 tall.
def _side(col0: int, row0: int, left: bool, jaw: str) -> dict[str, list[tuple[int, int]]]:
    order = ("O", "C", "I") if left else ("I", "C", "O")
    side = "L" if left else "R"
    return {f"{jaw}{surf}{side}": [(col0 + i, row0), (col0 + i, row0 + 1)] for i, surf in enumerate(order)}


def teeth_cells() -> dict[str, list[tuple[int, int]]]:
    cells: dict[str, list[tuple[int, int]]] = {}
    # upper jaw: rows 0-3
    cells["UOF"] = [(3, 0), (4, 0), (5, 0)]
    cells["UIF"] = [(3, 1), (4, 1), (5, 1)]
    cells |= _side(0, 2, True, "U") | _side(6, 2, False, "U")
    # lower jaw: rows 5-8 (row 4 is the gap between the jaws)
    cells |= _side(0, 5, True, "L") | _side(6, 5, False, "L")
    cells["LIF"] = [(3, 7), (4, 7), (5, 7)]
    cells["LOF"] = [(3, 8), (4, 8), (5, 8)]
    return cells


TEETH = teeth_cells()
TEETH_COLS, TEETH_ROWS = 9, 9


def draw_teeth(img, x: int, y: int, cell: int, coverage: dict[str, float], current: str | None, gap: int = 2) -> None:
    d = ImageDraw.Draw(img)
    for zone, cs in TEETH.items():
        frac = coverage.get(zone, 0.0)
        if zone == current:
            col = ACCENT
        elif frac >= 1.0:
            col = FG
        elif frac > 0:
            col = DIM
        else:
            col = OFF
        for cx, cy in cs:
            x0, y0 = x + cx * (cell + gap), y + cy * (cell + gap)
            d.rectangle([x0, y0, x0 + cell - 1, y0 + cell - 1], fill=col)


def teeth_size(cell: int, gap: int = 2) -> tuple[int, int]:
    return TEETH_COLS * (cell + gap) - gap, TEETH_ROWS * (cell + gap) - gap


# -- progress: 8 squares, 15 s each -----------------------------------------------------
PROGRESS_STEPS = 8


def draw_progress(img, cx: int, y: int, seconds: int, size: int = 12, gap: int = 6, blink_on: bool = True) -> None:
    d = ImageDraw.Draw(img)
    step = 120 / PROGRESS_STEPS
    done = min(int(seconds // step), PROGRESS_STEPS)
    total = PROGRESS_STEPS * size + (PROGRESS_STEPS - 1) * gap
    x = cx - total // 2
    for i in range(PROGRESS_STEPS):
        if seconds >= 120:
            col = ACCENT
        elif i < done:
            col = FG
        elif i == done:
            col = ACCENT if blink_on else OFF
        else:
            col = OFF
        d.rectangle([x, y, x + size - 1, y + size - 1], fill=col)
        x += size + gap


# -- Ukrainian strings ------------------------------------------------------------------
DAYS = ["Понеділок", "Вівторок", "Середа", "Четвер", "П’ятниця", "Субота", "Неділя"]
MONTHS_GEN = ["січня", "лютого", "березня", "квітня", "травня", "червня",
              "липня", "серпня", "вересня", "жовтня", "листопада", "грудня"]  # fmt: skip
STR = {
    "last": "Остання:",
    "at": "о",
    "great": "Чудово!",
    "short": "Замало",
    "pressure": "Тиснеш!",
    "waiting": "Чекаю щітку...",
    "wifi_setup": "Wi-Fi: esp-oralb-setup",
    "alert_red": "ТРИВОГА",
    "alert_yellow": "ЗАГРОЗА",
    "alert_none": "БЕЗПЕЧНО",  # "you are safe" (user wording; "Відбій" = all clear)
    "alert_city": "Київ",
    "since": "з",
}

# Air-raid siren (siren.pp.ua / ukrainealarm alert state for the configured region)
SIREN = [
    "................",
    ".......AA.......",
    "..A....AA....A..",
    "...A........A...",
    "......AAAA......",
    ".....AAAAAA.....",
    "....AAAAAAAA....",
    "....AA.AAAAA....",
    "....AA.AAAAA....",
    "....AAAAAAAA....",
    "....AAAAAAAA....",
    "...AAAAAAAAAA...",
    "..AAAAAAAAAAAA..",
    "..AAAAAAAAAAAA..",
    "................",
    "................",
]
MISSILE = [  # red: cruise missile flying right, flame behind (user's choice)
    "................",
    "................",
    "................",
    "..AA............",
    "..AAA...........",
    "...AAAAAAAAAA...",
    "OOAAAAAAAAAAAAA.",
    "OOAAAAAAAAAAAAAA",
    "OOAAAAAAAAAAAAA.",
    "...AAAAAAAAAA...",
    "..AAA...........",
    "..AA............",
    "................",
    "................",
    "................",
    "................",
]
DRONE = [  # yellow: Shahed-style drone from above, nose up: nose, delta wing, tip fins, propeller
    "................",
    ".......AA.......",
    "......AAAA......",
    "......AAAA......",
    "......AAAA......",
    "......AAAA......",
    ".....AAAAAA.....",
    "....AAAAAAAA....",
    "...AAAAAAAAAA...",
    "..AAAAAAAAAAAA..",
    ".AAAAAAAAAAAAAA.",
    "AAAAAAAAAAAAAAAA",
    "A.....AAAA.....A",
    "A......AA......A",
    ".....PPPPPP.....",
    "................",
]
SHIELD = [  # all clear: two-tone heraldic shield (user's choice); "B" = darker green half
    "................",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "..AAAAAABBBBBB..",
    "...AAAAABBBBB...",
    "...AAAAABBBBB...",
    "....AAAABBBB....",
    ".....AAABBB.....",
    "......AABB......",
    ".......AB.......",
    "................",
]
CALM = (90, 170, 110)  # muted green, only used for the all-clear state
ALERT_STYLE = {"red": (WARN, "alert_red"), "yellow": (SUN, "alert_yellow"), "none": (CALM, "alert_none")}
ALERT_ICON = {"red": MISSILE, "yellow": DRONE, "none": SHIELD}  # icon art per state (16x16); swap here, then `gen` + `golden`
# Alert icons: "A" takes the state colour, other letters a fixed colour.
ICON_COLORS = {"O": (255, 140, 40), "P": (150, 150, 150), "B": (60, 125, 80)}


def draw_icon(img, art: list[str], x: int, y: int, px: int, color) -> None:
    d = ImageDraw.Draw(img)
    for r, row in enumerate(art):
        for c, ch in enumerate(row):
            if ch != ".":
                col = color if ch == "A" else ICON_COLORS.get(ch, color)
                d.rectangle([x + c * px, y + r * px, x + (c + 1) * px - 1, y + (r + 1) * px - 1], fill=col)


def draw_alert(img, state: str | None, since: str | None) -> None:
    """Bottom-right block of the clock screen. state: "red", "yellow", "none", or None (unknown: hidden)."""
    if state is None:
        return
    color, key = ALERT_STYLE[state]
    # vertically centred on the weather icon (its art spans ~y 160..216, centre 188)
    draw_icon(img, ALERT_ICON[state], 196, 164, 3, color)
    full = state != "none" and since
    draw_text(img, STR[key], 250, 172 if full else 180, 1, color)
    if full:
        draw_text(img, f"{STR['since']} {since}", 250, 186, 1, DIM)
    draw_text(img, STR["alert_city"], 250, 200 if full else 194, 1, DIM)


def mmss(s: int) -> str:
    return f"{s // 60}:{s % 60:02d}"


# -- screens ------------------------------------------------------------------------------
def screen_clock(hh=7, mm=42, weekday=5, day=10, month=10, temp=12, weather="partly", last=None, colon=True,
                 alert="none", alert_since=None) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    t = f"{hh:02d}{':' if colon else ' '}{mm:02d}"  # colon blinks at 1 Hz; " " keeps the width
    px = 13
    draw_big(img, t, (W - big_width(t, px)) // 2, 22, px, FG)
    draw_text_c(img, f"{DAYS[weekday]}, {day} {MONTHS_GEN[month - 1]}", W // 2, 112, 2, FG)
    # bottom row: weather left, last session right
    draw_art(img, WEATHER[weather], 14, 156, 4)
    draw_text(img, f"{temp:+d}°", 86, 176, 3, FG)
    if last:  # superseded by the air-raid block (kept for reference / alternative layouts)
        secs, at = last
        draw_text(img, STR["last"], 196, 164, 1, DIM)
        draw_text(img, mmss(secs), 196, 180, 3, FG if secs >= 120 else DIM)
        draw_text(img, f"{STR['at']} {at}", 196, 210, 1, DIM)
    else:
        draw_alert(img, alert, alert_since)
    return img


def screen_brushing(secs=83, coverage=None, current="UIL", high=False, blink_on=True) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    t = mmss(secs)
    px = 11
    draw_big(img, t, (W - big_width(t, px)) // 2, 10, px, WARN if high else (ACCENT if secs >= 120 else FG))
    draw_progress(img, W // 2, 76, secs, blink_on=blink_on)
    cell = 13
    tw, th = teeth_size(cell)
    draw_teeth(img, (W - tw) // 2, 100, cell, coverage or {}, current)
    if high:
        draw_text(img, STR["pressure"], 8, H - 16, 1, WARN)
    # side labels: the user's left / right
    draw_text(img, "Л", (W - tw) // 2 - 22, 100 + th // 2 - 4, 1, DIM)
    draw_text(img, "П", (W + tw) // 2 + 14, 100 + th // 2 - 4, 1, DIM)
    return img


def screen_result(secs=134, coverage=None) -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    ok = secs >= 120
    draw_art(img, FACE_HAPPY if ok else FACE_SAD, 8, 32, 8, ACCENT, gap=1)
    draw_text_c(img, STR["great"] if ok else STR["short"], 8 + 64, 176, 2, ACCENT if ok else DIM)
    t = mmss(secs)
    px = 8
    draw_big(img, t, 196 - big_width(t, px) // 2 + 30, 18, px, FG)
    cell = 9
    tw, th = teeth_size(cell)
    draw_teeth(img, 226 - tw // 2, 82, cell, coverage or {}, None)
    return img


def demo_coverage(progress: float) -> dict[str, float]:
    order = ["UOL", "UOF", "UOR", "UIR", "UIF", "UIL", "UCL", "UCR", "LOR", "LOF", "LOL", "LIL", "LIF", "LIR", "LCR", "LCL"]
    n = progress * len(order)
    return {z: max(0.0, min(1.0, n - i)) for i, z in enumerate(order)}


def mock(out: Path) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    shots = {
        "1-clock": screen_clock(),
        "2-brushing": screen_brushing(83, demo_coverage(0.38), "UIL"),
        "3-brushing-pressure": screen_brushing(47, demo_coverage(0.2), "UOR", high=True),
        "4-result-good": screen_result(134, demo_coverage(1.0)),
        "5-result-short": screen_result(52, demo_coverage(0.4)),
        "6-weather-icons": weather_sheet(),
        "7-clock-night": screen_clock(hh=23, mm=15, temp=9, weather="clear_night"),
        "8-clock-night-partly": screen_clock(hh=2, mm=40, temp=7, weather="partly_night", last=None),
    }
    paths = []
    for name, im in shots.items():
        p = out / f"{name}.png"
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p)  # 2x so pixels are visible
        paths.append(p)
    # contact sheet with all screens
    rows = (len(shots) + 1) // 2
    sheet = Image.new("RGB", (W * 2 + 30, H * rows + 10 * (rows + 1)), (25, 25, 25))
    for i, im in enumerate(list(shots.values())):
        sheet.paste(im, (10 + (i % 2) * (W + 10), 10 + (i // 2) * (H + 10)))
    p = out / "0-all.png"
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(p)
    paths.insert(0, p)
    return paths


def weather_sheet() -> Image.Image:
    img = Image.new("RGB", (W, H), BG)
    for i, (name, art) in enumerate(WEATHER.items()):
        x, y = 8 + (i % 5) * 63, 24 + (i // 5) * 110
        draw_art(img, art, x, y, 3)
        draw_text(img, name.replace("_", " ")[:7], x, y + 56, 1, DIM)
    return img




# -- firmware asset generation ------------------------------------------------------------------
ROOT = HERE.parents[1]  # repository root
ZONE_MODEL = ROOT / "tools" / "python" / "src" / "esp_oralb" / "zone_model.json"
WEATHER_NAMES = list(WEATHER)  # clear partly cloudy fog rain snow storm: index = firmware icon id


def _c(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _rgb(c) -> str:
    return "0x%02X%02X%02X" % c


def gen_header(out: Path) -> None:
    import json

    model = json.loads(ZONE_MODEL.read_text())
    zones = list(model["zones"] if "zones" in model else model["means"])  # firmware zone order
    L: list[str] = []
    a = L.append
    a("// GENERATED by tools/design/design.py gen - do not edit.")
    a("// Pixel design assets for the 320x240 UI. The 8x8 text font is Press Start 2P")
    a("// (c) 2012 The Press Start 2P Project Authors, SIL Open Font License 1.1, see tools/design/OFL.txt.")
    a("#pragma once")
    a("#include <stddef.h>")
    a("#include <stdint.h>")
    a("")
    a("namespace gfx {")
    a("")
    a("constexpr int SCREEN_W = %d, SCREEN_H = %d;" % (W, H))
    a("")
    a("// palette (0xRRGGBB)")
    for name, col in (("BG", BG), ("FG", FG), ("DIM", DIM), ("OFF", OFF), ("ACCENT", ACCENT), ("WARN", WARN), ("SUN", SUN)):
        a("constexpr uint32_t C_%s = %s;" % (name, _rgb(col)))
    a("")
    a("// big 3x5 block digits: rows as strings of 0/1")
    a("struct BigGlyph { char ch; uint8_t w; const char* rows[5]; };")
    a("constexpr BigGlyph BIG_GLYPHS[] = {")
    for ch, rows in BIG.items():
        a("    {'%s', %d, {%s}}," % (ch, len(rows[0]), ", ".join(_c(r) for r in rows)))
    a("};")
    a("constexpr size_t BIG_GLYPH_COUNT = sizeof(BIG_GLYPHS) / sizeof(BIG_GLYPHS[0]);")
    a("")
    a("// Press Start 2P 8x8 glyphs keyed by Unicode codepoint (sorted); one byte per row, bit 7 = leftmost pixel")
    a("struct TextGlyph { uint32_t cp; uint8_t rows[8]; };")
    a("constexpr TextGlyph TEXT_GLYPHS[] = {")
    for ch in sorted(TEXT_CHARS, key=ord):
        rows = ", ".join("0x%02X" % int(r, 2) for r in _GLYPHS[ch])
        a("    {0x%04X, {%s}},  // %s" % (ord(ch), rows, ch if ch != "\\" else "backslash"))
    a("};")
    a("constexpr size_t TEXT_GLYPH_COUNT = sizeof(TEXT_GLYPHS) / sizeof(TEXT_GLYPHS[0]);")
    a("")
    a("// 16x16 pixel art, rows of 16 chars ('.' = empty)")
    for name, art in (("FACE_HAPPY", FACE_HAPPY), ("FACE_SAD", FACE_SAD)):
        a("constexpr const char* %s[16] = {" % name)
        for r in art:
            a("    %s," % _c(r))
        a("};")
    a("")
    a("// air-raid block: icon art, colour and label per state (generated from ALERT_ICON / ALERT_STYLE)")
    for state in ("red", "yellow", "none"):
        a("constexpr const char* ALERT_ICON_%s[16] = {" % state.upper())
        for r in ALERT_ICON[state]:
            a("    %s," % _c(r))
        a("};")
    a("// weather icons, index = Icon enum in weather.h: " + " ".join(WEATHER_NAMES))
    a("constexpr const char* WEATHER_ART[%d][16] = {" % len(WEATHER))
    for name in WEATHER_NAMES:
        a("    {  // " + name)
        for r in WEATHER[name]:
            a("        %s," % _c(r))
        a("    },")
    a("};")
    a("// per-letter colours of the weather icons")
    a("// alert icons: 'A' = state colour, other letters fixed (ICON_COLORS)")
    a("inline uint32_t icon_color(char letter, uint32_t main) {")
    for k, v in ICON_COLORS.items():
        a("  if (letter == '%s') return %s;" % (k, _rgb(v)))
    a("  return main;")
    a("}")
    a("inline uint32_t weather_color(char letter) {")
    a("  switch (letter) {")
    for k, v in WEATHER_COLORS.items():
        a("    case '%s': return %s;" % (k, _rgb(v)))
    a("    default: return C_FG;")
    a("  }")
    a("}")
    a("")
    a("// teeth chart: cells (column, row) per zone, indexed in the firmware's zone order (ZONE_CODES)")
    a("constexpr int TEETH_COLS = %d, TEETH_ROWS = %d;" % (TEETH_COLS, TEETH_ROWS))
    a("struct TeethZone { const char* code; uint8_t n; uint8_t cells[3][2]; };")
    a("constexpr TeethZone TEETH_ZONES[%d] = {" % len(zones))
    for z in zones:
        cs = TEETH[z]
        pad = cs + [(0, 0)] * (3 - len(cs))
        a("    {%s, %d, {%s}}," % (_c(z), len(cs), ", ".join("{%d, %d}" % c for c in pad)))
    a("};")
    a("")
    a("// Ukrainian strings (UTF-8)")
    a("constexpr const char* DAYS[7] = {%s};  // Monday first" % ", ".join(_c(d) for d in DAYS))
    a("constexpr const char* MONTHS_GEN[12] = {%s};" % ", ".join(_c(m) for m in MONTHS_GEN))
    for k, v in STR.items():
        a("constexpr const char* STR_%s = %s;" % (k.upper(), _c(v)))
    a("")
    a("struct AlertStyle { const char* const* art; uint32_t color; const char* label; bool label_dim; };")
    a("constexpr AlertStyle ALERT_STYLES[3] = {  // red, yellow, none")
    for state in ("red", "yellow", "none"):
        col, key = ALERT_STYLE[state]
        a("    {ALERT_ICON_%s, %s, STR_%s, false}," % (state.upper(), _rgb(col), key.upper()))  # label in the state colour
    a("};")
    a("")
    a("}  // namespace gfx")
    out.write_text("\n".join(L) + "\n")


def golden_scenarios() -> dict[str, Image.Image]:
    """Inputs mirrored by test/test_core/test_screens.cpp."""
    return {
        "clock": screen_clock(),
        "clock_colon_off": screen_clock(colon=False),
        "clock_night": screen_clock(hh=23, mm=15, temp=9, weather="clear_night"),
        "clock_night_partly": screen_clock(hh=2, mm=40, temp=7, weather="partly_night", last=None),
        "clock_alert_yellow": screen_clock(hh=20, mm=47, temp=13, weather="partly_night", alert="yellow", alert_since="20:30"),
        "clock_alert_red": screen_clock(hh=20, mm=52, temp=13, weather="partly_night", alert="red", alert_since="20:49"),
        "clock_alert_unknown": screen_clock(alert=None),
        "brushing": screen_brushing(83, demo_coverage(0.38), "UIL"),
        "pressure": screen_brushing(47, demo_coverage(0.2), "UOR", high=True),
        "result_good": screen_result(134, demo_coverage(1.0)),
        "result_short": screen_result(52, demo_coverage(0.4)),
    }


def write_golden(outdir: Path) -> list[Path]:
    """Run-length encoded RGB: repeated (u16 LE run, r, g, b) over row-major pixels."""
    import struct

    outdir.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, im in golden_scenarios().items():
        px = list(im.convert("RGB").getdata())
        data = bytearray()
        i = 0
        while i < len(px):
            j = i
            while j < len(px) and px[j] == px[i] and j - i < 65535:
                j += 1
            data += struct.pack("<HBBB", j - i, *px[i])
            i = j
        p = outdir / f"golden_{name}.rle"
        p.write_bytes(bytes(data))
        paths.append(p)
    return paths


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) >= 2 else ""
    if cmd == "mock" and len(sys.argv) >= 3:
        for p in mock(Path(sys.argv[2])):
            print(p)
    elif cmd == "gen":
        out = Path(sys.argv[2]) if len(sys.argv) >= 3 else ROOT / "include" / "gfx_assets.h"
        gen_header(out)
        print(out)
    elif cmd == "golden":
        for p in write_golden(Path(sys.argv[2]) if len(sys.argv) >= 3 else ROOT / "test" / "data"):
            print(p)
    else:
        print(__doc__)
