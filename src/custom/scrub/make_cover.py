"""Generate src/airwindows/common/covers/Scrub.json (128x64 cover override).

Concept, in the WaveFold layout (two equal rectangles mirrored around the middle of the
screen, 2 px strokes, rounded corners): on the left a sample-editor view of the buffer,
three drum hits drawn as a waveform with a 2 px playhead; the grain just before the head
is the frozen loop, in a dotted frame with a loop arrow over it. On the right SCRUB in the
top half, and in the bottom half a black block with a white scrub bar: a timeline, the
head on it and arrows either side (you drag it both ways).

Run from anywhere:  py src\\custom\\scrub\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/scrub/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "Scrub"
LABELS = ("POS", "GRAIN", "REC")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 4, 2)             # font column -> px: 2 px stems, letters 8 wide
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall, as WaveFold
LGAP = 2
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: SCRUB in rows 0..16, block 18..34
HITS = (4, 22, 44)           # drum hits along the buffer (x)
HEAD = 38                    # playhead x (2 px: 38, 39)
GRAIN = 12                   # frozen grain: the 12 columns before the head
WCY = 21                     # waveform centre row
WAMP = 9.0                   # waveform peak, px


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


def text_w(text):
    return len(text) * sum(COLS) + (len(text) - 1) * LGAP


def env(x):
    """Waveform height at column x: each hit jumps up and decays."""
    a = 0.0
    for h in HITS:
        if x >= h:
            a = max(a, math.exp(-(x - h) / 6.0))
    return a


def round_corners(c, x0, x1, y0, y1):
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        c.px(x, y, 0)
    c.px(x0 + 1, y0 + 1); c.px(x1 - 1, y0 + 1)
    c.px(x0 + 1, y1 - 1); c.px(x1 - 1, y1 - 1)


def build():
    c = Canvas()

    # --- left box: the buffer, the head and the frozen grain ----------------------------
    c.rect(BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    for x in range(BOX_X0 + 3, BOX_X1 - 2):
        a = env(x)
        # uneven column heights so it reads as a waveform, not a solid shape
        h = round(WAMP * a * (0.45, 1.0, 0.7, 0.9, 0.55)[x % 5])
        if h < 1:
            c.px(x, WCY)                          # silence: a flat line
        else:
            c.vline(x, WCY - h, WCY + h)
    # the frozen grain: a dotted frame over the columns before the head
    g0, g1 = HEAD - GRAIN, HEAD - 1
    fy0, fy1 = WCY - 11, WCY + 11
    for x in range(g0, g1 + 1, 2):
        c.px(x, fy0); c.px(x, fy1)
    for y in range(fy0, fy1 + 1, 2):
        c.px(g0, y)
    # loop arrow over the grain: from its end, up, back left, down into its start
    ay = fy0 - 2
    c.hline(g0 + 1, g1 - 1, ay - 3)
    c.px(g1, ay - 2); c.px(g1, ay - 1)
    c.px(g0, ay - 2); c.px(g0, ay - 1)
    c.hline(g0 - 2, g0 + 2, ay)                   # arrowhead pointing down at the start
    c.hline(g0 - 1, g0 + 1, ay + 1)
    # the playhead: 2 px, full height, a small flag on top
    for x in (HEAD, HEAD + 1):
        c.vline(x, ART_TOP + 3, ART_BOTTOM - 3)
    c.hline(HEAD - 1, HEAD + 2, ART_TOP + 3)
    c.hline(HEAD, HEAD + 1, ART_TOP + 4)

    # --- right box: SCRUB on top, a white scrub bar in a black block under it ----------
    c.rect(TXT_X0, ART_TOP, W - 1, ART_BOTTOM)
    ty = (SPLIT - ART_TOP - sum(ROWS)) // 2 + 1
    big_text(c, "SCRUB", TXT_X0 + (RECT_W - text_w("SCRUB")) // 2, ART_TOP + ty)
    for y in range(SPLIT + 1, ART_BOTTOM + 1):
        c.hline(TXT_X0, W - 1, y)
    by = (SPLIT + 1 + ART_BOTTOM) // 2            # bar row
    bx0, bx1 = TXT_X0 + 12, W - 13
    for x in range(bx0, bx1 + 1, 2):              # the timeline, dotted
        c.px(x, by, 0)
    c.hline(bx0, bx0 + 17, by, 0)                 # the part already played, solid
    hx = bx0 + 18                                 # the head on the bar: a 2 px post
    for x in (hx, hx + 1):
        c.vline(x, by - 5, by + 5, 0)
    for k in range(4):                            # arrows both ways at the ends
        c.vline(TXT_X0 + 4 + k, by - k, by + k, 0)
        c.vline(W - 5 - k, by - k, by + k, 0)
    round_corners(c, BOX_X0, BOX_X1, ART_TOP, ART_BOTTOM)
    round_corners(c, TXT_X0, W - 1, ART_TOP, ART_BOTTOM)

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
