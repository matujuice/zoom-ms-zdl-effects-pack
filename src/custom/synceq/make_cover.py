"""Generate src/airwindows/common/covers/SyncEQ.json (128x64 cover override).

WaveFold's layout (Luca, 2026-10-06: "wavefolder style is good for it"). The art area is
split into two equal rectangles that mirror each other around the middle of the screen.
On the left, a box with an EQ curve (low shelf up, a dip in the mids, high shelf up),
dots along the flat 0 dB line it leaves, and four bar ticks along the bottom, the
downbeats SyncEQ passes on. On the right, SYNC / EQ stacked in a rectangle the same
size as the box: SYNC black on white in the top half, EQ white on a black block in the
bottom half.

Run from anywhere:  py src\\custom\\synceq\\make_cover.py
Also writes cover_preview.png next to this script (dark blue on white, stretched 1.4x, like the pedal's screen).
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/synceq/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "SyncEQ"
LABELS = ("LOCUT", "LOW", "MID")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 6, 2)             # font column -> px: 2 px stems like the pack's other titles, 4 x 10 + 3 x 3 = 49
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall
LGAP = 3
TXT_IN = (RECT_W - (4 * sum(COLS) + 3 * LGAP)) // 2   # 6 px side margin inside the title rectangle
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: SYNC in rows 0..16, EQ block 18..34
FRAME = True                 # outline the title rectangle like the wave box
ROUND = True                 # clip the rectangles' corners, like DubSiren's box
AMP = 9.0                    # EQ curve: +-1 = +-AMP px around the 0 dB line
CY = 15                      # the 0 dB line (row), above the bar ticks
TICK = 4                     # bar ticks: rows ART_BOTTOM-2-TICK+1 .. ART_BOTTOM-2


def big_text(c, text, x, y, v=1):
    for ch in text:
        if ch in GLYPHS:                      # letters with diagonals, drawn by hand below
            for yy, row in enumerate(GLYPHS[ch]):
                for xx, bit in enumerate(row):
                    if bit == '#':
                        c.px(x + xx, y + yy, v)
            x += sum(COLS) + LGAP
            continue
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

# N, Y and Q need diagonals the 3-column font can't stretch into: 10 x 13, 2 px stems
GLYPHS = {
    "N": ["##......##",
          "###.....##",
          "####....##",
          "##.##...##",
          "##.##...##",
          "##..##..##",
          "##..##..##",
          "##...##.##",
          "##...##.##",
          "##....####",
          "##....####",
          "##.....###",
          "##......##"],
    "Y": ["##......##",
          "##......##",
          ".##....##.",
          ".##....##.",
          "..##..##..",
          "..##..##..",
          "...####...",
          "....##....",
          "....##....",
          "....##....",
          "....##....",
          "....##....",
          "....##...."],
    "Q": ["..######..",
          ".########.",
          "##......##",
          "##......##",
          "##......##",
          "##......##",
          "##......##",
          "##...##.##",
          "##....####",
          "##.....##.",
          ".########.",
          "..######.#",
          "........##"],
}

def line(c, x0, y0, x1, y1):
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(n + 1):
        c.px(round(x0 + (x1 - x0) * i / n), round(y0 + (y1 - y0) * i / n))


def eq(u):
    """EQ curve over the box, u = 0..1 (log frequency): low shelf +, mid bell -, high shelf +."""
    def shelf(u, at, k):          # smooth 0..1 step
        return 1.0 / (1.0 + math.exp(-(u - at) * k))
    return (0.8 * (1.0 - shelf(u, 0.22, 30.0))
            - 0.9 * math.exp(-((u - 0.5) / 0.07) ** 2)
            + 1.0 * shelf(u, 0.78, 30.0))

def build():
    c = Canvas()
    c.rect(BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    x0, x1 = BOX_X0 + 3, BOX_X1 - 3

    # ghost: the flat 0 dB line, a dot every 2 px where the curve has left it
    for x in range(x0, x1 + 1, 2):
        if abs(AMP * eq((x - x0) / (x1 - x0))) >= 2.0:
            c.px(x, CY)

    # EQ curve: solid, 2 px thick
    for off in (0, 1):
        prev = None
        for x in range(x0, x1 + 1):
            y = round(CY - AMP * eq((x - x0) / (x1 - x0))) + off
            if prev is None:
                c.px(x, y)
            else:
                line(c, x - 1, prev, x, y)
            prev = y

    # four bars along the bottom: a tick on each downbeat, the bar sync it passes on
    for i in range(4):
        x = x0 + round(i * (x1 - x0 - 1) / 3.0)
        for y in range(ART_BOTTOM - 1 - TICK, ART_BOTTOM - 1):
            c.px(x, y); c.px(x + 1, y)

    # title: SYNC black on white in the top half, EQ knocked out of a black block in
    # the bottom half (centred); the letters sit at the same distance from each half's edges
    ty = (SPLIT - ART_TOP - sum(ROWS)) // 2
    if FRAME:
        c.rect(TXT_X0, ART_TOP, W - 1, ART_BOTTOM)
    for y in range(SPLIT + 1, ART_BOTTOM + 1):
        c.hline(TXT_X0, W - 1, y)
    big_text(c, "SYNC", TXT_X0 + TXT_IN, ART_TOP + ty)
    big_text(c, "EQ", TXT_X0 + (RECT_W - (2 * sum(COLS) + LGAP)) // 2, SPLIT + 1 + ty, v=0)
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
    im = Image.new("RGB", (128, 64), (246, 248, 250))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (28, 52, 120))
    im.resize((128 * 6, round(64 * 6 * 1.4)), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
