"""Generate src/airwindows/common/covers/SGNL.json (128x64 cover override) for S.GN_L.

Concept (agreed with Luca, 2026-10-02): the title S.GN_L in blocky letters with corners
clipped by one pixel, centred in the art rows 0..34. It was full width (27 px tall) at
first; Luca asked for it in line with the other covers, so it is now 19 px tall (SCALE M,
the default; SG_SCALE=L or S picks the full-width or a 15 px version). Behind
the "." and the "_" stand sparse dotted ghosts of the I and the A the signal lost (the
"." is the foot of the I that survived), so it reads SIGNAL. The damage: the top of the
L's stem stuck and repeated sideways (the second copy dithered), the top of the S lifted
off, the L's foot slipped, ten short XOR glitch lines kept mostly in the gaps and off the
N, and a torn band sliding right. The damage leaks into the knob labels: LOSS has its
lower half slipped, SIZE lost a row, CODEC's last letter repeats like a stuck packet.
No bottom rule.

Run from anywhere:  py src\\custom\\sgnl\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/sgnl/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "SGNL"                                      # effect_name: the cover file is covers/SGNL.json

W, H = 128, 64
LABELS = ("LOSS", "SIZE", "CODEC")
AH = 35                          # art rows 0..34

# --- wide title glyphs on a 5 x 7 grid ---------------------------------------------
import os
SCALE = os.environ.get("SG_SCALE", "M")
if SCALE == "L":                 # the first, full-width version (27 px tall)
    COLW, ROWH, DOTW, LGAP = (5, 3, 4, 3, 5), (4, 4, 4, 3, 4, 4, 4), 5, 4
elif SCALE == "S":               # 15 px tall
    COLW, ROWH, DOTW, LGAP = (3, 2, 2, 2, 3), (2, 2, 2, 3, 2, 2, 2), 3, 3
else:                            # 19 px tall: a bit bigger than the other titles
    COLW, ROWH, DOTW, LGAP = (4, 2, 3, 2, 4), (3, 3, 2, 3, 2, 3, 3), 4, 4
GW, GH = sum(COLW), sum(ROWH)
G = {
    "S": ["#####", "#....", "#....", "#####", "....#", "....#", "#####"],
    "G": ["#####", "#....", "#....", "#..##", "#...#", "#...#", "#####"],
    "N": ["#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#"],   # stems; diagonal drawn in title()
    "L": ["#....", "#....", "#....", "#....", "#....", "#....", "#####"],
    ".": [".", ".", ".", ".", ".", ".", "#"],
    "_": [".....", ".....", ".....", ".....", ".....", ".....", "#####"],
}
DROP = {}                        # the underscore sits on the baseline with the others
TX = (128 - (5 * GW + DOTW + 5 * LGAP)) // 2      # centred
TY = (35 - GH) // 2                               # centred in the art rows 0..34


def Y(f):
    """A row of the first 27-px design, scaled to this title height."""
    return TY + round(f * GH / 27)


def title():
    """Clean title as a grid art[y][x] (0/1) and each letter's x range."""
    art = [[0] * W for _ in range(AH)]
    spans, x = [], TX
    for ch in "S.GN_L":
        g = G[ch]
        widths = COLW if len(g[0]) > 1 else (DOTW,)
        yy = TY + DROP.get(ch, 0)
        for r, row in enumerate(g):
            xx = x
            for k, bit in enumerate(row):
                if bit == "#":
                    for dy in range(ROWH[r]):
                        for dx in range(widths[k]):
                            art[yy + dy][xx + dx] = 1
                xx += widths[k]
            yy += ROWH[r]
        if ch == "N":                                    # one unbroken diagonal, stem to stem
            h = sum(ROWH)
            for dy in range(h):
                x0 = x + round(dy * (GW - COLW[0]) / (h - 1))
                for dx in range(COLW[0]):
                    art[TY + dy][x0 + dx] = 1
        spans.append((ch, x, x + sum(widths) - 1))
        x += sum(widths) + LGAP
    return art, {ch: (a, b) for ch, a, b in spans}


# --- damage primitives ---------------------------------------------------------------
def shift(art, y0, y1, dx, x0=0, x1=W - 1, wrap=False):
    for y in range(y0, y1 + 1):
        seg = art[y][x0:x1 + 1]
        n = len(seg)
        new = [0] * n
        for i, v in enumerate(seg):
            j = i + dx
            if wrap:
                j %= n
            if 0 <= j < n:
                new[j] = v
        art[y][x0:x1 + 1] = new


