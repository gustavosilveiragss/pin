"""Reconstroi o pin-case em B-rep (CadQuery) e exporta STEP editavel pro Onshape.
Sem gravacoes/lead-ins (cosmetico) pra malha limpa. Geometria = pin-case.scad (layout invertido)."""
import cadquery as cq
from cadquery import Solid, Vector

# --- params (de pin-case.scad) ---
wall=2.0; floor_th=2.0; lid_th=2.4
inner_w=45; inner_l=62; inner_h=28   # bateria 103450 (34x50x10) no mezanino
rout=6; rin=4; cham_e=0.8
outer_w=inner_w+2*wall; outer_l=inner_l+2*wall
P=2.54; pb_z=3.0
def bx(c): return (c-5.5)*P
def by(r): return (6.5-r)*P
mount=[(1,8),(10,8),(1,12),(10,12)]
tp_z0=11.0; tp_boss_x=[-3.2,11.2]; tp_boss_y=-28; tp_boss_d=5
usb_w=10.0; usb_h=4.0; usb_cx=4; usb_cz=tp_z0+2.75  # centro real 2.75 acima do fundo do PCB (antes +1.6 = baixo)
sw_cx=-11; sw_cz=6; sw_w=5.0; sw_h=3.0              # chave na parede +Y (oposta), slot knob 5.0x3.0
btn_d=4.3; btn_y=0; btn_z=8; btn_si=6.4; btn_so=8.4; btn_sd=4.5
post_d=5; post_cx=inner_w/2-1.7; post_cy=inner_l/2-1.7
notch_w=12; notch_h=3.2
bpost_d=4; ledge_z=17
scr_self=1.7; scr_free=2.2; scr_head=3.8

def cylZ(x,y,z0,z1,d): return Solid.makeCylinder(d/2, z1-z0, Vector(x,y,z0), Vector(0,0,1))
def boxC(cx,cy,cz,dx,dy,dz): return Solid.makeBox(dx,dy,dz, Vector(cx-dx/2, cy-dy/2, cz-dz/2))
def cylX(y,z,d,L,x0): return Solid.makeCylinder(d/2, L, Vector(x0,y,z), Vector(1,0,0))
def stadiumY(cx,cz,w,h,sy=-1):
    r=h/2; dxx=(w-h)/2; L=wall+2
    y0=sy*(outer_l/2+1); d=Vector(0,-sy,0)         # comeca fora, aponta p/ dentro
    a=Solid.makeCylinder(r,L,Vector(cx-dxx,y0,cz),d)
    b=Solid.makeCylinder(r,L,Vector(cx+dxx,y0,cz),d)
    s=a.fuse(b)
    if dxx>0.01:
        y_lo=y0-(L if sy>0 else 0)
        s=s.fuse(Solid.makeBox(2*dxx,L,2*r,Vector(cx-dxx,y_lo,cz-r)))
    return s

# --- BASE ---
h_out=floor_th+inner_h
outer=(cq.Workplane("XY").box(outer_w,outer_l,h_out).edges("|Z").fillet(rout)
       .translate((0,0, inner_h-h_out/2)))
outer=outer.edges("<Z").chamfer(cham_e)
inner=(cq.Workplane("XY").box(inner_w,inner_l,inner_h+4).edges("|Z").fillet(rin)
       .translate((0,0,(inner_h+4)/2)))
base=outer.cut(inner)

adds=[]
for sx in(-1,1):
  for sy in(-1,1):
    adds.append(cylZ(sx*post_cx, sy*post_cy, 0, inner_h-0.4, post_d))  # 0.4 abaixo do rim -> tampa assenta no rim
    adds.append(boxC(sx*(inner_w/2-3), sy*(inner_l/2-3), ledge_z+0.75, 6,6,1.5))
for (c,r) in mount: adds.append(cylZ(bx(c),by(r),0,pb_z,bpost_d))
for x in tp_boss_x: adds.append(cylZ(x,tp_boss_y,0,tp_z0,tp_boss_d))
adds.append(boxC(inner_w/2-btn_sd/2, btn_y, btn_z, btn_sd, btn_so, btn_so))
for sx in(-1,1): adds.append(boxC(sw_cx+sx*(8.5/2+0.5), inner_l/2-2, sw_cz, 1.5,4,5))  # cradle chave na parede +Y
for a in adds: base=base.union(a)

cuts=[stadiumY(usb_cx,usb_cz,usb_w,usb_h), stadiumY(sw_cx,sw_cz,sw_w,sw_h,1)]  # USB -Y, chave +Y
cuts.append(cylX(btn_y,btn_z,btn_d,wall+2, inner_w/2-0.5))
cuts.append(boxC(inner_w/2-btn_sd/2-0.5, btn_y, btn_z, btn_sd+1, btn_si, btn_si))
cuts.append(boxC(0, inner_l/2+wall/2, inner_h-notch_h/2+2, notch_w, wall+3, notch_h+4))
for sx in(-1,1):
  for sy in(-1,1): cuts.append(cylZ(sx*post_cx, sy*post_cy, inner_h-5.5, inner_h+0.5, scr_self))
