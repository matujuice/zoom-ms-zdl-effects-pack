"""Shared cover preview renderer: a 128x64 1-bit bitmap in the pedal LCD look (tall 6x9 px cells, 1 px grid,
near-flat blue-violet ink on grey-white, glass margin, dark rounded bezel). Copied from the firmware repo's
scripts/screens/make_screens.py (lcd/ink), the approved look; keep the two in step.

    from lcd_render import save_png, sheet
    save_png(pixels, path)            # pixels: 64 rows of 128 truthy/falsy values
    sheet([pixels, ...], path, cols)  # gallery grid (README release/covers.png)
"""
from pathlib import Path

from PIL import Image, ImageDraw

CW, CH, M, B = 6, 9, 18, 14
BG, OFF, GAP = (220, 222, 226), (209, 212, 218), (226, 228, 231)
STOPS = [(0, (40, 48, 128)), (0.5, (44, 44, 130)), (1, (52, 40, 128))]


def ink(t):
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if t <= b:
            f = (t - a) / (b - a)
            return tuple(round(ca[i] + (cb[i] - ca[i]) * f) for i in range(3))


def lcd(p, cw=CW, ch=CH, mx=M, my=M):
    w, h = len(p[0]), len(p)
    sw, sh = w * cw + 2 * mx, h * ch + 2 * my
    im = Image.new("RGB", (sw + 2 * B, sh + 2 * B), (255, 255, 255))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, im.width - 1, im.height - 1), 16, fill=(24, 24, 27))
    d.rounded_rectangle((B, B, B + sw - 1, B + sh - 1), 4, fill=BG)
    for y in range(h):
        for x in range(w):
            X, Y = B + mx + x * cw, B + my + y * ch
            d.rectangle((X, Y, X + cw - 1, Y + ch - 1), fill=GAP)
            d.rectangle((X, Y, X + cw - 2, Y + ch - 2), fill=ink(x / (w - 1)) if p[y][x] else OFF)
    return im


def save_png(pixels, path):
    lcd(pixels).save(str(path))


def sheet(frames, path, cols=1, gap=20):
    """frames side by side / in a grid on white, as lcd() images"""
    ims = [lcd(p) for p in frames]
    w, h = ims[0].size
    rows = -(-len(ims) // cols)
    out = Image.new("RGB", (cols * w + (cols + 1) * gap, rows * h + (rows + 1) * gap), (255, 255, 255))
    for i, im in enumerate(ims):
        out.paste(im, (gap + i % cols * (w + gap), gap + i // cols * (h + gap)))
    out.save(str(path))