def clear(art, x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            art[y][x] = 0


def copy(art, x0, y0, x1, y1, dx, dy, dither=False):
    src = [[art[y][x] for x in range(x0, x1 + 1)] for y in range(y0, y1 + 1)]
    for j, row in enumerate(src):
        for i, v in enumerate(row):
            x, y = x0 + i + dx, y0 + j + dy
            if 0 <= x < W and 0 <= y < AH and (not dither or (x + y) % 2 == 0):
                art[y][x] = v


def lcg(seed):
    while True:
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        yield seed >> 8


def round_corners(art):
    """Clip each outer corner of the letters by one pixel, like the pack's rounded boxes."""
    on = lambda x, y: 0 <= x < W and 0 <= y < AH and art[y][x]
    cut = []
    for y in range(AH):
        for x in range(W):
            if art[y][x]:
                for dx, dy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
                    if not on(x + dx, y) and not on(x, y + dy):
                        cut.append((x, y))
                        break
    for x, y in cut:
        art[y][x] = 0


def damage(art, sp):
    """The agreed damage: stuck L, lifted S, slipped foot, glitch lines, torn band."""
    a, b = sp["L"]
    st = COLW[0]
    for k, off in enumerate((st + 1, 2 * st + 2)):       # top of the L's stem stuck, repeated
        copy(art, a, TY, a + st - 1, Y(8), off, 0, dither=(k == 1))
    na, nb = sp["N"]
    s0, s1 = sp["S"]
    top = ROWH[0]
    copy(art, s0, TY, s1, TY + top - 1, 1, -2)           # top of the S lifted off
    clear(art, s0, TY + top - 2, s1, TY + top - 1)
    shift(art, TY + GH - ROWH[6], TY + GH - 1, 3, sp["L"][0], W - 1)   # the L's foot slipped
    r = lcg(7 if SCALE == "L" else 2)
    placed = 0
    while placed < 10:                                   # glitch lines: short dashes, XORed
        x, y = next(r) % W, next(r) % AH
        if SCALE != "L" and not (TX - 6 <= x <= W - TX + 2 and TY - 5 <= y <= TY + GH + 4):
            continue                                     # stay near the title
        n = 2 + next(r) % 5                              # 2..6 px long
        if na - 2 <= x + n and x <= nb + 2:
            continue                                     # keep the N clean
        if sum(art[y][min(W - 1, x + i)] for i in range(n)) > n // 2:
            continue                                     # mostly in the gaps, so letters stay whole
        placed += 1
        for i in range(n):
            if x + i < W:
                art[y][x + i] ^= 1
    shift(art, Y(10), Y(12), 3)                          # torn band slides right
    shift(art, Y(13), Y(13), 1)


# --- knob row with the damage leaking into it -----------------------------------------
def knob_row(c):
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        c.draw_text(label, max(2, cx - w // 2), 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)


def leak(c):
    """LOSS: lower half slipped; SIZE: a lost row; CODEC: last letter repeats."""
    def shift_rows(x0, x1, y0, y1, dx):
        for y in range(y0, y1 + 1):
            row = [c.pixels[y][x] for x in range(x0, x1 + 1)]
            for x in range(x0, x1 + 1):
                c.px(x, y, 0)
            for i, v in enumerate(row):
                if v and 0 <= x0 + i + dx < W:
                    c.px(x0 + i + dx, y)
    shift_rows(0, 40, 39, 41, 2)
    for x in range(44, 80):
        c.px(x, 39, 0)
    xs = [x for x in range(84, W) for y in range(37, 42) if c.pixels[y][x]]
    last = max(xs)
    for k, off in enumerate((4, 8)):
        for y in range(37, 42):
            for x in range(last - 2, last + 1):
                if c.pixels[y][x] and (k == 0 or (x + y) % 2 == 0) and x + off < W:
                    c.px(x + off, y)


GHOST = {".": ("I", ["#", "#", "#", "#", "#", "#", "."]),
         "_": ("A", ["..#..", ".#.#.", "#...#", "#####", "#...#", "#...#", "#...#"])}


def ghosts(art, sp):
    """Faint dithered I and A standing where the "." and the "_" are: the real letters
    the signal lost. The "." is the foot of the I that survived."""
    for ch, (_, g) in GHOST.items():
        x0 = sp[ch][0]
        widths = COLW if len(g[0]) > 1 else (DOTW,)
        yy = TY
        for r, row in enumerate(g):
            xx = x0
            for k, bit in enumerate(row):
                if bit == "#":
                    for dy in range(ROWH[r]):
                        for dx in range(widths[k]):
                            x, y = xx + dx, yy + dy
                            if x % 2 == 0 and y % 2 == 0:     # a sparse 1-in-4 dot screen
                                art[y][x] = 1
                xx += widths[k]
            yy += ROWH[r]


def build():
    c = Canvas()
    art, sp = title()
    round_corners(art)
    damage(art, sp)
    ghosts(art, sp)
    for y in range(AH):
        for x in range(W):
            if art[y][x]:
                c.px(x, y)
    knob_row(c)
    leak(c)
    return c


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
