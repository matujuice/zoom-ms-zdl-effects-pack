"""Generate src/airwindows/common/covers/DubSiren.json (128x64 cover override).

Concept, in the repo's 1-bit cover style (big title, dials under the firmware
value boxes, bottom rule), with reggae / dub flavour:
  * a big woofer in 3/4 perspective; the title and its echo are centred on the
    woofer's centre line and the upright "DUBSIREN" title
    (repo font, stencil slit) coming out from in front of it, as if the woofer
    were generating the name,
  * under the word, a smaller, fainter echo of it, like a delay tap fading.

Run:  py src\\custom\\dubsiren\\make_cover.py
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

TITLE = "DUBSIREN"
TEXT_X = 31
TEXT_Y = 6
SCALE = 3
SLIT_ROW = 9
GAP = 1                    # pixels between letters
LABELS = ("TRIG", "MODE", "PITCH")
TEXTURE_STREAKS = 60       # brushed-metal streaks
KNOB_TOP = 46              # top of the pedal's number boxes


def build():
    c = Canvas()

    # 2. Title mask: the repo's own 3x5 font at scale 3, upright, with a thin
    #    stencil slit through the letters
    m = Canvas()
    m.draw_text(TITLE, TEXT_X, TEXT_Y, scale=SCALE, spacing=GAP)
    bottom = TEXT_Y + 5 * SCALE - 1
    main = set()
    for y in range(64):
        for x in range(128):
            if m.pixels[y][x] and y != TEXT_Y + SLIT_ROW:
                main.add((x, y))
    x_max = max(p[0] for p in main)
    x_end = x_max

    # 3b. Woofer in 3/4 perspective BEHIND the first letter, as if it is what
    #     generates the name: the front face is an ellipse, the cone depth
    #     recedes up and to the left. The main word is drawn over it afterwards
    #     with a one-pixel gap, so the D cuts out of the woofer.
    def ell(cx, cy, rx, ry):
        pts = set()
        for i in range(720):
            a = i * math.pi / 360.0
            pts.add((int(round(cx + rx * math.cos(a))), int(round(cy + ry * math.sin(a)))))
        return pts

    def in_ell(x, y, cx, cy, rx, ry):
        return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0

    # front face only (no back rim / side shadow); the 3/4 view comes from the
    # inner parts sitting progressively further left, as if the cone recedes
    fx_, fy_, frx, fry = 30, 19, 9.0, 12.0
    for (x, y) in ell(fx_, fy_, frx, fry):                # outer rim, 2 px thick
        c.px(x, y)
    for (x, y) in ell(fx_, fy_, frx - 1.0, fry - 1.0):
        c.px(x, y)
    for (x, y) in ell(fx_ - 1, fy_, frx - 2.5, fry - 3.5):   # surround
        c.px(x, y)
    for (x, y) in ell(fx_ - 2, fy_, frx - 4.5, fry - 6.0):   # cone ridge (dithered)
        if (x + y) % 2 == 0:
            c.px(x, y)
    for yy in range(fy_ - 5, fy_ + 6):                     # dust cap, filled
        for xx in range(fx_ - 7, fx_ + 3):
            if in_ell(xx, yy, fx_ - 3.0, fy_, 2.8, 4.0):
                c.px(xx, yy)

    # keep the D itself clean: nothing of the woofer shows inside its cell
    for yy in range(TEXT_Y - 1, TEXT_Y + 5 * SCALE + 1):
        for xx in range(TEXT_X - 1, TEXT_X + 3 * SCALE + 1):
            c.px(xx, yy, 0)

    # 3c. Echo of the word: the name repeats under itself like delay taps, each
    #     repeat smaller and fainter than the one before (scale 2 at half
    #     density), centred under the title.
    for (esc, ey, esp, pat) in ((2, TEXT_Y + 5 * SCALE + 1, 2, 'half'),):
        em = Canvas()
        word_w = len(TITLE) * (3 * SCALE + GAP) - GAP
        ew = len(TITLE) * (3 * esc + esp) - esp
        em.draw_text(TITLE, TEXT_X + (word_w - ew) // 2, ey, scale=esc, spacing=esp)
        for y in range(64):
            for x in range(128):
                if em.pixels[y][x]:
                    if pat == 'half': keep = ((x + y) % 2 == 0)
                    else: keep = True
                    c.px(x, y, 1 if keep else 0)

    # 4. Main word on top, one-pixel gap around it so it stays readable
    for (x, y) in main:
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                c.px(x + dx, y + dy, 0)
    for (x, y) in main:
        c.px(x, y)

    # 4b. The metal enclosure: the whole graphic sits in a box like the steel case of a dub
    #     siren: a rounded outer wall, a thin inner panel edge a gap inside it (the folded
    #     lip of the sheet metal), and four screws with slots at different angles.
    BX0, BX1, BY0, BY1 = 0, 127, 0, 63                   # outer wall: the whole screen, knobs included
    for x in range(BX0 + 2, BX1 - 1):
        c.px(x, BY0); c.px(x, BY1)
    for y in range(BY0 + 2, BY1 - 1):
        c.px(BX0, y); c.px(BX1, y)
    for (x, y) in ((BX0 + 1, BY0 + 1), (BX1 - 1, BY0 + 1), (BX0 + 1, BY1 - 1), (BX1 - 1, BY1 - 1)):
        c.px(x, y)                                       # chamfered corners
    IX0, IX1, IY0, IY1 = 2, 125, 2, 61                   # inner lip
    for x in range(IX0, IX1 + 1):
        c.px(x, IY0); c.px(x, IY1)
    for y in range(IY0, IY1 + 1):
        c.px(IX0, y); c.px(IX1, y)
    for (sx, sy, ang) in ((6, 6, 0), (121, 6, 1), (6, 57, 1), (121, 57, 0)):
        for (dx, dy) in ((-2, -1), (-2, 0), (-2, 1), (2, -1), (2, 0), (2, 1), (-1, -2), (0, -2), (1, -2),
                         (-1, 2), (0, 2), (1, 2)):
            c.px(sx + dx, sy + dy)                       # round head, 5 px
        for dd in (-1, 0, 1):
            for ee in (-1, 0, 1):
                c.px(sx + dd, sy + ee, 0)                # hollow centre, then the slot
        for dd in (-1, 0, 1):
            if ang == 0: c.px(sx + dd, sy)               # slot -
            else: c.px(sx, sy + dd)                      # slot |
        c.px(sx, sy)

    # 5. Knob labels and dials under the firmware value boxes
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        lx = max(2, cx - w // 2)
        c.draw_text(label, lx, 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)

    # 6. Brushed metal: tiny horizontal scratches scattered evenly over the free space inside
    #    the lip: 2-4 px dashes, never closer than 2 px to the art, the text or the screws,
    #    spaced apart so it reads as a fine grain and not as noise, and not under the pedal's
    #    three number boxes.
    import random
    rnd = random.Random(11)
    def free(x, y, halo):
        for dy in range(-halo, halo + 1):
            for dx in range(-halo, halo + 1):
                xx, yy = x + dx, y + dy
                if 0 <= xx < 128 and 0 <= yy < 64 and c.pixels[yy][xx]:
                    return False
        return True
    boxes = [(kx - 5, KNOB_TOP - 5, kx + 27, 63) for (_k, kx, _y) in cc.knob_layout(3)]
    # a fine lattice of single pixels (the smallest mark the 128 x 64 screen can show), every
    # 3rd row, staggered row to row so it reads as grain, not as a grid. Like the title, the
    # art and the text keep a clear 3 px halo, and so do the pedal's number boxes.
    todo = []
    for y0 in range(IY0 + 2, IY1 - 1, 3):
        off = ((y0 // 3) * 2) % 5
        for x0 in range(IX0 + 2 + off, IX1 - 2, 5):
            if any(bx0 <= x0 <= bx1 and by0 <= y0 <= by1 for (bx0, by0, bx1, by1) in boxes):
                continue
            if free(x0, y0, 3):
                todo.append((x0, y0))
    for (x0, y0) in todo:
        c.px(x0, y0)

    return c, (0, x_max)


if __name__ == "__main__":
    c, bounds = build()
    print("title x range", bounds)
    out = ROOT / "src" / "airwindows" / "common" / "covers" / "DubSiren.json"
    out.write_text(json.dumps({"name": "DubSiren", "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (255, 255, 255))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (0, 0, 0))
    im.resize((128 * 6, 64 * 6), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
