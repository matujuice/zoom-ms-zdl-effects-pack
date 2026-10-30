#!/usr/bin/env python3
"""Render dist/*.ZDL on-device covers to the graphics/ gallery PNGs.

The gallery images had no generator -- they were made by hand in June 2026 and
then went stale the moment any effect's knobs were renamed. Hydra's still read
WINDOW / FAST / SLOW long after those knobs became Div / Tempo / Fast.

Geometry and palette are matched to the existing files rather than invented.
Output is not byte-identical to the June set: those were rendered from an older
cover whose border bled to the frame edge, while make_cover now draws it inset
by a pixel. The framing is the same; the art is simply current.

The cover is decoded from the ZDL and drawn by build/lcd_render.py (the pedal LCD look).
With no arguments it also writes release/covers.png, the README gallery.

Usage:
    python3 build/make_gallery_png.py            # every effect in dist/
    python3 build/make_gallery_png.py Stasis     # just one
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "build"))

from decode_picture import decode_picture  # noqa: E402
from lcd_render import save_png, sheet  # noqa: E402

# the README gallery (release/covers.png), in its order, two per row
README_COVERS = ["WaveFold", "DualShft", "Choral", "EuGate", "DubSiren", "SGNL", "Scrub", "DirtBox", "Breather",
                 "GRDELAY", "Sweep", "LOUDER", "Gooey", "DJFilter", "Washout", "Takeoff", "GRDLOOP"]
# effects whose gallery picture is a live still, not the stored cover: src/custom/<folder>/live_still.png
# (128x64, 1 bit, black = ink). LOUDER / Gooey: the meter with a drum loop going in (custom_covers.meter_frame);
# GRDLOOP: the loop bar drawn on the cover by the MIDISTOMP firmware (zoom-ms-modding asm/looper/lbox.c).
LIVE = {"LOUDER": "louder", "Gooey": "gooey", "GRDLOOP": "looper"}


def gallery_pixels(name: str):
    if name in LIVE:
        from PIL import Image
        im = Image.open(ROOT / "src" / "custom" / LIVE[name] / "live_still.png").convert("1")
        return [[int(im.getpixel((x, y)) == 0) for x in range(128)] for y in range(64)]
    return decode_picture(str(ROOT / "dist" / (name + ".ZDL")))[0]


def render(zdl: Path, out: Path) -> None:
    px, _name = decode_picture(str(zdl))
    out.parent.mkdir(parents=True, exist_ok=True)
    save_png(px, out)


def main() -> int:
    wanted = sys.argv[1:]
    dist = ROOT / "dist"
    gfx = ROOT / "graphics"
    zdls = sorted(dist.glob("*.ZDL"))
    if wanted:
        keep = {w.lower() for w in wanted}
        zdls = [z for z in zdls if z.stem.lower() in keep]
        if not zdls:
            print(f"no match in dist/ for {wanted}", file=sys.stderr)
            return 1
    for z in zdls:
        out = gfx / f"{z.stem.lower()}.png"
        render(z, out)
        print(f"  {z.stem:<10} -> {out.relative_to(ROOT)}")
    if not wanted:
        sheet([gallery_pixels(n) for n in README_COVERS],
              ROOT / "release" / "covers.png", cols=2)
        print("  release/covers.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
