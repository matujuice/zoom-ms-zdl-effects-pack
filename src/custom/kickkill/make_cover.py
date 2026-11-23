"""Generate src/airwindows/common/covers/KickKill.json (128x64 cover override).

KICK KILL in Washout's style (the pack's square block font, edges lightly worn): KICK is cut
out (negative) of several overlapping splats of blood with spikes, drips and droplets; KILL solid with worn
edges. Seeded, so every run draws the same picture. Knob row: custom_covers._draw_knob_row.

Run from anywhere:  py src/custom/kickkill/make_cover.py
The build (custom_covers.make_cover) picks the JSON up automatically.
Also writes cover_preview.png next to this script (LCD look, build/lcd_render.py).
"""
import json, math, random, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/kickkill/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc

NAME = "KickKill"
LABELS = ['Kill', 'Mix']
ART_H = 36                                        # rows 0..35; the knob labels start at 37
TOP, H, ST = 8, 20, 4                             # letter top row, height, stroke


def glyph(ch):
    """Pixels (x, y) of one block letter at the origin; returns (pixels, width)."""
    p = set()
    def box(x0, y0, x1, y1):
        p.update((x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1))
    if ch == 'I':
        box(0, 0, ST, H - 1); return p, ST + 1
    if ch == 'L':
        box(0, 0, ST - 1, H - 1); box(0, H - ST, 11, H - 1); return p, 12
    if ch == 'C':
        box(0, 1, ST - 1, H - 2); box(1, 0, 12, ST - 1); box(1, H - ST, 12, H - 1); return p, 13
    if ch == 'K':
        box(0, 0, ST - 1, H - 1)
        for y in range(H):                        # arm up-right, leg down-right, meeting at the middle
            x = 4 + round(abs(y - (H - 1) / 2) * 9 / ((H - 1) / 2))
            box(x, y, min(x + ST, 13), y)
        return p, 14
    raise ValueError(ch)


def word(text, x0):
    out = set()
    for ch in text:
        p, w = glyph(ch)
        out.update((x0 + x, TOP + y) for x, y in p)
        x0 += w + 2
    return out, x0 - 2


def build():
    rnd = random.Random(503)
    c = Canvas()
    ink = set()
    def splat(cx, cy, r, sx=1.0, spikes=0):
        """One blob: wobbly rim, a few spikes thrown out with a droplet at each end."""
        ph = [rnd.uniform(0, 6.3) for _ in range(3)]
        sp = [(rnd.uniform(-math.pi, math.pi), rnd.uniform(0.35, 0.8)) for _ in range(spikes)]
        for y in range(ART_H):
            for x in range(0, 71):
                dx, dy = (x - cx) / sx, y - cy
                a, d = math.atan2(dy, dx), math.hypot(dx, dy)
                rim = r * (1 + 0.12 * math.sin(3 * a + ph[0]) + 0.07 * math.sin(7 * a + ph[1]) + 0.04 * math.sin(13 * a + ph[2]))
                for sa, sl in sp:                 # a spike: a narrow lobe 35..80 % past the rim
                    da = abs((a - sa + math.pi) % (2 * math.pi) - math.pi)
                    rim += r * sl * max(0.0, 1 - da / 0.10)
                if d <= rim:
                    ink.add((x, y))
        for sa, sl in sp:
            d = r * (1 + sl + 0.25)
            ex, ey = round(cx + math.cos(sa) * d * sx), round(cy + math.sin(sa) * d)
            ink.update((ex + i, ey + j) for i in (0, 1) for j in (0, 1))
    # several splats of blood thrown on top of each other behind KICK, small ones flung around
    splat(14, 18, 14.5, 0.85, 3)                  # one splat behind each letter
    splat(25, 16, 13.5, 0.55, 2)
    splat(36, 19, 14.5, 0.75, 3)
    splat(53, 17, 14.5, 0.80, 3)
    splat(66, 5, 3.0, 1.0, 1)                     # small ones flung around
    splat(3, 31, 2.8, 1.0, 1)
    splat(44, 33, 2.2, 1.0, 0)
    splat(2, 3, 1.8, 1.0, 0)
    splat(29, 2, 1.6, 1.0, 0)
    # drips running down from the lowest rim
    for x in (10, 26, 37, 55):
        y = max(yy for (xx, yy) in ink if xx == x and yy < 33)
        end = min(ART_H - 2, y + rnd.randint(2, 4))
        ink.update((x, yy) for yy in range(y, end + 1))
        ink.update(((x - 1, end), (x + 1, end), (x, end + 1)))
    # loose droplets
    for _ in range(16):
        ink.add((rnd.randint(0, 70), rnd.randint(0, ART_H - 1)))
    ink = {(x, y) for (x, y) in ink if x <= 70}  # keep the blood clear of KILL
    # KICK cut out of the blood (negative), KILL solid with worn edges
    kick, _ = word("KICK", 7)
    ink -= kick
    kill, end = word("KILL", 76)
    for (x, y) in kill:
        edge = any((x + i, y + j) not in kill for i, j in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if not (edge and rnd.random() < 0.12):
            ink.add((x, y))
    for (x, y) in ink:
        if 0 <= x < 128 and 0 <= y < ART_H:
            c.px(x, y)
    cc._draw_knob_row(c, LABELS)
    return c


if __name__ == "__main__":
    c = build()
    out = ROOT / "src" / "airwindows" / "common" / "covers" / (NAME + ".json")
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    from lcd_render import save_png
    save_png(c.pixels, Path(__file__).resolve().parent / "cover_preview.png")
    print("wrote", out)
