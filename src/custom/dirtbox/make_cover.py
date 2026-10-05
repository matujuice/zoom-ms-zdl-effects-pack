"""Generate src/airwindows/common/covers/DirtBox.json (128x64 cover override).

Concept (Luca, 2026-10-05): DubSiren's metal case, but a beaten-up one.
  * the case: DubSiren's outer wall, inner lip and slotted screws, knocked about: the top
    wall dented in, one corner folded, the lip sprung loose at one spot, one screw turned
    crooked and two gone (bare holes with rust round them),
  * the classic acid smiley where DubSiren has its woofer,
  * DIRTBOX in the pack's chunky title letters (2 px stems, like WaveFold),
  * smiley and title centred together as one group, a short scratch, DubSiren's brushed-metal grain.

Run from anywhere:  py src\\custom\\dirtbox\\make_cover.py
Also writes cover_preview.png next to this script (black on white).
"""
import json, math, random, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]        # src/custom/dirtbox/make_cover.py -> repo root
sys.path.insert(0, str(ROOT / "build"))
sys.path.insert(0, str(ROOT / "src" / "airwindows" / "common"))
from screen_image import Canvas
import custom_covers as cc
from custom_covers import _VSquash

NAME = "DirtBox"
A=1.4
def line(c,x0,y0,x1,y1,v=1,skip=None):
    n=max(abs(x1-x0),abs(y1-y0),1)
    for i in range(n+1):
        x,y=round(x0+(x1-x0)*i/n),round(y0+(y1-y0)*i/n)
        if skip and skip(x,y): continue
        c.px(x,y,v)
def bez(p0,p1,p2,p3,n=40):
    return [(round((1-t)**3*p0[0]+3*(1-t)**2*t*p1[0]+3*(1-t)*t*t*p2[0]+t**3*p3[0]),
             round((1-t)**3*p0[1]+3*(1-t)**2*t*p1[1]+3*(1-t)*t*t*p2[1]+t**3*p3[1])) for t in [i/n for i in range(n+1)]]
