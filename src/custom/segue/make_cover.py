"""Generate src/airwindows/common/covers/Segue.json (128x64 cover override).

Concept (the WaveFold layout: two equal rectangles mirrored around the middle of the
screen, 2 px strokes, rounded corners). On the left, the loop: a phrase of eight bars
as a strip of eight blocks, a repeat arrow running from its end back over the top to bar 1.
On the right, the title rectangle: SEGUE black on white in the top half and, knocked
out of a black block in the bottom half, a DJ crossfader with its cap pushed to the
loop side: the jump to 100 % loop that hides the pattern change.

Run from anywhere:  py src\\custom\\segue\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/segue/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "Segue"
LABELS = ("XFADE", "LOCUT", "ROLL")
W, H = 128, 64
ART_TOP, ART_BOTTOM = 0, 34  # the art lives in rows 0..34, labels start at 37
RECT_W = 61                  # box x 0..60, gap 61..66, title x 67..127: mirrored around 63.5
BOX_X0, BOX_X1 = 0, RECT_W - 1
TXT_X0 = W - RECT_W
COLS = (2, 5, 2)             # font column -> px: 2 px stems, letters 9 wide (5 x 9 + 4 x 3 = 57)
ROWS = (3, 2, 3, 2, 3)       # font row -> px: letters 13 tall, as WaveFold
LGAP = 3
TITLE = "SEGUE"
SPLIT = (ART_TOP + ART_BOTTOM) // 2                   # row 17: title in rows 0..16, block 18..34

BARS = 8
CELL, GAP = 5, 1             # bar blocks 5 px wide, 1 px apart: 8 x 5 + 7 = 47
STRIP_Y0, STRIP_Y1 = 17, 28  # the bar strip
ARROW_Y = 6                  # the repeat arrow's top run (2 px: rows 6, 7)


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


def round_corners(c, x0, y0, x1, y1):
    for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
        c.px(x, y, 0)
    c.px(x0 + 1, y0 + 1); c.px(x1 - 1, y0 + 1)
    c.px(x0 + 1, y1 - 1); c.px(x1 - 1, y1 - 1)


def build():
    c = Canvas()

    # --- left: eight bars and a repeat arrow from the end back to bar 1 -------------
    c.rect(BOX_X0, ART_TOP, BOX_X1, ART_BOTTOM)
    strip_w = BARS * CELL + (BARS - 1) * GAP
    sx = BOX_X0 + (RECT_W - strip_w) // 2
    for b in range(BARS):
        x = sx + b * (CELL + GAP)
        for y in range(STRIP_Y0, STRIP_Y1 + 1):
            c.hline(x, x + CELL - 1, y)
    ex = sx + strip_w - 1                              # strip ends
    lx = sx + 1
    # arrow: up from the strip's end, over the top, down into bar 1, 2 px everywhere
    for x in (ex - 1, ex):
        c.vline(x, ARROW_Y, STRIP_Y0 - 3)
    for y in (ARROW_Y, ARROW_Y + 1):
        c.hline(lx + 1, ex, y)
    for x in (lx + 1, lx + 2):
        c.vline(x, ARROW_Y, STRIP_Y0 - 7)
    for k in range(5):                                 # arrowhead, pointing down at bar 1
        c.hline(lx + 1 - 4 + k, lx + 2 + 4 - k, STRIP_Y0 - 7 + k)
    c.px(ex - 1, ARROW_Y, 0); c.px(lx + 1, ARROW_Y, 0)  # round the arrow's two bends

    # --- right: SEGUE on white, a crossfader knocked out of the black block ---------
    c.rect(TXT_X0, ART_TOP, W - 1, ART_BOTTOM)
    ty = (SPLIT - ART_TOP - sum(ROWS)) // 2 + 1
    big_text(c, TITLE, TXT_X0 + (RECT_W - text_w(TITLE) + 1) // 2, ART_TOP + ty)
    for y in range(SPLIT + 1, ART_BOTTOM + 1):
        c.hline(TXT_X0, W - 1, y)
    my = (SPLIT + 1 + ART_BOTTOM) // 2                 # the fader's middle row
    x0, x1 = TXT_X0 + 8, W - 9
    for y in (my, my + 1):                             # the slot, 2 px
        c.hline(x0, x1, y, 0)
    for x in (x0, x1):                                 # end stops
        c.vline(x, my - 3, my + 4, 0)
    capx = x1 - 9                                      # the cap, pushed to the loop side
    for y in range(my - 5, my + 7):
        c.hline(capx, capx + 6, y, 0)
    c.vline(capx + 3, my - 3, my + 4, 1)               # the cap's grip line
    c.vline(capx - 1, my - 5, my + 6, 1)               # keep the cap clear of the slot
    c.vline(capx + 7, my - 5, my + 6, 1)
    for y in (my, my + 1):
        c.px(capx - 1, y, 0); c.px(capx + 7, y, 0)

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
