"""Generate src/airwindows/common/covers/GRDELAY.json (128x64 cover override).

GRDELAY, the GRDLOOP cover's pair: the name in GRDLOOP's big font, under it GRDLOOP's rounded
2 px bar split into 8 grid cells (gaps in the edges, dotted lines). The first cell is the hit,
then one echo block per cell, the same size, each repeat a lighter dither. Knob row as GRDLOOP.
(First built as BarDelay, renamed GridDly, renamed GRDELAY on 2026-10-30.)

Run from anywhere:  py src\\custom\\griddly\\make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script (LCD look, build/lcd_render.py).
"""
import importlib.util, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("grdloop_cover", ROOT / "src" / "custom" / "looper" / "make_cover.py")
g = importlib.util.module_from_spec(spec)
spec.loader.exec_module(g)                      # GRDLOOP's helpers; its __main__ does not run

NAME = "GRDELAY"
LABELS = ("TYPE", "TIME", "FDBK")
W, BARS, Y0, Y1 = g.W, g.BARS, g.BAR_Y0, g.BAR_Y1
X0, X1 = 2, W - 3                               # inside the stroke
BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]
FONT_Y = ["101", "101", "010", "010", "010"]    # GRDLOOP's clearer Y


def title(c):
    t = g.Canvas()
    g.Canvas._FONT = dict(g.Canvas._FONT, Y=FONT_Y)
    tx = (W - g.text_w(NAME) + 1) // 2
    g.big_text(t, NAME, tx, 1)
    ink = {(x, y) for y in range(Y0 - 1) for x in range(W) if t.pixels[y][x]}
    for x, y in ink:                            # 1 px of air, as GRDLOOP
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if 0 <= x + dx < W and 0 <= y + dy < 64:
                    c.px(x + dx, y + dy, 0)
    for x, y in ink:
        c.px(x, y)


def gx(i):
    return X0 + (X1 - X0 + 1) * i // BARS


def build():
    c = g.Canvas()
    title(c)
    for d in (0, 1):
        c.rect(d, Y0 + d, W - 1 - d, Y1 - d)
    by0, by1 = Y0 + 4, Y1 - 4                   # blocks: 2 px of white inside the stroke
    for k in range(7):                          # cell 0 = the hit, cells 1..6 = repeats
        bx0, bx1 = gx(k) + 3, gx(k + 1) - 3
        dens = 1.0 if k == 0 else 0.8 * 0.72 ** (k - 1)
        for y in range(by0, by1 + 1):
            for x in range(bx0, bx1 + 1):
                if BAYER[y & 3][x & 3] < dens * 16:
                    c.px(x, y)
    for i in range(1, BARS):                    # GRDLOOP's grid: edge gaps and dotted lines
        for y in (Y0, Y0 + 1, Y1 - 1, Y1):
            c.px(gx(i), y, 0)
        for y in range(Y0 + 2, Y1 - 1, 2):
            c.px(gx(i), y, 1 - c.pixels[y][gx(i)])
    for x, y in ((0, 0), (1, 0), (0, 1)):       # round corners, radius 2
        for px_, py_ in ((x, Y0 + y), (W - 1 - x, Y0 + y), (x, Y1 - y), (W - 1 - x, Y1 - y)):
            c.px(px_, py_, 0)
    for px_, py_ in ((1, Y0 + 1), (W - 2, Y0 + 1), (1, Y1 - 1), (W - 2, Y1 - 1)):
        c.px(px_, py_)
    for (kid, kx, ky), label in zip(g.cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        c.draw_text(label, max(2, cx - w // 2), 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = g._VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)
    return c


if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / f"{NAME}.json"
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    from lcd_render import save_png             # build/ is on sys.path via the GRDLOOP module
    save_png(c.pixels, Path(__file__).resolve().parent / "cover_preview.png")
    print("wrote", out)
