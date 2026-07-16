// ============================================================
//  CASE DO PIN - ESP32-C3 broche  (TALL / ESTILOSO / MULTICOR)
//  Pebble arredondado. Parafuso M2x5 (postes da tampa + placa nas abas).
//  Placa no CHAO (parafusa na base). Bateria mezanino no topo. TP4056 parafusa 2 pads. OLED SOLTO fora; jumpers saem
//  por um buraco entre tampa e caixa (rimo +Y). Tampa LISA.
//  Textos (logo do fundo + rotulos USB/ON-OFF/BTN) sao GRAVADOS (recesso
//  1.0mm, SEM cor). Origem no centro XY; z=0 no CHAO INTERNO.
//  // CHUTE = confirmar no scan. Multicor opcional: base e lid em cores.
//  Unidade: mm.
// ============================================================

part = "all";        // all | base | lid   (exporta base/lid; textos ja gravados)
$fn = 64;

// ---------- FOLGAS / PARAFUSO UNICO ----------
fit    = 0.4;
cham   = 0.8;        // lead-in chanfrado nos furos (guia plugue, acabamento)
screw_self = 1.7;    // M2 auto-atarraxa
screw_free = 2.2;    // passante M2
screw_head = 3.8;    // countersink
// PARAFUSO UNICO: M2x5 auto-atarraxante em TUDO (tampa 4 + placa 4 no chao + TP4056 2 = 10)

// ---------- CAIXA (pebble) ----------
wall   = 2.0;
floor_th = 2.0;
lid_th = 2.4;
inner_w = 41;                 // X (+/-20.5)
inner_l = 46;                 // Y (+/-23)
inner_h = 24;                 // z 0..24
rout   = 6;                   // raio externo (pebble); r6 contem os postes de canto
rin    = rout - wall;         // raio interno
cham_e = 0.8;                 // chanfro arestas inferiores (anti elephant-foot + costura)
outer_w = inner_w + 2*wall;   // 44
outer_l = inner_l + 2*wall;   // 50
// externo 44 x 50 x 28.4

// ---------- PERFBOARD 10x12 (ESP + R1 + R2), centrada ----------
pb_pitch = 2.54;
pb_th  = 1.6;
pb_z   = 3.0;                 // placa NO CHAO (invertido): z3..4.6, sobre 4 postes de z0
function bx(c) = (c - 5.5)*pb_pitch;    // col1=-11.43 .. col10=+11.43
function by(r) = (6.5 - r)*pb_pitch;    // lin1=+13.97 .. lin12=-13.97
mount = [[1,8],[10,8],[1,12],[10,12]];  // furos de fixacao livres

// ---------- BATERIA LiPo 503035 (chao, centrada) ----------
bat_t=5.5; bat_w=31; bat_l=36;          // CHUTE. Agora MEZANINO no TOPO (apoia nas abas z17), presa pela tampa

// ---------- POSTES da placa (do CHAO ate a placa) + prateleira da bateria ----------
bpost_d=4;                    // 4 postes da placa (nos furos mount), z0..pb_z -> suportes na BASE
bat_ledge_z=17;               // 4 abas de canto onde a bateria (mezanino) apoia

// ---------- TP4056 mezanino (parafusado na parede -Y, USB-C atravessa) ----------
tp_z0=11.0;                    // PCB mezanino (mais baixo: a placa foi pro chao)
// TP4056 real (pesquisado): PCB 26x18, 6 pads Ø1.54 (retangulo 22.9x14.3). O user
// PARAFUSA por 2 pads que NAO usa (+/- de entrada, perto do USB-C): fura esses 2
// pads pra 2.0mm -> M2 auto-atarraxa nos postes. Confirma tp_boss_x/y com a placa.
tp_boss_x=[-3.2, 11.2]; tp_boss_y=-21; tp_boss_d=5;  // 2 postes nos 2 pads +/- (espac. 14.3)
usb_w=10.0; usb_h=4.0; usb_cx=4; usb_cz=tp_z0+1.6;   // USB-C real 8.9x3.2 + folga (pesquisado)

// ---------- CHAVE (fenda na parede -Y) ----------
sw_cx=-11; sw_cz=6; sw_w=4.5; sw_h=2.4;   // SS-12D00G4: curso 2.0 + knob 1.6 (pesquisado)

// ---------- BOTAO (parede +X) ----------
btn_d=4.3; btn_y=0; btn_z=8;    // tactile 6x6 do kit: plunger Ø3.5. Painel redondo 6mm -> 6.3
btn_sock_in=6.4; btn_sock_out=8.4; btn_sock_d=4.5;  // socket 6x6 por dentro (encaixa/segura o corpo solto)

