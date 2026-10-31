"""Generate src/airwindows/common/covers/GRDLOOP.json (128x64 cover override).

GRDLOOP: the name in big letters on top and, under it, the empty rounded loop bar outline
(2 px stroke, radius-2 corners, rows 17..34). On MIDISTOMP GRDLOOP draws the live bar
(fill, state, bar.beat/cycle) into it itself (looper.c lp_bar); on stock it stays empty.

Run from anywhere:  py src/custom/looper/make_cover.py
Also writes cover_preview.png next to this script (LCD look, build/lcd_render.py).
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/looper/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "GRDLOOP"
LABELS = ("MIX", "ROLL", "SYNC")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 3, 2)             # font column -> px: 2 px stems, letters 7 wide (6 x 7 + 5 x 2 = 52)
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall, as WaveFold
LGAP = 2
TITLE = "GRDL@P"                               # @ = the OO spiral
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: title in rows 0..16, block 18..34

BARS = 8
FILL_BARS = 5
BAR_Y0, BAR_Y1 = 17, ART_BOTTOM   # the loop bar, 2 px stroke


# the two Os fused into one wide spiral glyph
GLYPHS = {"@": (("111111111", "000000001", "111111101", "100000101", "101110101",
                  "101000101", "101111101", "100000001", "111111111"),
                 (2, 2, 2, 2, 9, 2, 2, 2, 2), (2, 1, 2, 1, 1, 1, 2, 1, 2))}   # 25 x 13, as wide as GRD: 2 px lines


def big_text(c, text, x, y, v=1):
    for ch in text:
        rows, cols, rsz = GLYPHS[ch] if ch in GLYPHS else (Canvas._FONT[ch], COLS, ROWS)
        yy = y - (sum(rsz) - sum(ROWS)) // 2           # a taller glyph sticks out evenly
        for r, row in enumerate(rows):
            xx = x
            for k, bit in enumerate(row):
                if bit == '1':
                    for dy in range(rsz[r]):
                        for dx in range(cols[k]):
                            c.px(xx + dx, yy + dy, v)
                xx += cols[k]
            yy += rsz[r]
        x += sum(cols) + LGAP


def round_strokes(p):
    """Radius-2 corners on 2 px strokes: cut each outer corner, fill the inner one."""
    out = set(p)
    for x, y in p:
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            if (x + sx, y) not in p and (x, y + sy) not in p and (x - sx, y) in p and (x, y - sy) in p                     and (x - 2 * sx, y) in p and (x, y - 2 * sy) in p:
                out -= {(x, y), (x - sx, y), (x, y - sy)}
                out.add((x - sx, y - sy))
    return out


def text_w(text):
    return sum(sum(GLYPHS.get(ch, (0, COLS, 0))[1]) for ch in text) + (len(text) - 1) * LGAP


def round_corners(c, x0, y0, x1, y1):
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        c.px(x, y, 0)
    c.px(x0 + 1, y0 + 1); c.px(x1 - 1, y0 + 1)
    c.px(x0 + 1, y1 - 1); c.px(x1 - 1, y1 - 1)


def small_xor(c, text, x, y, k=2):
    """3x5 font at k x k, XORed so it reads on the fill and on white."""
    for ch in text:
        glyph = ["101", "101", "010", "010", "010"] if ch == "Y" else Canvas._FONT[ch]   # a clearer Y
        for r, row in enumerate(glyph):
            for j, bit in enumerate(row):
                if bit == '1':
                    for dy in range(k):
                        for dx in range(k):
                            xx, yy = x + j * k + dx, y + r * k + dy
                            c.px(xx, yy, 1 - c.pixels[yy][xx])
        x += 4 * k


# glitch: title rows -> sideways shift (rows 0..12 of the 13-row letters)
SLICES = {}                                    # no glitch slips
def glitch_title(c):
    t = Canvas()
    tx, ty = (W - text_w(TITLE) + 1) // 2, 1
    big_text(t, TITLE, tx, ty)
    ink = set()
    for y in range(BAR_Y0 - 1):                    # all title rows, the spiral is taller than the letters
        d = SLICES.get(y - ty, 0)
        ink |= {(x + d, y) for x in range(W) if t.pixels[y][x]}
    sx0 = tx + text_w(TITLE[:TITLE.index("@")]) + LGAP   # round the spiral so it reads as one
    sx1 = sx0 + text_w("@")
    spiral = {(x, y) for x, y in ink if sx0 <= x < sx1}
    ink = (ink - spiral) | round_strokes(spiral)
    ink |= {(sx0 + dx, y) for dx, y in ((2, 6), (2, 11), (18, 6), (18, 8), (22, 3), (22, 11))}   # Luca's extra inner-corner pixels
    for x, y in ink:                               # 1 px of air around the title
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if 0 <= x + dx < W and 0 <= y + dy < H:
                    c.px(x + dx, y + dy, 0)
    for x, y in ink:
        c.px(x, y)


def build():
    c = Canvas()
    glitch_title(c)

    # --- the loop bar outline only: on MIDISTOMP looper.c lp_bar draws the live bar into it
    for d in (0, 1):
        c.rect(d, BAR_Y0 + d, W - 1 - d, BAR_Y1 - d)
    for x, y in ((0, 0), (1, 0), (0, 1)):              # round the four corners, radius 2
        for px_, py_ in ((x, BAR_Y0 + y), (W - 1 - x, BAR_Y0 + y), (x, BAR_Y1 - y), (W - 1 - x, BAR_Y1 - y)):
            c.px(px_, py_, 0)
    for px_, py_ in ((1, BAR_Y0 + 1), (W - 2, BAR_Y0 + 1), (1, BAR_Y1 - 1), (W - 2, BAR_Y1 - 1)):
        c.px(px_, py_)

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
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "build"))
    from lcd_render import save_png
    save_png(c.pixels, Path(__file__).resolve().parent / "cover_preview.png")
    print("wrote", out)
