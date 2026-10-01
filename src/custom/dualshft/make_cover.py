"""Generate src/airwindows/common/covers/DualShft.json (128x64 cover override).

Concept: the big title is the art. Two out-of-phase waves (the two voices /
the opposite-moving LFO) run straight through the letters: the top half of the
text rides one wave, the bottom half rides the opposite wave, and a wave line
is drawn across the text with inverted pixels so it stays visible on top of
the letters.

Run from anywhere:  py src\\custom\\dualshft\\make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script.
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/dualshft/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

TITLE = "DUAL SHIFT"
TEXT_Y = 7                 # top row of the 15-row title
TEXT_H = 15
MID = TEXT_Y + TEXT_H // 2           # row that splits the two halves
WAVE_CY = TEXT_Y + TEXT_H // 2        # waves run through the middle of the text
LABELS = ("PTCH1", "PTCH2", "DLY1")


def title_mask():
    m = Canvas()
    m.draw_text(TITLE, 9, TEXT_Y, scale=3, spacing=2)
    return m


def build():
    c = Canvas()
    mask = title_mask()

    # 1. Title, drawn solid so it stays readable.
    for y in range(64):
        for x in range(128):
            if mask.pixels[y][x]:
                c.px(x, y)

    # 2. The two voices: two big waves, opposite phase, run the full width and
    #    weave through the letters. In the open space voice 1 is a solid 2-pixel
    #    line and voice 2 a dotted line; where voice 1 passes through a letter it
    #    continues as a dashed cut (one pixel in three is knocked out), so the
    #    wave clearly runs across the word while the letters stay readable.
    lam = 40.0
    halo = set()
    for y in range(64):
        for x in range(128):
            if mask.pixels[y][x]:
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        halo.add((x + dx, y + dy))
    for x in range(3, 125):
        s = math.sin(2 * math.pi * (x - 9) / lam)
        ya = int(round(WAVE_CY - 9.0 * s))
        yb = int(round(WAVE_CY + 9.0 * s))
        for yy in (ya, ya + 1):
            if mask.pixels[yy][x]:
                if x % 3 == 0 and yy == ya:          # dashed cut through the letter
                    c.px(x, yy, 0)
            elif (x, yy) not in halo:
                c.px(x, yy)
        if (x & 1) == 0 and (x, yb) not in halo:
            c.px(x, yb)

    # 3. Small arrows at the ends (pitch up / pitch down) and a thin trace.
    for i in range(4):                       # up arrow, left
        c.px(10 - i, 27 + i); c.px(10 + i, 27 + i)
    c.vline(10, 27, 34); c.vline(9, 28, 34); c.vline(11, 28, 34)
    for i in range(4):                       # down arrow, right
        c.px(117 - i, 34 - i); c.px(117 + i, 34 - i)
    c.vline(117, 27, 34); c.vline(116, 27, 33); c.vline(118, 27, 33)
    for x in range(20, 108):
        yy = int(round(30.5 + 2.5 * math.sin(2 * math.pi * (x - 14) / lam * 0.5)))
        if (x % 3) != 2:
            c.px(x, yy)

    # 4. Knob labels and dials under the firmware value boxes (page 1 knobs).
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
    out = ROOT / "src" / "airwindows" / "common" / "covers" / "DualShft.json"
    out.write_text(json.dumps({"name": "DualShft", "w": 128, "h": 64, "grid": c.pixels}))
    from PIL import Image
    im = Image.new("RGB", (128, 64), (20, 22, 26))
    for y in range(64):
        for xx in range(128):
            if c.pixels[y][xx]:
                im.putpixel((xx, y), (186, 228, 255))
    im.resize((128 * 6, 64 * 6), Image.NEAREST).save(
        str(Path(__file__).resolve().parent / "cover_preview.png"))
    print("wrote", out)