for (c,r) in mount: cuts.append(cylZ(bx(c),by(r),-1,pb_z+0.5,scr_self))
for x in tp_boss_x: cuts.append(cylZ(x,tp_boss_y,tp_z0-4.5,tp_z0+0.5,scr_self))
for c in cuts: base=base.cut(c)

# --- GRAVACOES (mesmo do .scad, se a fonte falhar, gera sem) ---
# CadQuery.text() nao tem spacing -> monto letra por letra c/ tracking (track=1.25).
try:
    def glyph(ch,s,d): return cq.Workplane("XY").text(ch,s,d,combine=False,halign="center",valign="center")
    def spaced(t,s,d,track=1.25):
        items=[]
        for ch in t:
            if ch==' ': items.append((None,s*0.4)); continue
            w=glyph(ch,s,d); items.append((w, w.val().BoundingBox().xlen))
        gap=s*0.16*track
        total=sum(x for _,x in items)+gap*(len(items)-1)
        x=-total/2; shapes=[]
        for w,wd in items:
            if w is not None: shapes.append(w.translate((x+wd/2,0,0)).val())
            x+=wd+gap
        return cq.Compound.makeCompound(shapes)
    # logo: texto INTEIRO num text() so (baseline alinhada, bloco centrado -> sem wobble). size 3.0.
    lgw = cq.Workplane("XY").text("marmota.dev.br",3.0,0.8,halign="center",valign="center")
    lg = cq.Compound.makeCompound(lgw.vals()).mirror("YZ").translate((0,-18,-floor_th))
    base = base.cut(lg)
    def wl(t,cx,cz,sy=-1):
        g=spaced(t,2.4,1.0).rotate((0,0,0),(1,0,0),90)
        if sy>0: g=g.rotate((0,0,0),(0,0,1),180)   # +Y: le certo do lado +Y
        return g.translate((cx, sy*(outer_l/2-1.0), cz))
    base = base.cut(wl("USB-C",usb_cx,usb_cz-4.2))       # parede -Y
    base = base.cut(wl("ON/OFF",sw_cx,sw_cz-4.2,1))      # parede +Y (oposta)
    print("gravacoes OK")
except Exception as e:
    print("gravacoes falharam (segue sem):", repr(e))

# --- LID ---
lid=(cq.Workplane("XY").box(outer_w,outer_l,lid_th).edges("|Z").fillet(rout)
     .translate((0,0,lid_th/2)))
lid=lid.edges("<Z").chamfer(cham_e).edges(">Z").chamfer(0.8)
for sx in(-1,1): lid=lid.union(boxC(sx*(inner_w/2-1.3), 0, -0.7, 1.0,15,1.6))  # rib de registro folgada (~0.8mm), cruza z=0 (cola na tampa)
lc=[]
for sx in(-1,1):
  for sy in(-1,1):
    lc.append(cylZ(sx*post_cx,sy*post_cy,-1,lid_th+1,scr_free))
    lc.append(cylZ(sx*post_cx,sy*post_cy,lid_th-1.4,lid_th+1,scr_head+0.4))
for c in lc: lid=lid.cut(c)

# --- MOCKS ---
mocks={
 "perfboard": boxC(0,0,3.8, 25.4,30.5,1.6),
 "esp":       boxC(-2.5,7.6,7.1, 18,15,5),
 "tp4056":    boxC(4,-16,11.8, 18,26,1.6),
 "bateria":   boxC(0,0,22, 34,50,10),
 "chave":     boxC(-11,29.3,6, 8.5,3.5,3.5),   # parede +Y
 "botao":     boxC(22.5,0,8, 4,6,6),
}

def solidOf(x): return x.val() if hasattr(x,"val") else x

# STEP 1: so a caixa (base + tampa lado a lado)
a1=cq.Assembly(name="pin-case")
a1.add(solidOf(base), name="base")
a1.add(solidOf(lid).moved(cq.Location(Vector(outer_w+12,0,0))), name="tampa")
a1.save("case/pin-case.step")

# STEP 2: montagem completa (caixa fechada + componentes)
a2=cq.Assembly(name="pin-montagem")
a2.add(solidOf(base), name="base")
a2.add(solidOf(lid).moved(cq.Location(Vector(0,0,26))), name="tampa")
for n,s in mocks.items(): a2.add(s, name=n)
a2.save("case/pin-montagem.step")

# (guards de fuzzy skin removidos: nao precisa mais da separacao no Bambu)

# STL da base pra validar (renderizo no OpenSCAD)
cq.exporters.export(base, "/tmp/cq_base.stl")
cq.exporters.export(lid, "/tmp/cq_lid.stl")
print("OK: pin-case.step + pin-montagem.step gerados")
