"""Generate src/airwindows/common/covers/DirtBox.json (128x64 cover override).

Concept (Luca, 2026-10-05): DubSiren's metal case, but a beaten-up one.
  * the case: DubSiren's outer wall, inner lip and slotted screws, knocked about: the top
    wall dented in, one corner folded, the lip sprung loose at one spot, one screw turned
    crooked and two gone (bare holes with rust round them),
  * the classic acid smiley where DubSiren has its woofer,
  * DIRTBOX in the pack's chunky title letters (2 px stems, like WaveFold),
  * the title centred on the box, two sizes up with open counters so it reads, and dirtied;
  * the smiley sticker (turned 10 degrees, lower right edge peeling) stuck on after the
    title: its right edge laps over the D's stem, the D still reads;
  * uneven grimy grain, heavier toward the edges.

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
SCX,SCY,SR=13.5,15.5,10.0                # sticker centre and radius: its right edge just over the D
def build():
    rnd=random.Random(5)
    c=Canvas()
    TITLE,TX,TY="DIRTBOX",23,8         # title 82 x 17, centred on the box (x 23..104)
    # title: the pack's chunky letters (WaveFold's scaling), two sizes up (10 x 17 per letter, open counters),
    # then dirtied: rough chipped edges, a few pits in the paint, grime crept past the edges
    # and one fine scratch through it
    COLS,ROWS,LG=(2,6,2),(3,4,3,4,3),2
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
    edge=sorted(p for p in main if any((p[0]+dx,p[1]+dy) not in main for dx,dy in N4))
    inner=sorted(p for p in main if p not in set(edge))
    for p in rnd.sample(edge,len(edge)//24): main.discard(p)                 # chipped edges
    for p in rnd.sample(inner,len(inner)//70): main.discard(p)              # pits
    for k in range(24):                                                     # the scratch
        main.discard((round(TX+48+k*0.9),round(TY+14-k*0.35)))
    outside=sorted(set((x+dx,y+dy) for (x,y) in edge for dx,dy in N4) - main)
    grime=set(rnd.sample(outside,len(outside)//40))
    # the smiley is a sticker: the classic acid face (round rim, upright oval eyes, grin with
    # creases), stuck on a little crooked (turned 10 degrees), its lower right edge peeling:
    # that part is folded back over the face, showing its dithered backing.
    # Drawn as shapes in screen-true units (y x 1.4), sampled 8 x 8 per pixel.
    scx,scy,R,TH=SCX,SCY*A,SR,math.radians(-10)
    nx,ny,cut=0.7071,0.7071,7.4                                             # fold line (local)
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
            if in_disc(pu,pv) and pu*nx+pv*ny>cut and d>cut-(R-cut)-0.1:
                if abs(math.hypot(pu,pv)-R)<1.3 or d>cut-1.0: return 'ink'  # flap edge and fold
                return 'back'
            r=math.hypot(u,v)
            if r>=R-1.7: return 'ink'                                       # rim
            if ((abs(u)-3.4)/1.45)**2+((v+3.0)/2.7)**2<=1.0: return 'ink'   # eyes
            g=math.hypot(u,v+0.4)
            if 5.0<=g<=6.5 and v+0.4>1.6: return 'ink'                      # grin
            for sx in (-1,1):                                               # creases
                if abs(u-sx*6.9)<0.75 and 0.2<v<2.6: return 'ink'
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
    # the title goes on the box first, with a clear gap round it so it stays readable ..
    for (x,y) in main:
        for dy in (-1,0,1):
            for dx in (-1,0,1): c.px(x+dx,y+dy,0)
    for p in main|grime: c.px(*p)
    # .. then the sticker over it: its paper (and a 1 px gap round it) hides what is under
    ring=set((x+dx,y+dy) for (x,y) in sticker for dx,dy in N4)-sticker
    for p in sticker|ring: c.px(*p,0)
    for p in face|back: c.px(*p)
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
    inbox=lambda x,y: (x,y) in sticker or any(a<=x<=b and d<=y<=e for (a,d,b,e) in boxes)
    # long scratches: thin, single-pixel, broken where they cross anything
    scr=[(4,44,13,40)]
    marks=[]
    for (a,b,d,e) in scr:
        n=max(abs(d-a),abs(e-b))
        for i in range(n+1):
            x,y=round(a+(d-a)*i/n),round(b+(e-b)*i/n)
            if free(x,y,1) and not inbox(x,y): marks.append((x,y))
    # uneven, grimy metal: grain on a jittered grid whose density follows a few soft blobs
    # of dirt (heavier toward the edges and the bent corner, light in the middle), with an
    # occasional short brushed streak where it is dirtiest
    blobs=[(8,40,16,0.9),(118,22,14,0.8),(30,6,12,0.6),(64,30,18,0.35),(100,8,10,0.6),(10,12,10,0.7)]
    def dirt(x,y):
        v=0.22
        for (bx,by,br,bw) in blobs:
            d=math.hypot(x-bx,(y-by)*A)/br
            if d<1.6: v+=bw*math.exp(-d*d)
        return min(v,0.85)
    for y0 in range(4,60,2):
        for x0 in range(4,123,3):
            x,y=x0+rnd.randint(-1,1),y0
            if inbox(x,y) or not free(x,y,2): continue
            dv=dirt(x,y)
            if rnd.random()<dv*0.7:
                marks.append((x,y))
                if dv>0.55 and rnd.random()<0.3 and free(x+1,y,2): marks.append((x+1,y))
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
