"""Generate src/airwindows/common/covers/LOUDER.json: a plain PLACEHOLDER cover (title and
knob labels only, the same look as the generic cover of custom_covers.make_cover). Real cover
art is designed later with Luca (docs/NEW-EFFECTS.md, "Covers").

Run from anywhere:  py src/custom/louder/make_cover.py
Also writes cover_preview.png next to this script.
"""
import json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/louder/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash, DEVICE_ASPECT

NAME = "LOUDER"
LABELS = ["Gain", "Ceil", "Rels"]


def build():
    return cc.meter_cover(NAME, LABELS)      # placeholder + the live meter band


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
