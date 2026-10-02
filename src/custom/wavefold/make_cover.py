"""Generate src/airwindows/common/covers/WaveFold.json (128x64 cover override).

Concept: the art area is split into two equal rectangles that mirror each other
around the middle of the screen. On the left, a box with two cycles of a sine
folded once at the limit: each peak bends back down and leaves a small dip,
while dots show where the plain sine would have gone. On the right, WAVE / FOLD
stacked in a rectangle the same size as the box: WAVE black on white in the top
half, FOLD white on a black block in the bottom half.

Run from anywhere:  py src\\custom\\wavefold\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/wavefold/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "WaveFold"
LABELS = ("DRIVE", "SYMM", "TONE")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 6, 2)             # font column -> px: 2 px stems like the pack's other titles, 4 x 10 + 3 x 3 = 49
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall
LGAP = 3
TXT_IN = (RECT_W - (4 * sum(COLS) + 3 * LGAP)) // 2   # 6 px side margin inside the title rectangle
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: WAVE in rows 0..16, FOLD block 18..34
FRAME = True                 # outline the title rectangle like the wave box
ROUND = True                 # clip the rectangles' corners, like DubSiren's box
LIMIT = 0.55                 # fold limit, as a fraction of the sine's peak
AMP = 14.0                   # sine peak in px
PERIOD = (RECT_W - 5) / 2.0  # two full cycles inside the box


def big_text(c, text, x, y, v=1):
    for ch in text:
        rows = Canvas._FONT[ch]
        yy = y
        for r, row in enumerate(rows):
            xx = x
            for k, bit in enumerate(row):
                if bit == '1':
                    for dy in range(ROWS[r]):
                        for dx in range(COLS[k]):
                            c.px(xx + dx, yy + dy, v)
                xx += COLS[k]
            yy += ROWS[r]
        x += sum(COLS) + LGAP


def line(c, x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        c.px(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n))


def fold(v):
    """Reflect at +-LIMIT, like a folder at low Drive."""
    if v > LIMIT:
        return 2 * LIMIT - v
    if v < -LIMIT:
        return -2 * LIMIT - v
    return v


def build():
    c = Canvas()
    c.rect(BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    cy = (ART_TOP + ART_BOTTOM) / 2
    x0, x1 = BOX_X0 + 2, BOX_X1 - 2
    s = lambda x: math.sin(2 * math.pi * (x - x0) / PERIOD)

    # ghost: the part of the sine the fold removed, a dot every 2 px along the curve
    run, p = 2.0, None
    for i in range(x0 * 8, x1 * 8 + 1):
        x = i / 8.0
        v = s(x)
        if abs(v) <= LIMIT + 0.04:
            p, run = None, 2.0
            continue
        y = cy - AMP * v
        if p:
            run += math.hypot(x - p[0], y - p[1])
        p = (x, y)
        if run >= 2.0:
            c.px(round(x), round(y))
            run = 0.0

    # folded sine: solid, 2 px thick
    for off in (0, 1):
        prev = None
        for x in range(x0, x1 + 1):
            y = round(cy - AMP * fold(s(x))) + off
            if prev is None:
                c.px(x, y)
            else:
                line(c, x - 1, prev, x, y)
            prev = y

    # title: WAVE black on white in the top half, FOLD knocked out of a black block in
    # the bottom half; the letters sit at the same distance from each half's edges
    ty = (SPLIT - ART_TOP - sum(ROWS)) // 2
    if FRAME:
        c.rect(TXT_X0, ART_TOP, W - 1, ART_BOTTOM)
    for y in range(SPLIT + 1, ART_BOTTOM + 1):
        c.hline(TXT_X0, W - 1, y)
    big_text(c, "WAVE", TXT_X0 + TXT_IN, ART_TOP + ty)
    big_text(c, "FOLD", TXT_X0 + TXT_IN, SPLIT + 1 + ty, v=0)
    if ROUND:
        for x0, x1 in ((BOX_X0, BOX_X1), (TXT_X0, W - 1)):
            for x, y in ((x0, ART_TOP), (x1, ART_TOP), (x0, ART_BOTTOM), (x1, ART_BOTTOM)):
                c.px(x, y, 0)
            c.px(x0 + 1, ART_TOP + 1); c.px(x1 - 1, ART_TOP + 1)
            c.px(x0 + 1, ART_BOTTOM - 1); c.px(x1 - 1, ART_BOTTOM - 1)

    # --- labels and dials under the firmware value boxes (page 1 knobs) --------
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        lx = max(2, cx - w // 2)
        c.draw_text(label, lx, 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)
    return c                                  # no bottom rule


if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / (NAME + ".json")
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (255, 255, 255))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (0, 0, 0))
    im.resize((128 * 6, 64 * 6), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