def build():
    rnd=random.Random(5)
    c=Canvas()
    TITLE,TX,TY="DIRTBOX",43,12        # smiley (21) + 5 + title (68): x 17..110, centred
    # title: the pack's 3x5 font at scale 3 with DubSiren's stencil slit; worn, not wrecked:
    # a few chipped pixels on the edges and one letter (T) sitting a pixel low, as if knocked
    # the pack's chunky title letters (WaveFold's font scaling: 2 px stems)
    COLS,ROWS,LG=(2,4,2),(3,2,3,2,3),2
    main=set(); x=TX
    for ch in TITLE:
        yy=TY
        for r,row in enumerate(Canvas._FONT[ch]):
            xx=x
            for k,bit in enumerate(row):
                if bit=='1':
                    for dy in range(ROWS[r]):
                        for dx in range(COLS[k]): main.add((xx+dx,yy+dy))
                xx+=COLS[k]
            yy+=ROWS[r]
        x+=sum(COLS)+LG
    edge=[p for p in main if any((p[0]+dx,p[1]+dy) not in main for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))]
    for p in rnd.sample(sorted(edge),3): main.discard(p)
    # the classic acid smiley where DubSiren has its woofer, drawn by hand (rows are 1.4x
    # taller than columns on the screen, so 21 x 16 pixels reads as round): a 2 px rim,
    # upright oval eyes, a wide grin with the little creases at its ends
    SMILEY = (".......#######.......",
              "....####.....####....",
              "...##...........##...",
              "..##..##.....##..##..",
              ".##...##.....##...##.",
              ".##...##.....##...##.",
              "##....##.....##....##",
              "##.................##",
              "##.#.............#.##",
              "##..##.........##..##",
              ".##...##.....##...##.",
              ".##.....#####.....##.",
              "..##.............##..",
              "...##...........##...",
              "....####.....####....",
              ".......#######.......")
    face = set((17 + x, 10 + y) for y, row in enumerate(SMILEY) for x, ch in enumerate(row) if ch == "#")
    for p in face: c.px(*p)
    # gap round the title so it stays readable, then the title
    for (x,y) in main:
        for dy in (-1,0,1):
            for dx in (-1,0,1): c.px(x+dx,y+dy,0)
    for p in main: c.px(*p)
    # the metal case, DubSiren's construction, knocked about: the outer wall dented in
    # along the top (a soft dip, not a gap), one corner folded in, the inner lip sprung loose
    for x in range(2,126):
        y=0+(1 if 72<=x<=82 else 0)+(1 if 75<=x<=79 else 0)
        c.px(x,y)
        if x>=7: c.px(x,63)
    for y in range(2,62):
        c.px(127,y)
        if y<58: c.px(0,y)
    line(c,0,58,5,63)                                                    # folded corner
    c.px(1,1); c.px(126,1); c.px(126,62)
    for x in range(2,126):
        y=2+(1 if 74<=x<=80 else 0)
        if not (100<=x<=104): c.px(x,y)                                  # lip sprung at one spot
        if x>=8: c.px(x,61)
    for y in range(2,62):
        c.px(125,y)
        if y<57: c.px(2,y)
    line(c,2,57,6,61)
    line(c,100,2,104,4)                                                  # the loose lip bends up
    # screws: one left, one turned crooked, two gone (bare holes with rust round them)
    def head(sx,sy,slot):
        for (dx,dy) in ((-2,-1),(-2,0),(-2,1),(2,-1),(2,0),(2,1),(-1,-2),(0,-2),(1,-2),(-1,2),(0,2),(1,2)): c.px(sx+dx,sy+dy)
        if slot=='-': c.hline(sx-1,sx+1,sy)
        else: c.px(sx-1,sy+1); c.px(sx,sy); c.px(sx+1,sy-1)
    def hole(sx,sy):
        for (dx,dy) in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)): c.px(sx+dx,sy+dy)
    head(6,6,'-'); hole(121,6); head(121,57,'/'); hole(8,53)
    rust=[(121,6,6),(8,53,7),(4,59,5)]
    # labels and dials, as on every cover
    for (kid,kx,ky),label in zip(cc.knob_layout(3),("MODEL","DRIVE","TONE")):
        cx=kx+10; w=len(label)*4-1
        c.draw_text(label,max(2,cx-w//2),37,scale=1,spacing=1)
        sq=_VSquash(c,ky+7); sq.circle(cx,ky+7,6); sq.vline(cx,ky+2,ky+7)
    def free(x,y,h):
        return all(not c.pixels[yy][xx] for yy in range(max(0,y-h),min(64,y+h+1)) for xx in range(max(0,x-h),min(128,x+h+1)))
    boxes=[(kx-5,41,kx+27,63) for (_k,kx,_y) in cc.knob_layout(3)]
    inbox=lambda x,y: any(a<=x<=b and d<=y<=e for (a,d,b,e) in boxes)
    # long scratches: thin, single-pixel, broken where they cross anything
    scr=[(4,44,13,40)]
    marks=[]
    for (a,b,d,e) in scr:
        n=max(abs(d-a),abs(e-b))
        for i in range(n+1):
            x,y=round(a+(d-a)*i/n),round(b+(e-b)*i/n)
            if free(x,y,1) and not inbox(x,y): marks.append((x,y))
    # brushed-metal grain (DubSiren's staggered lattice)
    for y0 in range(4,60,3):
        off=((y0//3)*2)%5
        for x0 in range(4+off,123,5):
            if inbox(x0,y0): continue
            if free(x0,y0,3): marks.append((x0,y0))
    # rust: clusters round the bare holes and the folded corner
    for (rx,ry,r) in rust:
        for _ in range(26):
            a=rnd.random()*6.283; d=rnd.random()**0.6*r
            x,y=round(rx+d*math.cos(a)),round(ry+d*math.sin(a)/A)
            if 3<=x<=124 and 3<=y<=60 and free(x,y,0) and not inbox(x,y) and rnd.random()<0.6: marks.append((x,y))
    for p in marks: c.px(*p)
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
