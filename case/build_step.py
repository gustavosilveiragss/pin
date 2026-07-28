"""Reconstroi o pin-case em B-rep (CadQuery) e exporta STEP editavel pro Onshape.
Sem gravacoes/lead-ins (cosmetico) pra malha limpa. Geometria = pin-case.scad (layout invertido)."""
import cadquery as cq
from cadquery import Solid, Vector

# --- params (de pin-case.scad) ---
wall=2.0; floor_th=2.0; lid_th=2.4
inner_w=45; inner_l=62; inner_h=28   # bateria 103450 (34x50x10) no mezanino
rout=6; rin=rout-wall; cham_e=0.8
outer_w=inner_w+2*wall; outer_l=inner_l+2*wall
P=2.54; pb_z=3.0; fit=0.4
def bx(c): return (c-6)*P                            # 11 col centradas (placa 30mm real)
def by(r): return (6-r)*P
mount=[(2,3),(10,3),(2,9),(10,9)]                    # 4 furos p/ cantos: X=+-10.16, Y=+-7.62
tp_z0=11.0; tp_boss_x=[-6,6]; tp_boss_y=-(inner_l/2-2.5); tp_boss_d=6   # TP4056 17mm; d6 FUNDE na parede -Y (nao tangencia)
usb_w=10.0; usb_h=5.0; usb_cx=0; usb_cz=tp_z0+3.0     # USB-C CENTRADO; centro p/ PCB 1.6mm; slot alto + counterbore
usb_cb_w=12; usb_cb_h=5.0; usb_cb_deep=1.2            # rebaixo externo alinhado ao slot (sem web 0.25mm), afina p/ 0.8mm
# chave 12(X larg)x5(Z alt)x5(Y prof): bolso FECHADO + teto 2 aguas (gable) + fenda + counterbore
sw_cx=-11; sw_cz=6; sw_bw=12; sw_bh=5; sw_bt=5; sw_slot_w=7.0; sw_slot_h=3.6; sw_sock_wall=1.6
sw_ridge_hw=2.5; sw_roof=1.2; sw_cb_w=11; sw_cb_h=5; sw_cb_deep=1.0
sw_pk_top=sw_cz+(sw_bh+fit)/2; sw_ridge_z=sw_pk_top+((sw_bw+fit)/2-sw_ridge_hw)   # gable
# botao 6x6: bolso-tubo + lip fino de 0.8mm -> plunger PROUD ~1.2mm
btn_y=0; btn_z=8; btn_body=6.0; btn_plunger_d=4.6; btn_lip=0.8; btn_sock_wall=1.6; btn_sock_d=4.5
post_d=5; post_cx=inner_w/2-1.7; post_cy=inner_l/2-1.7
notch_w=12; notch_h=3.2
bpost_d=4; ledge_z=16     # topo da aba z16 -> 12mm ate a tampa (folga p/ celula ate 11mm). Cunha de suporte so no .scad (cosmetico)
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
    adds.append(boxC(sx*(inner_w/2-4.5), sy*(inner_l/2-4.5), ledge_z-0.75, 9,9,1.5))  # aba 9x9, apoio ~3.5x3mm/canto, topo z16
for (c,r) in mount: adds.append(cylZ(bx(c),by(r),0,pb_z,bpost_d))
for x in tp_boss_x: adds.append(cylZ(x,tp_boss_y,0,tp_z0,tp_boss_d))
btn_xout=outer_w/2-btn_lip; btn_wext=btn_body+2*btn_sock_wall+fit; btn_top=btn_z+btn_wext/2
adds.append(boxC(btn_xout-btn_sock_d/2, btn_y, btn_top/2, btn_sock_d, btn_wext, btn_top))   # bolso-tubo do botao ao CHAO (+X)
sw_wext=sw_bw+2*sw_sock_wall+fit; sw_d=sw_bt+sw_sock_wall+fit; sw_top=sw_ridge_z+sw_roof   # z0 ate acima da cumeeira
adds.append(boxC(sw_cx, inner_l/2-sw_d/2+0.01, sw_top/2, sw_wext, sw_d, sw_top))            # bolso da chave ao CHAO, teto 2 aguas (+Y)
for a in adds: base=base.union(a)

cuts=[stadiumY(usb_cx,usb_cz,usb_w,usb_h)]                                   # USB-C (-Y)
cuts.append(boxC(usb_cx, -(outer_l/2)+usb_cb_deep-(usb_cb_deep+0.5)/2, usb_cz, usb_cb_w, usb_cb_deep+0.5, usb_cb_h))  # counterbore externo
# chave: bolso (abre atras -Y) + teto 2 aguas + fenda do atuador + counterbore externo
sw_fy=inner_l/2; sw_by=inner_l/2-(sw_bt+sw_sock_wall+fit)-1
cuts.append(boxC(sw_cx,(sw_fy+sw_by)/2,sw_cz, sw_bw+fit, sw_fy-sw_by, sw_bh+fit))
_half=(sw_bw+fit)/2
_gable=(cq.Workplane("XZ").polyline([(-_half,sw_pk_top),(_half,sw_pk_top),(sw_ridge_hw,sw_ridge_z),(-sw_ridge_hw,sw_ridge_z)])
        .close().extrude(sw_fy-sw_by+0.1)).translate((sw_cx, sw_fy+0.05, 0)).val()   # remove o pico -> encostas 45
cuts.append(_gable)
cuts.append(stadiumY(sw_cx,sw_cz,sw_slot_w,sw_slot_h,1))
cuts.append(boxC(sw_cx, (outer_l/2)-sw_cb_deep+(sw_cb_deep+0.5)/2, sw_cz, sw_cb_w, sw_cb_deep+0.5, sw_cb_h))  # counterbore +Y
# botao: bolso-tubo (abre pro interior -X) + furo do plunger no lip
cuts.append(boxC(btn_xout-(btn_sock_d+1)/2+0.01, btn_y, btn_z, btn_sock_d+1, btn_body+fit, btn_body+fit))
cuts.append(cylX(btn_y,btn_z,btn_plunger_d, btn_lip+0.3, btn_xout-0.1))
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
    lg = cq.Compound.makeCompound(lgw.vals()).mirror("YZ").translate((0,-25,-floor_th))  # = logo_y do .scad
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

def solidOf(x): return x.val() if hasattr(x,"val") else x

# STEP editavel pro Onshape: base + tampa lado a lado (uma peca cada, mesmo arquivo)
a1=cq.Assembly(name="pin-case")
a1.add(solidOf(base), name="base")
a1.add(solidOf(lid).moved(cq.Location(Vector(outer_w+12,0,0))), name="tampa")
a1.save("case/pin-case.step")
print("OK: case/pin-case.step gerado")
