"""Gooey cover: the live meter (src/custom/common/zdl_meter.h) under the approved v16 title.

Run from anywhere:  py src/custom/gooey/make_cover.py
Re-renders title.txt (title rows 0..16, '#' = black) from the approved mockup code in
MOCKUPS (make_v11.title_gooey_k with the v13 G and the v16 holes) when that folder and PIL are there; the build only reads
title.txt. Then writes covers/Gooey.json (frame A: default knobs, empty history) and
cover_preview.png next to this script.
"""
import json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]                            # src/custom/gooey -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
import custom_covers as cc

NAME, FOLDER, BAR = "Gooey", "gooey", 16
MOCKUPS = Path.home() / "OneDrive" / "Documenti" / "CLAUDE" / "meter-mockups"


def render_title():
    """title rows 0..BAR from the mockup code, or None when the mockups or PIL are missing"""
    try:
        sys.path.insert(0, str(MOCKUPS))
        import make_v8 as v8
        import make_v11 as v11
        import make_v13 as v13
        import make_v16 as v16
    except ImportError:
        return None
    s = v8.Screen([[0] * 128 for _ in range(16)])
    s.p = [[0] * 128 for _ in range(64)]
    s.rect(0, 0, 127, BAR)
    v11.path_pts = v13.g_v12
    v11.title_gooey_k(*v13.ARGS, holes=v16.HOLES)(s)
    return ["".join("#" if s.p[y][x] else "." for x in range(128)) for y in range(BAR + 1)]


if __name__ == "__main__":
    t = render_title()
    if t:
        (HERE / "title.txt").write_text("\n".join(t) + "\n")
        print("wrote", HERE / "title.txt")
    else:
        print("mockups or PIL missing: kept title.txt")
    m = json.loads((HERE / "manifest_pedal.json").read_text(encoding="utf-8"))
    by, tags = cc.meter_geometry(FOLDER, [p["default"] for p in m["params"]])
    c = cc.meter_frame(cc.meter_title(FOLDER), by, tags)
    out = ROOT / "src" / "airwindows" / "common" / "covers" / (NAME + ".json")
    out.write_text(json.dumps({"name": NAME, "w": 128, "h": 64, "grid": c.pixels}))
    print("wrote", out)
    from PIL import Image
    im = Image.new("RGB", (128, 64), (255, 255, 255))
    for y in range(64):
        for x in range(128):
            if c.pixels[y][x]:
                im.putpixel((x, y), (0, 0, 0))
    im.resize((128 * 6, 64 * 6), Image.NEAREST).save(str(HERE / "cover_preview.png"))
