"""Generate src/airwindows/common/covers/Choral.json (128x64 cover override).

Concept, in the repo's 1-bit cover style (big repo-font title with a stencil
slit, dials under the firmware value boxes, bottom rule):
  * the name CHORAL on top,
  * under it a row of five identical pairs of lips (robot voices, cupid's bow),
    one per vowel, only the opening differs; the whole picture has a digital
    glitch pass (sliced scanlines, dropped rows, tracking dashes, noise). Shaped like the vowel (A wide open, E half open, I stretched thin,
    O a round ring, U small and pursed).

Run:  py src\\custom\\formant\\make_cover.py
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

TITLE = "CHORAL"
NAME = "Choral"
SCALE = 3
GAP = 1
TEXT_Y = 2
SLIT_ROW = 9
LABELS = ("VOWEL", "RESO", "CHORD")
HEAD_Y = (28, 27, 26, 27, 28)       # head centres, middle one on the top riser
HEAD_R = 9
VOWELS = "AEIOU"


def lips(c, cx, cy, vowel):
    """A pair of lips: solid, with a cupid's bow on the upper lip, the vowel's
    opening left empty and a small gloss mark on the lower lip. Squashed space
    (device rows are ~1.3x taller than wide)."""
    sq = _VSquash(c, cy)
    # robot chorus: every pair of lips has the same outer size, only the
    # opening (the vowel) differs. half-width, opening half-width / half-height
    w, tu, tl = 10, 3.4, 4.4
    wi, ho = {
        "A": (7.0, 5.0),
        "E": (8.5, 2.4),
        "I": (9.0, 0.6),
        "O": (4.0, 5.0),
        "U": (2.4, 3.0),
    }[vowel]
    for x in range(-w, w + 1):
        u = x / float(w)
        sh = math.sqrt(max(0.0, 1.0 - u * u))
        y0 = -(ho + tu) * sh
        if abs(u) < 0.4:                               # cupid's bow
            y0 += 2.2 * (1.0 - abs(u) / 0.4)
        y1 = (ho + tl) * sh
        if abs(x) <= wi:
            ui = x / float(wi)
            si = math.sqrt(max(0.0, 1.0 - ui * ui))
            o0, o1 = -ho * si, ho * si
        else:
            o0, o1 = 1.0, -1.0                         # no opening here
        for y in range(int(round(y0)), int(round(y1)) + 1):
            if o0 <= y <= o1 and ho > 0:
                continue
            sq.px(cx + x, cy + y)
    

def build():
    c = Canvas()

    # 1. Title: repo 3x5 font at scale 3, stencil slit, centred
    w = len(TITLE) * (3 * SCALE + GAP) - GAP
    tx = (128 - w) // 2
    m = Canvas()
    m.draw_text(TITLE, tx, TEXT_Y, scale=SCALE, spacing=GAP)
    main = set()
    for y in range(64):
        for x in range(128):
            if m.pixels[y][x]:
                main.add((x, y))
    for (x, y) in main:
        c.px(x, y)

    # 2. Five pairs of lips, one per vowel, evenly spaced
    pitch = 25
    x0 = 64 - 2 * pitch
    for i, v in enumerate(VOWELS):
        lips(c, x0 + i * pitch, 26, v)

    # 2b. Glitch pass (robotic voices): sliced + shifted scanlines, dropped rows,
    #     tracking dashes and a few noise blocks. Deterministic.
    import random
    rnd = random.Random(486)
    def shift_rows(y0, y1, dx):
        for y in range(y0, y1 + 1):
            row = c.pixels[y]
            new = [0] * 128
            for x in range(128):
                nx = x + dx
                if row[x] and 2 <= nx < 126:
                    new[nx] = 1
            c.pixels[y] = new
    # chromatic-aberration ghost: a dithered copy of the lips, 2 px right
    # and 1 px down, only where the crisp picture has no pixel
    crisp = [row[:] for row in c.pixels]
    for y in range(17, 35):
        for x in range(2, 124):
            if crisp[y][x] and not crisp[y + 1][x + 2] and (x + y) % 2 == 0:
                c.pixels[y + 1][x + 2] = 1
    # a few single-row slices, nudged 2-3 px on the lips and only 1 px on the
    # title (3 px made CHORAL hard to read)
    shift_rows(TEXT_Y + 4, TEXT_Y + 4, 1)
    shift_rows(TEXT_Y + 10, TEXT_Y + 10, -1)
    shift_rows(21, 21, 3)
    shift_rows(29, 29, -3)
    # short dropouts
    for x, y, n in ((18, 25, 4), (47, 27, 3), (73, 23, 4), (101, 28, 3)):
        for xx in range(x, x + n):
            c.pixels[y][xx] = 0
    # tracking dashes and noise in the gaps
    for y, x, n in ((18, 10, 9), (34, 70, 11), (18, 98, 7), (34, 20, 6)):
        for xx in range(x, x + n):
            if xx % 4 != 3:
                c.px(xx, y)
    for bx, by in ((5, 19), (56, 33), (119, 19), (92, 33), (39, 19)):
        for yy in range(by, by + 2):
            for xx in range(bx, bx + 3):
                if (xx + yy) % 2 == 0:
                    c.px(xx, yy)

    # 3. Knob labels and dials under the firmware value boxes
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        lw = len(label) * 4 - 1
        lx = max(2, cx - lw // 2)
        c.draw_text(label, lx, 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)

    pass                                      # no bottom rule
    return c


if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / f"{NAME}.json"
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (20, 22, 26))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (186, 228, 255))
    im.resize((128 * 6, 64 * 6), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
