"""Generate src/airwindows/common/covers/EuGate.json (128x64 cover override).

Concept: the Euclidean rhythm ring. 16 steps sit on a circle like a clock; the 5 hits of
E(5,16) are the full dots, joined into a polygon; the 11 rests are empty dots. On the left the name: EU in solid
letters, a faint dithered "CLIDIAN" after it (so it reads EUCLIDIAN) and GATE under it.
Black on white, like the pedal.

Run from anywhere:  py src\\custom\\eugate\\make_cover.py
Also writes cover_preview.png next to this script.
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/eugate/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "EuGate"
LABELS = ("NOTES", "STEPS", "SHIFT")
HITS = (0, 3, 6, 9, 12)            # Euclid(5, 16)
RCX, RCY, RR = 100, 18, 16          # ring centre and horizontal radius
ASPECT = 1.4                        # the LCD pixel is 1.4x taller than wide (build/lcd_geometry.py)
ART_BOTTOM = 35


def pt(s, r, cx=RCX, cy=RCY):
    a = -math.pi / 2 + 2 * math.pi * s / 16
    return cx + r * math.cos(a), cy + r * math.sin(a) / ASPECT   # squashed in y: round on the device


def line(c, x0, y0, x1, y1, every=1):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        if i % every:
            continue
        c.px(int(round(x0 + (x1 - x0) * i / n)), int(round(y0 + (y1 - y0) * i / n)))


def disc(c, cx, cy, r2):
    for y in range(cy - 3, cy + 4):
        for x in range(cx - 3, cx + 4):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r2:
                c.px(x, y)


def build():
    c = Canvas()

    # --- the name: EU solid, "CLIDIAN" a faint dither after it (EUCLIDIAN), GATE under ----------
    c.draw_text("EU", 4, 3, scale=2, spacing=2)                       # 14 x 10 px
    g = Canvas()
    g.draw_text("CLIDIAN", 20, 3, scale=2, spacing=2)                 # 54 x 10 px, same size as EU
    for y in range(64):
        for x in range(128):
            if g.pixels[y][x] and (x + y) % 2 == 0:
                c.px(x, y)
    c.draw_text("GATE", 4, 16, scale=2, spacing=2)                    # 30 x 10 px
    c.draw_text("5/16=3.3.3.3.4", 4, 29, scale=1, spacing=1)          # the maths: 5 hits in 16, the gaps

    # --- the ring: 16 dots on a circle, the 5 hits full and the 11 rests empty rings ------------
    for s in range(16):
        x, y = pt(s, RR)
        x0, y0 = int(round(x - 1.5)), int(round(y - 1.0))
        for dy in range(3):                       # 4 wide x 3 tall = about round on the device
            for dx in range(4):
                if dx in (0, 3) and dy in (0, 2):
                    continue                      # round off the corners
                if dx in (1, 2) and dy == 1 and s not in HITS:
                    continue                      # a rest: the inside is left empty
                c.px(x0 + dx, y0 + dy)

    # the dotted cross in the middle, like the axes of a graph
    for d in range(-12, 13, 3):
        c.px(RCX + d, RCY)
    for d in range(-8, 9, 2):
        c.px(RCX, RCY + d)

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
