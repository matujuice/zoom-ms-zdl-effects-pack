"""Generate src/airwindows/common/covers/Metro.json (128x64 cover override).

Concept (the WaveFold layout: two equal rectangles mirrored around the middle of the
screen, 2 px strokes, rounded corners). On the left, a pyramid metronome with its arm
swung out. On the right, the title rectangle: METRO black on white in the top half and,
knocked out of a black block in the bottom half, four beats, the first one big: the
accented downbeat the clicks mark.

Run from anywhere:  py src\\custom\\metro\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/metro/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "Metro"
LABELS = ("MIX", "CLICK", "BARS")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 5, 2)             # font column -> px: 2 px stems, letters 9 wide (5 x 9 + 4 x 3 = 57)
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall, as WaveFold
LGAP = 3
TITLE = "METRO"
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: title in rows 0..16, block 18..34

BODY_Y0, BODY_Y1 = 6, 31    # the metronome body's top and base rows
BODY_HT, BODY_HB = 8, 14     # its half widths at the top and at the base


# the 3-column font's M reads as H when its middle column is 5 px wide: give it a V
M_ROWS = ["10001", "11011", "10101", "10001", "10001"]
M_COLS = (2, 2, 1, 2, 2)


def big_text(c, text, x, y, v=1):
    for ch in text:
        rows, cols = (M_ROWS, M_COLS) if ch == "M" else (Canvas._FONT[ch], COLS)
        yy = y
        for r, row in enumerate(rows):
            xx = x
            for k, bit in enumerate(row):
                if bit == '1':
                    for dy in range(ROWS[r]):
                        for dx in range(cols[k]):
                            c.px(xx + dx, yy + dy, v)
                xx += cols[k]
            yy += ROWS[r]
        x += sum(COLS) + LGAP


def text_w(text):
    return len(text) * sum(COLS) + (len(text) - 1) * LGAP


def round_corners(c, x0, y0, x1, y1):
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        c.px(x, y, 0)
    c.px(x0 + 1, y0 + 1); c.px(x1 - 1, y0 + 1)
    c.px(x0 + 1, y1 - 1); c.px(x1 - 1, y1 - 1)


def thick_line(c, x0, y0, x1, y1, v=1):
    """a 2 px line (two columns side by side), from (x0, y0) to (x1, y1)"""
    n = max(abs(x1 - x0), abs(y1 - y0))
    for i in range(n + 1):
        x = round(x0 + (x1 - x0) * i / n)
        y = round(y0 + (y1 - y0) * i / n)
        c.px(x, y, v); c.px(x + 1, y, v)


def build():
    c = Canvas()

    # --- left: a pyramid metronome, its arm swung to the right ----------------------
    c.rect(BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    mx = (BOX_X0 + BOX_X1) // 2                         # 30: the body's middle
    base_y, top_y = BODY_Y1, BODY_Y0
    thick_line(c, mx - BODY_HB - 1, base_y, mx - BODY_HT - 1, top_y)    # left side
    thick_line(c, mx + BODY_HB - 1, base_y, mx + BODY_HT - 1, top_y)    # right side
    for y in (top_y, top_y + 1):
        c.hline(mx - BODY_HT, mx + BODY_HT - 1, y)                     # top
    for y in (base_y - 1, base_y):
        c.hline(mx - BODY_HB - 4, mx + BODY_HB + 3, y)                 # foot, wider
    px, py = mx, base_y - 5                             # the arm's pivot
    ax, ay = mx + 5, ART_TOP + 3                        # its tip, above the body
    thick_line(c, px, py, ax, ay)
    wx = px + (ax - px) * 6 // 10                       # the weight, 6/10 of the way up
    wy = py + (ay - py) * 6 // 10
    for y in range(wy - 2, wy + 3):
        c.hline(wx - 2, wx + 3, y)
    for y in (py - 1, py, py + 1):                      # the pivot
        c.hline(px - 1, px + 2, y)

    # --- right: METRO on white, four beats knocked out of the black block ------------
    c.rect(TXT_X0, ART_TOP, W - 1, ART_BOTTOM)
    ty = (SPLIT - ART_TOP - sum(ROWS)) // 2 + 1
    big_text(c, TITLE, TXT_X0 + (RECT_W - text_w(TITLE) + 1) // 2, ART_TOP + ty)
    for y in range(SPLIT + 1, ART_BOTTOM + 1):
        c.hline(TXT_X0, W - 1, y)
    my = (SPLIT + 1 + ART_BOTTOM) // 2                 # the beats' middle row
    step = 13
    x0 = TXT_X0 + (RECT_W - 3 * step) // 2             # four beats, 13 apart, centred
    for b in range(4):
        cx = x0 + b * step
        r = 4 if b == 0 else 2                          # the downbeat is the accent
        _VSquash(c, my).filled_circle(cx, my, r, 0)

    round_corners(c, BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    round_corners(c, TXT_X0, ART_TOP, W - 1, ART_BOTTOM)

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
