"""Generate src/airwindows/common/covers/Breather.json (128x64 cover override).

CHOSEN COVER (build_ds, Luca 2026-10-06): BREATHER, one letter per step of an 8-step
grid, all one size. Two kick envelopes run through the letters: under each curve the
letters are the sparse dot screen (ducked), above it solid ink, so B and T are mostly dots
and A and the last R are solid. The curve is dotted between the letters; under the title
a down arrow marks each kick and a dotted line shows the gain dropping and recovering.
The earlier rounds below (build, build_merged) are kept for reference.

Concept (Luca, 2026-10-05): an 8-step grid across the art area, one letter of BREATHER
per step, all the same size, and the letters DUCK the way the effect does: the letter on
a kick step is a sparse dotted ghost (the dot screen of S.GN_L's lost letters) and the
next letters fill back in, half dots, three-quarter dots, solid. A row of step pads
under the letters is replaced by the amp envelopes of the two kicks: as each kick dies
away the letters fill back in (Luca, 2026-10-05).

VARIANT "two": kicks on steps 1 and 5 (16ths, Div 1/4: two beats).
VARIANT "one": one duck over the whole word (Div 1/2).

Run from anywhere:  py src\\custom\\breather\\make_cover.py [out.png]
Also writes cover_preview.png next to this script (dark blue on white, stretched 1.4x).
"""
import json, math, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "Breather"
WORD = "BREATHER"
LABELS = ("TARGT", "SHAPE", "DEPTH")
W = 128
CELL = 16                    # 8 steps x 16 px
TOP = 1                      # letters in rows 1..19
COLS = (4, 4, 4)             # font column -> px: letters 12 wide
ROWS = (4, 4, 3, 4, 4)       # font row -> px: letters 19 tall
ENV_TOP, ENV_BASE = 22, 33   # kick envelopes under the letters
ART_H = 35                   # art rows 0..34
M_ROWS = (5, 4, 4, 4, 5)     # merged version: letters 22 tall
DS_ROWS = (4, 4, 3, 4, 4)    # DualShft-style version: letters 19 tall
DS_TOP = 2
DS_INVERT = False            # cut the curve into solid letter pixels too
DS_SINK = 0                  # px the most ducked letter sinks
DS_GAIN = (29, 34)           # rows of the dotted gain line
MERGE_CURVE = False          # draw the curve in the gaps between letters
MERGE_DECAY = 3.6            # merged version: envelope decay over one beat
ENV_FADE = True              # envelope fill fades solid -> sparse over the beat
ENV_DECAY = 2.6              # decay rate over one beat (4 steps)

# The duck as ink density: 0 = sparse 1-in-4 dot screen (the kick, ducked furthest),
# 1 = half the dots, 2 = three in four, 3 = solid (recovered). Kicks on steps 1 and 5.
LEVELS = {"two": (0, 1, 2, 3, 0, 1, 2, 3), "one": (0, 0, 1, 1, 2, 2, 3, 3)}
KICKS = {"two": (0, 4), "one": (0,)}

def ink(level, x, y):
    if level >= 3: return True
    if level == 2: return not (x % 2 == 1 and y % 2 == 1)
    if level == 1: return (x + y) % 2 == 0
    return x % 2 == 0 and y % 2 == 0

def letter(c, ch, x, y, level):
    for r, row in enumerate(Canvas._FONT[ch]):
        xx = x
        for k, bit in enumerate(row):
            if bit == "1":
                for dy in range(ROWS[r]):
                    for dx in range(COLS[k]):
                        if ink(level, xx + dx, y + dy):
                            c.px(xx + dx, y + dy)
            xx += COLS[k]
        y += ROWS[r]

