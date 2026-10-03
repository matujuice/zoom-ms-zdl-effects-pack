"""Generate src/airwindows/common/covers/Scrub.json (128x64 cover override).

Concept (after DualShft: the big title is the art): SCRUB across the screen, centred,
and a playhead scrubbing through it. Left of the head the letters are solid (already
played), right of the head they are dithered (still to come). The head is a 2 px line
with a cap on top that runs down onto a timeline under the title: solid behind the
head, dotted ahead of it, arrows at both ends (you drag it both ways), and the grain
just before the head drawn thick (the slice that loops when you stop).

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
COLS = (3, 9, 3)             # font column -> px: 3 px stems, letters 15 wide
ROWS = (3, 3, 3, 3, 3)       # font row -> px: letters 15 tall, as DualShft
LGAP = 8
TITLE = "SCRUB"
TY = 6                       # title rows 6..20
HEAD = 63                    # playhead x (2 px: 63, 64), the middle of the screen
LINE_Y = 28                  # timeline row
LX0, LX1 = 10, 117           # timeline ends (arrows outside them)
GRAIN = 10                   # thick grain on the timeline, just before the head


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
            if x < HEAD - 1:
                c.px(x, y)
            elif x > HEAD + 2 and (x + y) % 2 == 0:
                c.px(x, y)

    # --- the playhead: 2 px with a 1 px clear gap either side, a cap on top ------------
    top, bot = 1, LINE_Y + 3
    for x in (HEAD, HEAD + 1):
        c.vline(x, top + 2, bot)
    c.hline(HEAD - 2, HEAD + 3, top)
    c.hline(HEAD - 1, HEAD + 2, top + 1)

    # --- the timeline: solid behind, dotted ahead, the grain thick, arrows at the ends -
    c.hline(LX0, HEAD - 2, LINE_Y)
    for x in range(HEAD + 3, LX1 + 1, 2):
        c.px(x, LINE_Y)
    for y in (LINE_Y - 1, LINE_Y + 1):
        c.hline(HEAD - 1 - GRAIN, HEAD - 2, y)
    for k in range(4):
        c.vline(LX0 - 6 + k, LINE_Y - k, LINE_Y + k)          # left arrow
        c.vline(LX1 + 6 - k, LINE_Y - k, LINE_Y + k)          # right arrow

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
