"""Generate src/airwindows/common/covers/Washout.json: a plain PLACEHOLDER cover (title and
knob labels only, the same look as the generic cover of custom_covers.make_cover). Real cover
art is designed later with Luca (docs/NEW-EFFECTS.md, "Covers").


Also writes cover_preview.png next to this script.
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/washout/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash, DEVICE_ASPECT

NAME = "Washout"
LABELS = ["Time", "Wash", "Fdbk"]


def build():
    c = Canvas()
    c.rect(1, 1, 126, 62)
    for by in range(2, 16):
        c.hline(2, 125, by)
    x = max(3, (128 - (len(NAME) * 7 - 1)) // 2)
    _VSquash(c, 9, aspect=DEVICE_ASPECT).draw_text(NAME, x, 4, scale=2, spacing=1, v=0)
    cc._draw_knob_row(c, LABELS)
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