def build_merged():
    """Alternative: the envelopes run THROUGH the letters. Each kick's envelope jumps to the
    top of the letters on the kick and decays over the beat. Letter pixels under the curve
    are the sparse dot screen (ducked), pixels above it are solid (breathing again), so B
    and T are all dots, A and the last R all ink, and the letters between are dotted at
    the bottom and solid on top. The curve itself is drawn in the gaps between letters."""
    c = Canvas()
    lw = sum(COLS)
    x_first = (CELL - lw) // 2
    span = 4 * CELL
    rows = M_ROWS
    top = (ART_H - sum(rows)) // 2
    base = top + sum(rows) - 1
    curve = [None] * W                           # first row under the envelope, per column
    for k in KICKS["two"]:
        xa = x_first + k * CELL
        xb = xa + span - 1 - (CELL - lw) if k == 0 else W - 1 - x_first
        for x in range(xa, xb + 1):
            t = (x - xa) / float(span)
            curve[x] = round(top - 1 + (base + 2 - top) * (1 - math.exp(-MERGE_DECAY * t)))
    inside = [[False] * W for _ in range(64)]
    for i, ch in enumerate(WORD):
        x, y = i * CELL + x_first, top
        for r, row in enumerate(Canvas._FONT[ch]):
            xx = x
            for kk, bit in enumerate(row):
                for dy in range(rows[r]):
                    for dx in range(COLS[kk]):
                        inside[y + dy][xx + dx] = True
                        if bit == "1":
                            X, Y = xx + dx, y + dy
                            under = curve[X] is not None and Y >= curve[X]
                            if not under or ink(0, X, Y):
                                c.px(X, Y)
                xx += COLS[kk]
            y += rows[r]
    prev = None
    for x in (range(W) if MERGE_CURVE else ()):  # the curve, in the gaps only
        if curve[x] is None:
            prev = None
            continue
        y0 = curve[x] if prev is None or curve[x] > prev else prev
        y1 = base + 1 if prev is None else curve[x]
        for y in range(min(y0, y1, curve[x]), max(y0, y1, curve[x]) + 1):
            if 0 <= y < 64 and not inside[y][x] and y <= base + 1:
                c.px(x, y)
        prev = curve[x]
    labels(c)
    return c