// ---------- POSTES DE CANTO (chao -> tampa, M2) ----------
post_d=5; post_cx=18.8; post_cy=21.3;   // folga bateria (+/-15.5,+/-18) = 0.8mm

// ---------- TAMPA LISA + passa-cabo no rimo ----------
cable_notch_w=12; cable_notch_h=3.2;   // buraco do cabo entre tampa e caixa (rimo +Y)

// ---------- LOGO no fundo externo ----------
logo_txt="marmota.dev.br"; logo_size=3.4; logo_deep=1.0; logo_y=-18;   // 1.0 = gravado fundo (SEM cor)

show_refs=false;

// ============================================================
//  HELPERS
// ============================================================
module rr(w,l,h,r){ linear_extrude(height=h) offset(r=r) square([w-2*r, l-2*r], center=true); }
module corners(cx,cy){ for(sx=[-1,1],sy=[-1,1]) translate([sx*cx, sy*cy, 0]) children(); }
// slot stadium na parede -Y (formato de conector) + lead-in chanfrado na face externa
module slotY(cx, cz, w, h){
    dx=(w-h)/2;
    hull() for(sx=[-1,1])   // furo passante
        translate([cx+sx*dx, -(inner_l/2+wall/2), cz]) rotate([90,0,0]) cylinder(h=wall+3, d=h, center=true, $fn=40);
    hull() for(sx=[-1,1]){  // funil de encaixe na face externa
        translate([cx+sx*dx, -outer_l/2-0.05, cz]) rotate([90,0,0]) cylinder(h=0.02, d=h+2*cham, center=true, $fn=40);
        translate([cx+sx*dx, -outer_l/2+cham,  cz]) rotate([90,0,0]) cylinder(h=0.02, d=h,        center=true, $fn=40);
    }
}

module txt(t,s){ text(t, size=s, halign="center", valign="center", font="DejaVu Sans:style=Bold"); }
// Gravacoes (2a cor): logo no fundo + rotulo de cada entrada na parede.
lbl_sz=2.4;
module engravings(){
    dw=logo_deep+0.02;    // paredes: recesso 1.0
    df=0.5+0.02;          // fundo do case: 0.5 (nao enfraquece a face de cola do alfinete)
    // fundo externo (raso)
    translate([0, logo_y, -floor_th-0.01]) mirror([1,0,0]) linear_extrude(df) txt(logo_txt, logo_size);
    // parede -Y (grava da face externa): USB-C e ON/OFF, abaixo de cada furo
    for(L=[["USB-C", usb_cx, usb_cz-4.2], ["ON/OFF", sw_cx, sw_cz-4.2]])
        translate([L[1], -outer_l/2+logo_deep, L[2]]) rotate([90,0,0]) linear_extrude(dw) txt(L[0], lbl_sz);
    // botao (+X): sem escrito
}

