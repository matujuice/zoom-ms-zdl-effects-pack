"""Generate src/airwindows/common/covers/DirtBox.json (128x64 cover override).

Concept (Luca, 2026-10-05): DubSiren's metal case, broken but neat and intentional.
  * the case: DubSiren's outer wall, inner lip and slotted screws, knocked about: the top
    wall dented in, one corner folded, the lip sprung loose at one spot, one screw turned
    crooked and two gone (bare holes),
  * a few straight cracks through the metal (and through the paint where they cross the
    title); no specks or grime,
  * the classic acid smiley as a big sticker stuck on later by whoever modded it: wrapped round
    the case's left edge (the wall stays visible) and the A's lower left corner (the title still reads), turned 10
    degrees, its upper right edge peeling,
  * DIRTBOX in thick, even letters: 3 px stems, 2 px bars (the same weight on screen once
    the 1.4x tall pixels are counted), centred on the box, slightly worn (a few chips).

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
SCX,SCY,SR=13.5,24.5,14.0             # sticker centre and radius (screen units)
def build():
    c=Canvas()
    rnd=random.Random(7)
    TITLE,TX,TY="DIRTBOX",23,10        # title centred on the box, x 23..104
    # title: thick, even strokes, 3 px stems and 2 px bars, 4 px counters, 10 x 14 a letter
    COLS,ROWS,LG=(3,4,3),(2,4,2,4,2),2
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
    N4=((1,0),(-1,0),(0,1),(0,-1))
    # slight wear on the paint: a few chipped edge pixels and two small pits
    edge=sorted(p for p in main if any((p[0]+dx,p[1]+dy) not in main for dx,dy in N4))
    for p in rnd.sample(edge,len(edge)//40): main.discard(p)
    for p in ((58,16),(96,13)): main.discard(p)
    for p in main: c.px(*p)
    # the smiley sticker: the classic acid face (round rim, upright oval eyes, grin with
    # creases), turned 10 degrees, its upper right edge (the part over the title) peeling:
    # that part is folded back over the face, showing its dithered backing, and the title
    # shows where it lifted. Shapes in screen-true units (y x 1.4),
    # scaled from a radius-10.5 design, sampled 8 x 8 per pixel.
    R=SR; f=R/10.5
    scx,scy,TH=SCX,SCY*A,math.radians(-10)
    nx,ny,cut=0.7071,-0.7071,9.6*f                                         # fold line (local)
    ct,st=math.cos(TH),math.sin(TH)
    def local(X,Y):
        X-=scx; Y-=scy
        return X*ct+Y*st, -X*st+Y*ct
    def in_disc(u,v): return u*u+v*v<=R*R
    def sample(u,v):
        """'ink', 'back' (folded flap), 'paper' (sticker face) or None (bare metal)"""
        d=u*nx+v*ny
        if in_disc(u,v) and d<=cut:
            pu,pv=u-2*(d-cut)*nx,v-2*(d-cut)*ny                            # mirror over the fold
            if in_disc(pu,pv) and pu*nx+pv*ny>cut:
                if abs(math.hypot(pu,pv)-R)<1.4*f or d>cut-1.1*f: return 'ink'   # flap edge and fold
                return 'back'
            r=math.hypot(u,v); U,V=u/f,v/f
            if r>=R-1.9*f: return 'ink'                                     # rim
            if ((abs(U)-3.4)/1.45)**2+((V+3.0)/2.7)**2<=1.0: return 'ink'   # eyes
            g=math.hypot(U,V+0.4)
            if 5.0<=g<=6.6 and V+0.4>1.6: return 'ink'                      # grin
            for sx in (-1,1):                                               # creases
                if abs(U-sx*6.9)<0.75 and 0.2<V<2.6: return 'ink'
            return 'paper'
        return None
    face=set(); back=set(); sticker=set()
    for y in range(int(SCY-R/A)-2,int(SCY+R/A)+3):
        for x in range(int(SCX-R)-2,int(SCX+R)+3):
            cnt={'ink':0,'back':0,'paper':0}
            for sy in range(8):
                for sx in range(8):
                    k=sample(*local(x+(sx+0.5)/8.0,(y+(sy+0.5)/8.0)*A))
                    if k: cnt[k]+=1
            tot=cnt['ink']+cnt['back']+cnt['paper']
            if tot>=20: sticker.add((x,y))
            if cnt['ink']>=26: face.add((x,y))
            elif cnt['back']>=24 and (x+y)%2==0: back.add((x,y))
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
    # screws: one left, one turned crooked, two gone (bare holes)
    def head(sx,sy,slot):
        for (dx,dy) in ((-2,-1),(-2,0),(-2,1),(2,-1),(2,0),(2,1),(-1,-2),(0,-2),(1,-2),(-1,2),(0,2),(1,2)): c.px(sx+dx,sy+dy)
        if slot=='-': c.hline(sx-1,sx+1,sy)
        else: c.px(sx-1,sy+1); c.px(sx,sy); c.px(sx+1,sy-1)
    def hole(sx,sy):
        for (dx,dy) in ((-1,-1),(0,-1),(1,-1),(-1,0),(1,0),(-1,1),(0,1),(1,1)): c.px(sx+dx,sy+dy)
    head(6,6,'-'); hole(121,6); head(121,57,'/'); hole(8,53)
    # straight cracks: ink on bare metal, a clean cut where they cross the title or a wall
    def crack(pts):
        for (x0,y0),(x1,y1) in zip(pts,pts[1:]):
            n=max(abs(x1-x0),abs(y1-y0),1)
            for i in range(n+1):
                x,y=round(x0+(x1-x0)*i/n),round(y0+(y1-y0)*i/n)
                if (x,y) in sticker: continue
                c.px(x,y,0 if c.pixels[y][x] else 1)
    crack([(58,0),(56,7)])                                               # top wall
    crack([(127,23),(103,33)])                                           # right wall, under the title
    crack([(127,42),(116,52)])                                           # right wall, by Tone
    crack([(84,63),(81,52)])                                             # bottom wall, between dials
    # the sticker was stuck on later, over the title, wrapped round the case edge: its paper (and a 1 px gap round it)
    # hides what is under; where it peels, the title shows again
    # it wraps round the case's left edge: the wall and lip (x 0..2) stay on top of it
    ring=set((x+dx,y+dy) for (x,y) in sticker for dx,dy in N4)-sticker
    for (x,y) in sticker|ring:
        if x>=3: c.px(x,y,0)
    for (x,y) in face|back:
        if x>=4: c.px(x,y)
    # labels and dials, as on every cover
    for (kid,kx,ky),label in zip(cc.knob_layout(3),("MODEL","DRIVE","TONE")):
        cx=kx+10; w=len(label)*4-1
        c.draw_text(label,max(2,cx-w//2),37,scale=1,spacing=1)
        sq=_VSquash(c,ky+7); sq.circle(cx,ky+7,6); sq.vline(cx,ky+2,ky+7)
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