def build_ds():
    """DualShft-style take on the merged cover (Luca, 2026-10-06). The kick envelopes still
    run through the letters (dots under the curve, ink above), and on top of that:
      - the ducked letters sink: each letter drops by up to DS_SINK px with the envelope
        at its centre, so the word dips on B and T and stands back up by A and R;
      - the envelope curve is drawn across the title, dotted between the letters (inside
        them the dots-to-ink edge is the curve);
      - under the title, the gain the effect applies (the duck: drops on the kick and
        recovers) as a dotted line, with a down arrow on each kick."""
    c = Canvas()
    lw = sum(COLS)
    x_first = (CELL - lw) // 2
    span = 4 * CELL
    rows = DS_ROWS
    top = DS_TOP
    base = top + sum(rows) - 1 + DS_SINK
    env = [None] * W                             # 1 at the kick .. 0, per column
    for k in KICKS["two"]:
        xa = x_first + k * CELL
        xb = xa + span - 1 if k == 0 else W - 1
        for x in range(xa, min(W, xb + 1)):
            env[x] = math.exp(-MERGE_DECAY * (x - xa) / float(span))
    curve = [None if e is None else round(base + 1 - (base + 1 - top) * e) for e in env]
    inside = [[False] * W for _ in range(64)]
    for i, ch in enumerate(WORD):
        xl = i * CELL + x_first
        sink = round(DS_SINK * (env[xl + lw // 2] or 0.0))
        y = top + sink
        for r, row in enumerate(Canvas._FONT[ch]):
            xx = xl
            for kk, bit in enumerate(row):
                for dy in range(rows[r]):
                    for dx in range(COLS[kk]):
                        X, Y = xx + dx, y + dy
                        inside[Y][X] = True
                        if bit == "1":
                            under = curve[X] is not None and Y >= curve[X]
                            if not under or ink(0, X, Y):
                                c.px(X, Y)
                xx += COLS[kk]
            y += rows[r]
    # the curve over the title: inverted inside the letter boxes, dotted outside
    prev = None
    for x in range(W):
        if curve[x] is None:
            prev = None
            continue
        ys = [curve[x]] if prev is None else range(min(prev, curve[x]), max(prev, curve[x]) + 1)
        if prev is not None and curve[x] < prev - 1:      # the attack: vertical edge
            ys = range(curve[x], prev + 1)
        for y in ys:
            if y > base:
                continue
            if inside[y][x]:
                if DS_INVERT and c.pixels[y][x] and y < curve[x] + 1:
                    c.px(x, y, 0)
            elif (x + y) % 2 == 0:
                c.px(x, y)
        prev = curve[x]
    # under the title: the gain (1 - envelope) as a dotted line, arrows on the kicks
    g_top, g_bot = DS_GAIN
    for x in range(W):
        if env[x] is None:
            continue
        y = round(g_top + (g_bot - g_top) * env[x])
        if x % 2 == 0:
            c.px(x, y)
    for k in KICKS["two"]:
        ax = x_first + k * CELL + lw // 2
        for d in range(4):                         # down arrow just above the line
            c.hline(ax - 3 + d, ax + 3 - d, g_top - 1 + d)
        c.vline(ax, g_top - 5, g_top - 1)
    labels(c)
    return c

def labels(c):
    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        c.draw_text(label, max(2, cx - w // 2), 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)

def build(variant="two"):
    c = Canvas()
    lw = sum(COLS)
    for i, ch in enumerate(WORD):
        x0 = i * CELL
        letter(c, ch, x0 + (CELL - lw) // 2, TOP, LEVELS[variant][i])

    # the kicks' amp envelopes: jump up on the kick step, decay over the beat, so the
    # letters fill in as the kick dies away. Solid outline, area in the sparse dot screen.
    x_first = (CELL - lw) // 2
    x_last = W - 1 - x_first
    span = 4 * CELL
    for k in KICKS[variant]:
        xa = x_first + k * CELL
        xb = min(x_last, xa + span - 1 - (0 if k * CELL + span < W else 0))
        if k * CELL + span < 8 * CELL:
            xb = xa + span - 1 - (CELL - lw)          # stop where the next kick's letter starts
        prev = None
        for x in range(xa, xb + 1):
            t = (x - xa) / float(span)
            y = round(ENV_BASE - (ENV_BASE - ENV_TOP) * math.exp(-ENV_DECAY * t))
            lvl = 3 - min(3, int((x - xa) * 4 // span)) if ENV_FADE else 0   # solid -> sparse
            for yy in range(y + 1, ENV_BASE + 1):
                if ink(lvl, x, yy):
                    c.px(x, yy)
            if prev is None:
                c.vline(x, y, ENV_BASE)                # the attack
            else:
                c.vline(x, min(prev, y), max(prev, y))
            prev = y
        c.hline(xa, xb, ENV_BASE)                      # floor

    for (kid, kx, ky), label in zip(cc.knob_layout(3), LABELS):
        cx = kx + 10
        w = len(label) * 4 - 1
        c.draw_text(label, max(2, cx - w // 2), 37, scale=1, spacing=1)
        cyd = ky + 7
        sq = _VSquash(c, cyd)
        sq.circle(cx, cyd, 6)
        sq.vline(cx, cyd - 5, cyd)
    return c

def preview(c, path, scale=5):
    from PIL import Image
    im = Image.new("RGB", (W, 64), (246, 248, 251))
    for y in range(64):
        for x in range(W):
            if c.pixels[y][x]:
                im.putpixel((x, y), (28, 52, 120))
    im.resize((W * scale, round(64 * 1.4 * scale)), Image.NEAREST).save(str(path))

if __name__ == "__main__":
    # the chosen cover is build_ds() (Luca, 2026-10-06); build() / build_merged() are the
    # earlier rounds, kept for reference
    c = build_ds()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / (NAME + ".json")
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    preview(c, sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent / "cover_preview.png")
    print("wrote", out)
