"""Generate src/airwindows/common/covers/Scrub.json (128x64 cover override).

Concept (after DualShft: the big title is the art): SCRUB across the screen, centred,
and a playhead scrubbing through it. Left of the head the letters are solid (already
played), right of the head they are dithered (still to come). The head is a 2 px line
with a cap on top that runs down onto a timeline under the title: solid behind the
head, dotted ahead of it, arrows at both ends (you drag it both ways). The grain, the
slice around the head that loops when you stop, is a selection centred on the head,
over the title and the timeline, like a region in a sample editor: black fill, the
letters inside solid and inverted, its sides dithered ramps (the crossfade).

Run from anywhere:  py src\\custom\\scrub\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, math, os, sys
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
COLS = (3, 8, 3)             # font column -> px: 3 px stems, letters 14 wide (title 102 wide:
                             # even, so it centres on the 2 px head exactly)
ROWS = (3, 3, 3, 3, 3)       # font row -> px: letters 15 tall, as DualShft
LGAP = 8
TITLE = "SCRUB"
TY = 6                       # title rows 6..20
HEAD = 63                    # playhead x (2 px: 63, 64), the middle of the screen
HEAD_LINE = os.environ.get("SCRUB_HEAD_LINE", "1") == "1"   # 0 = only the arrow on top
LINE_Y = 28                  # timeline row
LX0, LX1 = 10, 117           # timeline ends (arrows outside them)
SEL_HALF = 11                # the selection reaches 11 columns either side of the head
RAMP = 3                     # crossfade columns at each end of the selection
SEL_Y0, SEL_Y1 = TY - 2, LINE_Y + 2   # the selection runs from above the title to below the timeline


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


def build():
    c = Canvas()

    # --- the title, centred: solid behind the head, dithered ahead of it ---------------
    m = Canvas()
    tx = (W - text_w(TITLE)) // 2
    big_text(m, TITLE, tx, TY)
    for y in range(H):
        for x in range(W):
            if not m.pixels[y][x]:
                continue
            if x < HEAD - 1 or HEAD - SEL_HALF <= x <= HEAD + 1 + SEL_HALF:
                c.px(x, y)                        # played, or inside the selection: solid
            elif x > HEAD + 2 and (x + y) % 2 == 0:
                c.px(x, y)

    # --- the playhead: 2 px with a 1 px clear gap either side, a cap on top ------------
    top, bot = 1, LINE_Y + 3
    if HEAD_LINE:
        for x in (HEAD, HEAD + 1):
            c.vline(x, top + 2, bot)
    c.hline(HEAD - 2, HEAD + 3, top)
    c.hline(HEAD - 1, HEAD + 2, top + 1)

    # --- the timeline: solid behind, dotted ahead, the grain thick, arrows at the ends -
    c.hline(LX0, HEAD - 2 if HEAD_LINE else HEAD + 1, LINE_Y)
    for x in range(HEAD + 3, LX1 + 1, 2):
        c.px(x, LINE_Y)
    for k in range(4):
        c.vline(LX0 - 6 + k, LINE_Y - k, LINE_Y + k)          # left arrow
        c.vline(LX1 + 6 - k, LINE_Y - k, LINE_Y + k)          # right arrow

    # --- the grain: a selection around the head, like a region in a sample editor ----
    # Centred on the head: the grain is read around the head. Inside, everything is
    # inverted (black fill, white letters, white head); over RAMP columns at each side the
    # inversion fades in through 25 / 50 / 75 % ordered dither: the crossfade.
    x0, x1 = HEAD - SEL_HALF, HEAD + 1 + SEL_HALF
    for y in range(SEL_Y0, SEL_Y1 + 1):
        for x in range(x0, x1 + 1):
            d = min(x - x0, x1 - x) + 1                     # 1 at the outer columns
            if d <= RAMP:
                share = d / (RAMP + 1)                      # 0.25, 0.5, 0.75 inward
                xm = x - x0 if x - x0 < x1 - x else x1 - x   # mirrored: both sides match
                bayer = ((0, 2), (3, 1))[y % 2][xm % 2] / 4.0 + 0.125
                if bayer > share:
                    continue
            c.px(x, y, 0 if c.pixels[y][x] else 1)

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