// ============================================================
//  BASE
// ============================================================
module base(){
    ce=cham_e;
    difference(){
        union(){
            // casca pebble com CHANFRO na aresta inferior externa (anti elephant-foot).
            // topo reto (chanfrar o topo fazia o poste de canto vazar).
            difference(){
                union(){
                    hull(){  // chanfro inferior
                        translate([0,0,-floor_th])    rr(outer_w-2*ce, outer_l-2*ce, 0.02, max(0.6,rout-ce));
                        translate([0,0,-floor_th+ce]) rr(outer_w, outer_l, 0.02, rout);
                    }
                    translate([0,0,-floor_th+ce]) rr(outer_w, outer_l, floor_th+inner_h-ce, rout);
                }
                translate([0,0,0]) rr(inner_w, inner_l, inner_h+1, rin);
            }
            // postes de canto ATE A BASE
            corners(post_cx, post_cy) cylinder(h=inner_h, d=post_d);
            // POSTES da placa: do CHAO ate a placa (invertido -> suportes na BASE)
            for(m=mount) translate([bx(m[0]), by(m[1]), 0]) cylinder(h=pb_z, d=bpost_d);
            // prateleira da bateria (mezanino): 4 abas de canto (bateria apoia, tampa prende)
            for(sx=[-1,1],sy=[-1,1]) translate([sx*(inner_w/2-3), sy*(inner_l/2-3), bat_ledge_z+0.75])
                cube([6,6,1.5], center=true);
            // bosses do TP4056 (parede -Y, mezanino)
            for(x=tp_boss_x) translate([x, tp_boss_y, 0]) cylinder(h=tp_z0, d=tp_boss_d);
            // socket 6x6 do botao tactil (por dentro da parede +X, segura o corpo solto)
            translate([inner_w/2 - btn_sock_d, btn_y - btn_sock_out/2, btn_z - btn_sock_out/2])
                cube([btn_sock_d + 0.1, btn_sock_out, btn_sock_out]);
            // cradle da chave SS-12D00 (corpo 8.5x3.5): 2 nervuras flanqueiam -> encaixa+cola facil
            for(sx=[-1,1]) translate([sw_cx + sx*(8.5/2+0.5), -inner_l/2 + 2, sw_cz])
                cube([1.5, 4, 5], center=true);
        }
        // --- furos de rosca (M2 self) ---
        corners(post_cx, post_cy) translate([0,0,inner_h-5.5]) cylinder(h=6, d=screw_self);  // tampa (sai limpo no topo)
        // placa: piloto nos 4 postes (M2x5 cego -> ponta para no chao, nao fura o fundo)
        for(m=mount) translate([bx(m[0]), by(m[1]), -1]) cylinder(h=pb_z+1.5, d=screw_self);
        // TP4056: piloto M2 nos 2 postes (parafusa pelos 2 pads +/- furados a 2.0mm)
        for(x=tp_boss_x) translate([x, tp_boss_y, tp_z0-4.5]) cylinder(h=5, d=screw_self);

        // --- CORTES nas paredes ---
        slotY(usb_cx, usb_cz, usb_w, usb_h);   // USB-C stadium + lead-in
        slotY(sw_cx, sw_cz, sw_w, sw_h);       // chave stadium + lead-in
        // botao (+X): furo Ø4.3 do plunger + lead-in chanfrado na face externa
        translate([inner_w/2-0.5, btn_y, btn_z]) rotate([0,90,0]) cylinder(h=wall+2, d=btn_d, $fn=48);
        translate([outer_w/2-0.8, btn_y, btn_z]) rotate([0,90,0]) cylinder(h=1.0, d1=btn_d, d2=btn_d+1.6, $fn=48);
        // cavidade do socket (encaixa o corpo 6x6; ele seat contra a parede, plunger sai)
        translate([inner_w/2 - btn_sock_d - 1, btn_y - btn_sock_in/2, btn_z - btn_sock_in/2])
            cube([btn_sock_d + 1, btn_sock_in, btn_sock_in]);
        // passa-cabo do OLED: notch ARREDONDADO (nao corta o cabo), aberto pro topo +Y
        hull(){
            for(sx=[-1,1]) translate([sx*(cable_notch_w/2-1), inner_l/2+wall/2, inner_h-cable_notch_h+1])
                rotate([90,0,0]) cylinder(h=wall+3, d=2, center=true, $fn=24);
            translate([0, inner_l/2+wall/2, inner_h+3]) cube([cable_notch_w, wall+3, 0.1], center=true);
        }

        // --- gravacoes ---
        engravings();
    }
}

// ============================================================
//  TAMPA LISA (chanfro topo+fundo, lips de registro)
// ============================================================
module lid(){
    ce=cham_e;
    difference(){
        union(){
            // placa com CHANFRO no topo E no fundo (costura simetrica, anti elephant-foot)
            hull(){
                translate([0,0,0])          rr(outer_w-2*ce, outer_l-2*ce, 0.02, max(0.6,rout-ce));
                translate([0,0,ce])         rr(outer_w, outer_l, 0.02, rout);
                translate([0,0,lid_th-0.8]) rr(outer_w, outer_l, 0.02, rout);
                translate([0,0,lid_th])     rr(outer_w-1.6, outer_l-1.6, 0.02, max(0.6,rout-0.8));
            }
            // lips de registro por dentro das paredes +-X (alinham a tampa, seguram o abaulamento)
            for(sx=[-1,1]) translate([sx*(inner_w/2-0.95), 0, -0.7]) cube([1.2, 15, 1.6], center=true);
        }
        // furos: passante + CONTRAFURO reto (cabeca panela M2 recolhe rente)
        corners(post_cx, post_cy){
            translate([0,0,-2]) cylinder(h=lid_th+3, d=screw_free);
            translate([0,0,lid_th-1.4]) cylinder(h=1.5, d=screw_head+0.4);   // Ø4.2 p/ cabeca panela
        }
    }
}

// ============================================================
//  RENDER
// ============================================================
if(part=="base") base();       // corpo (ja traz logo + rotulos GRAVADOS, sem cor)
else if(part=="lid") lid();    // tampa
else {
    base();
    translate([outer_w+10,0,0]) lid();
}
