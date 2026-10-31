"""Generate src/airwindows/common/covers/Takeoff.json (128x64 cover override), Luca's takeoff-v5a.

TAKEOFF in BREATHER's stretched 3x5 font: sparse dither above a riser boundary, a solid line, a
white line, solid below; the boundary rises across the word and peaks on the last F's top-right
corner. Under it a filled wave growing from zero, with a dotted centre line. BREATHER's knob row.
(meter-mockups/make_takeoff_v5.py build("a").)

Run from anywhere:  py src/custom/takeoff/make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script (LCD look, build/lcd_render.py).
"""
import importlib.util, json, math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/takeoff/make_cover.py -> repo root
spec = importlib.util.spec_from_file_location("breather_cover", ROOT / "src" / "custom" / "breather" / "make_cover.py")
br = importlib.util.module_from_spec(spec)
spec.loader.exec_module(br)                       # BREATHER's ink() and labels(); its __main__ does not run
from screen_image import Canvas                   # build/ is on sys.path via the BREATHER module

NAME = "Takeoff"
br.LABELS = ("Dir", "Len", "Sound")
WORD = "TAKEOFF"
CELL, COLS, ROWS, TOP = 18, (4, 4, 4), (4, 4, 3, 4, 4), 2   # letters 12 x 19, rows 2..20
X0 = (128 - 7 * CELL) // 2 + (CELL - 12) // 2
BOT = TOP + sum(ROWS) - 1                         # last letter row
B_MID, B_AMP = 29, 6                              # wave under the title
XE = X0 + 6 * CELL + 11                           # last F's right column


def boundary(x):
    """white line row: low on T, exactly one row above the last F's top-right corner"""
    t = min(max((x - X0) / (XE - X0), 0.0), 1.0)
    return BOT - 1 - (BOT - TOP) * t ** 1.4


def build():
    c = Canvas()
    for i, ch in enumerate(WORD):
        y = TOP
        for r, row in enumerate(Canvas._FONT[ch]):
            xx = X0 + i * CELL
            for k, bit in enumerate(row):
                if bit == "1":
                    for dy in range(ROWS[r]):
                        for dx in range(COLS[k]):
                            X, Y = xx + dx, y + dy
                            wy = round(boundary(X))
                            if Y > wy or Y == wy - 1 or (Y < wy and br.ink(0, X, Y)):
                                c.px(X, Y)
                xx += COLS[k]
            y += ROWS[r]
    for x in range(4, 124):                       # filled wave, growing left to right
        t = (x - 4) / 119
        h = B_AMP * t * (0.55 + 0.45 * abs(math.sin(2 * math.pi * x / 6)))
        n = int(h) + (h % 1 > ((x * 5) % 7 + 0.5) / 7)   # dithered top / bottom pixel: no visible steps
        c.vline(x, B_MID - n, B_MID + n)
    for x in range(2, 126):                       # dotted centre line: white in the wave, ink outside
        inside = c.pixels[B_MID - 1][x] or c.pixels[B_MID + 1][x]
        if x % 2 == 0:
            c.px(x, B_MID, 0 if inside else 1)
        elif not inside:
            c.px(x, B_MID, 0)
    br.labels(c)
    return c


if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / (NAME + ".json")
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    from lcd_render import save_png
    save_png(c.pixels, Path(__file__).resolve().parent / "cover_preview.png")
    print("wrote", out)
