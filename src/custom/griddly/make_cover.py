"""Generate src/airwindows/common/covers/GridDly.json (128x64 cover override).

Concept (DualShft/Breather style, the title is the art): GRIDLY in thick 4 px letters, with
dithered drips falling from the letters' feet (Breather), and under it the delay as a row of
hit bumps on a dotted step line: six repeats, each smaller than the last, an arrow at the right
for the direction it travels.

Run from anywhere:  py src\\custom\\griddly\\make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script.
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

TITLE = "GRIDLY"
LABELS = ("TYPE", "TIME", "FDBK")
SCALE, GAP, X0, Y0 = 4, 4, 16, 1
CY = 29                                    # centre line of the bumps
# the pack font's 3-wide G and Y read badly at this size: own glyphs (Y is 5 wide)
FONT = dict(Canvas._FONT)
FONT["G"] = ["111", "100", "101", "101", "111"]
FONT["Y"] = ["10001", "10001", "01010", "00100", "00100"]
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

def dither(x, y, d):
    return BAYER[y & 3][x & 3] < d * 16

def title_mask():
    m = Canvas(); x = X0
    for ch in TITLE:
        rows = FONT[ch]
        for r, row in enumerate(rows):
            for i, b in enumerate(row):
                if b == "1":
                    for dy in range(SCALE):
                        for dx in range(SCALE):
                            m.px(x + i * SCALE + dx, Y0 + r * SCALE + dy)
        x += len(rows[0]) * SCALE + GAP
    return m

def build():
    c = Canvas(); m = title_mask()
    for y in range(64):
        for x in range(128):
            if m.pixels[y][x]:
                c.px(x, y)
    for y in range(24):                       # drips from the feet of the letters
        for x in range(128):
            if m.pixels[y][x] and not m.pixels[y + 1][x]:
                for k in range(1, 4):
                    if dither(x, y + k, 0.7 - 0.2 * k) and not m.pixels[y + k][x] and y + k < 25:
                        c.px(x, y + k)
    n, x0, pitch = 6, 9, 21
    for x in range(4, 120, 4):                # the steps
        if not any(abs(x - (x0 + k * pitch)) < 9 for k in range(n)):
            c.px(x, CY)
    for k in range(n):                        # hit bumps, each 22 % smaller
        cx = x0 + k * pitch; a = 5.0 * 0.78 ** k
        for j in range(-7, 8):
            h = int(round(a * (1 - abs(j) / 8.0) ** 1.2))
            for yy in range(CY - h, CY + h + 1):
                c.px(cx + j, yy)
    for i in range(4):                        # arrow
        c.vline(122 + i, CY - 3 + i, CY + 3 - i)
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        c.draw_text(label, max(2, cx - w // 2), 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)
    return c

if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / "GridDly.json"
    out.write_text(json.dumps({"name": "GridDly", "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (246, 248, 252))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (28, 52, 120))
    im.resize((128 * 5, int(64 * 5 * 1.4)), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
