"""Generate src/airwindows/common/covers/Sweep.json (128x64 cover override).

Concept (Breather style, the title is the art): SWEEP in thick 4 px letters. A phaser notch sits
in the gap between the two Es and leaves a dithered trail over the letters it has already passed.
Under it the LFO: a sine, solid up to the point marker (where the notch is now), dotted after it
(still to come). A dotted line runs from the marker straight up through the title.

Run from anywhere:  py src\\custom\\sweep\\make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script.
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

TITLE = "SWEEP"
LABELS = ("TYPE", "RATE", "DEPTH")
SCALE, GAP, X0, Y0 = 4, 5, 20, 3
NX, NGAP, TRAIL = 76, 2, 30               # notch centre, half-width of the clear zone, trail length
CY, AMP, LAM = 31, 4.0, 36.0              # LFO centre line, height, wavelength
# the pack font has a 3-wide W that reads as H: own 5-wide W
FONT = dict(Canvas._FONT)
FONT["W"] = ["10001", "10001", "10101", "10101", "01010"]
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

def wave(x):
    return math.sin(2 * math.pi * ((x - NX) / LAM))

def build():
    c = Canvas(); m = title_mask()
    for y in range(64):
        for x in range(128):
            if not m.pixels[y][x]:
                continue
            if x >= NX + NGAP or x < NX - TRAIL - NGAP:
                c.px(x, y)
            elif x >= NX - NGAP:
                continue                                    # the notch itself
            else:
                d = 1 - (NX - NGAP - 1 - x) / TRAIL         # dense near the notch
                if dither(x, y, 1 - d * 0.8):
                    c.px(x, y)
    for x in range(4, 124):                                 # LFO: swept = solid, to come = dotted
        y = int(round(CY - AMP * wave(x)))
        if x <= NX or x % 2 == 0:
            c.px(x, y)
        if x <= NX:
            y2 = int(round(CY - AMP * wave(x + 1)))
            for yy in range(min(y, y2), max(y, y2) + 1):
                c.px(x, yy)
    my = int(round(CY - AMP * wave(NX)))
    for y in range(0, my):                                  # dotted line straight up through the title
        if y % 2 == 0:
            c.px(NX, y)
    for yy in range(my - 1, my + 2):                        # point marker
        for xx in range(NX - 1, NX + 2):
            c.px(xx, yy)
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
    out = ROOT / "src" / "airwindows" / "common" / "covers" / "Sweep.json"
    out.write_text(json.dumps({"name": "Sweep", "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (246, 248, 252))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (28, 52, 120))
    im.resize((128 * 5, int(64 * 5 * 1.4)), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
