"""Generate src/airwindows/common/covers/WaveFold.json (128x64 cover override).

Concept: a clean title on top, and under it a sine wave that grows from left to
right inside two dotted limit lines. Past the lines it does not clip, it FOLDS:
the solid trace is reflected back inside while a dotted "ghost" shows where the
wave would have gone, and a small spike marks every bounce. The title is small and sits on the wave, with a 1 px gap cleared around it.

Run from anywhere:  py src\\custom\\wavefold\\make_cover.py
Also writes cover_preview.png next to this script (black on white, like the pedal's screen).
"""
import json, math, random, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/wavefold/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "WaveFold"
TITLE = "WAVEFOLD"
LABELS = ("DRIVE", "SYMM", "TONE")
SCALE = 1                    # title letters: 3 x 5 px
PITCH = 10                   # letter pitch
LW = 8                       # letter width (the 6 px font stretched to 8)
STRETCH = (0, 1, 2, 2, 3, 3, 4, 5)   # output column -> font column (stems stay 2 px, the middle widens)
TITLE_X, TITLE_Y = 24, 11    # 79 px wide with the word gap, 15 tall: centred on the screen
CY, LIM = 18, 11              # wave centre row, fold limit in pixels
PERIOD = 69.7                # wave period in pixels
WAVE_OFF = -6.4              # phase: a fold peak sits in the middle of each side window
ART_BOTTOM = 35              # the art lives in rows 0..35, labels start at 37
SEED = 11


def fold(v):
    """Triangle fold, period 4: v for |v| <= 1, reflected beyond."""
    u = (v * 0.25 + 0.25) % 1.0
    return 1.0 - abs(4.0 * u - 2.0)


def build():
    rnd = random.Random(SEED)
    W, H = 128, 64
    title = Canvas()
    title.draw_text(TITLE, 0, 0, scale=2, spacing=2)     # 6 x 10 letters, 8 px pitch
    # stretch the 10 rows to 15 (rows 1, 3, 5, 7, 9 doubled) so the letters are tall
    T = [[False] * W for _ in range(H)]
    for k, srow in enumerate((0, 1, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9)):
        for li in range(8):                      # each letter: 6 font columns stretched to LW
            for ox in range(LW):
                T[TITLE_Y + k][TITLE_X + li * PITCH + ox] = bool(title.pixels[srow][li * 8 + STRETCH[ox]])
    for y in range(H):                           # 1 px extra gap before FOLD
        for x in range(W - 1, TITLE_X + 4 * PITCH - 1, -1):
            T[y][x] = T[y][x - 1]
        T[y][TITLE_X + 4 * PITCH - 1] = False
    C = [[False] * W for _ in range(H)]

    def put(x, y):
        if 0 <= x < W and 0 <= y <= ART_BOTTOM:
            C[y][x] = True

    S = [[False] * W for _ in range(H)]          # the solid (folded) trace, merged into C later
    def puts(x, y):
        if 0 <= x < W and 0 <= y <= ART_BOTTOM:
            S[y][x] = True

    A = 1.5                                    # drive: peaks reach 1.6 x the fold limit
    def raw(x):
        return A * math.sin(2 * math.pi * (x - WAVE_OFF) / PERIOD)
    def ypix(v):
        return CY - LIM * v

    # fold limit lines, dotted: one dot every 3 px, the same spacing as the ghost below
    for x in range(2, W, 3):                   # dots at 2, 5 .. 125: the same margin at both ends
        put(x, CY - LIM)
        put(x, CY + LIM + 1)
    # unfolded sine, the part the fold cuts off: a dotted ghost with the dots spaced evenly
    # ALONG the curve (every 3 px of path), so slopes and tops look as regular as the flats
    run = 0.0
    px_, py_ = None, None
    for i in range(0, W * 8 + 1):
        x = i / 8.0
        v = raw(x)
        if abs(v) <= 1.0:
            px_ = None
            run = 3.0                          # a dot right where the wave leaves the limit
            continue
        xx, yy = x, ypix(v)
        if px_ is not None:
            run += math.hypot(xx - px_, yy - py_)
        px_, py_ = xx, yy
        if run >= 3.0:
            gx, gy = int(round(xx)), int(round(yy))
            # never touch another dot (the limit line's, or the one before): a lone dot
            # reads as a dot, two touching ones read as a smudge
            if any(0 <= gy + dy < H and 0 <= gx + dx < W and C[gy + dy][gx + dx]
                   for dy in (-1, 0, 1) for dx in (-1, 0, 1)):
                continue
            put(gx, gy)
            run = 0.0
    # folded sine: solid, 2 px thick, no gaps
    prev = None
    for x in range(0, W):
        y = int(round(ypix(fold(raw(x)))))
        puts(x, y)
        if prev is None or abs(y - prev) <= 1:
            puts(x, y + 1)                       # slightly thicker on the flatter parts
        if prev is not None and abs(y - prev) > 1:
            lo, hi = min(y, prev), max(y, prev)
            for yy in range(lo + 1, hi):
                puts(x if yy >= (lo + hi) / 2 else x - 1, yy)
        prev = y
    # the solid trace is dropped behind the title: only slivers of it showed there
    for yy in range(H):
        for xx in range(W):
            if S[yy][xx] and not (TITLE_X - 2 <= xx <= TITLE_X + 7 * PITCH + LW + 3):
                C[yy][xx] = True

    # title interplay: clear a 1 px halo around the letters, then draw them.
    # WAVE is drawn normally; FOLD is inverted (solid block, letters knocked out).
    FX0 = TITLE_X + 4 * PITCH             # block left edge (1 px before the F)
    FX1 = TITLE_X + 7 * PITCH + LW + 1             # block right edge (1 px after the D)
    FY0, FY1 = TITLE_Y - 1, TITLE_Y + 15       # one row above and below the letters
    for y in range(H):
        for x in range(W):
            near = any(T[y + dy][x + dx]
                       for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                       if 0 <= y + dy < H and 0 <= x + dx < W)
            inblock = TITLE_X - 2 <= x <= FX1 + 1 and FY0 - 1 <= y <= FY1 + 1
            if (near or inblock) and y <= ART_BOTTOM:
                C[y][x] = False
    for y in range(H):
        for x in range(W):
            if FX0 <= x <= FX1 and FY0 <= y <= FY1:
                C[y][x] = not T[y][x]
            elif T[y][x]:
                C[y][x] = True

    # --- labels and dials under the firmware value boxes (page 1 knobs) --------
    c = Canvas()
    for y in range(H):
        for x in range(W):
            if C[y][x]:
                c.px(x, y)
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        lx = max(2, cx - w // 2)
        c.draw_text(label, lx, 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)
    pass                                      # no bottom rule
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
