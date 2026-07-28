// ============================================================
//  CASE DO PIN - ESP32-C3 broche  (TALL / ESTILOSO / MULTICOR)
//  Pebble arredondado. Parafuso M2x5 (postes da tampa + placa nas abas).
//  Placa no CHAO (parafusa na base). Bateria mezanino no topo. TP4056 parafusa 2 pads. OLED SOLTO fora; jumpers saem
//  por um buraco entre tampa e caixa (rimo +Y). Tampa LISA.
//  CARGA (USB-C do TP4056) na parede -Y ; ON/OFF na parede +Y (LADO OPOSTO) ; botao na +X.
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
inner_w = 45;                 // X (+/-22.5)
inner_l = 62;                 // Y (+/-31)
inner_h = 28;                 // z 0..28
rout   = 6;                   // raio externo (pebble); r6 contem os postes de canto
rin    = rout - wall;         // raio interno
cham_e = 0.8;                 // chanfro arestas inferiores (anti elephant-foot + costura)
outer_w = inner_w + 2*wall;   // 49
outer_l = inner_l + 2*wall;   // 66
// externo 49 x 66 x 32.4

// ---------- PERFBOARD real (medida): 30mm larg, furos ~2.5mm de cada borda ----------
// Grid 2.54 CENTRADO. Furo externo a ~2.3mm da borda (11 col -> +-12.7). Placa NO CHAO
// (invertida): z3..4.6, apoia em 4 postes de z0. ESP + 2R soldados por cima.
pb_w   = 30;                  // largura real (X)
pb_pitch = 2.54;
pb_th  = 1.6;
pb_z   = 3.0;
function bx(c) = (c - 6)*pb_pitch;      // 11 col: col1=-12.7 .. col11=+12.7
function by(r) = (6 - r)*pb_pitch;      // 11 lin: lin1=+12.7 .. lin11=-12.7
mount = [[2,3],[10,3],[2,9],[10,9]];    // 4 furos p/ cantos: X=+-10.16, Y=+-7.62 (furos reais, folga p/ cabeca M2)

// ---------- BATERIA LiPo 103450 1500mAh (mezanino no topo, apoia nas abas z17, presa pela tampa) ----------
bat_t=10; bat_w=34; bat_l=50;           // vao livre entre postes: X 36.6, Y 53.6, Z 11 (28-17)

// ---------- POSTES da placa (do CHAO ate a placa) + prateleira da bateria ----------
bpost_d=4;                    // 4 postes da placa (nos furos mount), z0..pb_z -> suportes na BASE
bat_ledge_z=16;               // 4 abas onde a bateria apoia; topo z16 -> 12mm ate a tampa (folga p/ celula ate 11mm)

// ---------- TP4056 mezanino (parafusado na parede -Y, USB-C atravessa) ----------
// TP4056 real (medido): 17mm larg, furos ~2.5mm de cada lado -> 2 furos externos a +-6.
// Placa CENTRADA (X=0): o USB-C (centro da larg) alinha com a fenda em X=0. User parafusa
// pelos 2 pads +/- de entrada (perto do USB-C, nao usados): fura p/ 2.0mm, M2 auto-atarraxa
// nos 2 postes. O USB-C femea encosta na parede interna -Y e VAZA pela fenda p/ fora.
tp_w=17;                       // largura real (X)
tp_z0=11.0;                    // fundo do PCB (mezanino; placa principal foi pro chao)
tp_boss_x=[-6, 6]; tp_boss_y=-(inner_l/2-2.5); tp_boss_d=6;  // pads a +-6 (borda-2.5); d6 FUNDE na parede -Y (nao tangencia -> manifold)
// USB-C: centro do vao 2.75mm acima do FUNDO do PCB. Furo E postes saem do MESMO datum
// (tp_z0) -> sobem juntos, porta e pads seguem alinhados.
usb_w=10.0; usb_h=5.0; usb_cx=0; usb_cz=tp_z0+3.0;   // centro p/ PCB 1.6mm; slot alto + counterbore externo -> femea pluga facil
usb_cb_w=12; usb_cb_h=5.0; usb_cb_deep=1.2;          // rebaixo externo alinhado ao slot (sem web de 0.25mm), afina p/ 0.8mm da face

// ---------- CHAVE liga/desliga (parede +Y, OPOSTA da carga) ----------
// Corpo medido: 12(X larg) x 5(Z alt) x 5(Y prof). Atuador na face +Y, desliza em X.
// Bolso retangular FECHADO (4 paredes) abraca o corpo, IGUAL ao socket do botao: o corpo
// encosta na parede interna +Y e o atuador sai por uma fenda que desliza em X.
sw_cx=-11; sw_cz=6;
sw_bw=12; sw_bh=5; sw_bt=5;      // corpo: X larg, Z alt, Y prof
sw_slot_w=7.0; sw_slot_h=3.6;    // fenda do atuador na parede +Y (curso em X; folga p/ knob, ja conta a barriga da coroa)
sw_sock_wall=1.6;                // parede do bolso
sw_ridge_hw=2.5;                 // meia-largura da cumeeira do teto 2 aguas (ponte curta, imprime sem suporte)
sw_roof=1.2;                     // espessura do teto sobre a cumeeira
sw_cb_w=11; sw_cb_h=5; sw_cb_deep=1.0;   // rebaixo externo (acesso do dedo ao atuador, parede -> 1mm)

// ---------- BOTAO momentaneo 6x6 (parede +X) ----------
// Corpo 6x6 num bolso fechado; o plunger Ø3.5 vaza por um LIP FINO na face externa ->
// fica PROUD (~1.2mm pra fora), facil de clicar. O lip so segura o corpo contra sair p/ FORA;
// o clique empurra o corpo p/ DENTRO, entao a retencao real e o bolso justo + um ponto de cola
// atras do corpo (botao e solto, ligado por fio). Dobre as pernas do tactil pra tras antes de
// inserir (vao ~8mm > abertura 6.4mm).
btn_y=0; btn_z=8;
btn_body=6.0;            // corpo 6x6
btn_plunger_d=4.6;       // furo do plunger (Ø3.5 + folga)
btn_lip=0.8;             // parede fina do lip (plunger sobra ~P-0.8mm)
btn_sock_wall=1.6;       // parede do bolso
btn_sock_d=4.5;          // prof do bolso (X): abriga o corpo 6x6 e guia a insercao

// ---------- POSTES DE CANTO (chao -> tampa, M2) ----------
post_d=5; post_cx=inner_w/2-1.7; post_cy=inner_l/2-1.7;   // vao entre postes = inner - 8.4, folga ~1.3mm/lado na bateria

// ---------- TAMPA LISA + passa-cabo no rimo ----------
cable_notch_w=12; cable_notch_h=3.2;   // buraco do cabo entre tampa e caixa (rimo +Y)

// ---------- LOGO no fundo externo ----------
logo_txt="marmota.dev.br"; logo_size=3.0; logo_deep=1.0; logo_y=-25;   // size 3.0 (cabe c/ margem) ; 1.0 = gravado fundo

show_refs=false;

// ============================================================
//  HELPERS
// ============================================================
module rr(w,l,h,r){ linear_extrude(height=h) offset(r=r) square([w-2*r, l-2*r], center=true); }
module corners(cx,cy){ for(sx=[-1,1],sy=[-1,1]) translate([sx*cx, sy*cy, 0]) children(); }
// slot stadium numa parede +-Y (sy=-1 parede -Y, sy=+1 parede +Y) + lead-in chanfrado na face externa
module slotY(cx, cz, w, h, sy=-1){
    dx=(w-h)/2;
    hull() for(sx=[-1,1])   // furo passante
        translate([cx+sx*dx, sy*(inner_l/2+wall/2), cz]) rotate([90,0,0]) cylinder(h=wall+3, d=h, center=true, $fn=40);
    hull() for(sx=[-1,1]){  // funil de encaixe na face externa
        translate([cx+sx*dx, sy*(outer_l/2+0.05), cz]) rotate([90,0,0]) cylinder(h=0.02, d=h+2*cham, center=true, $fn=40);
        translate([cx+sx*dx, sy*(outer_l/2-cham),  cz]) rotate([90,0,0]) cylinder(h=0.02, d=h,        center=true, $fn=40);
    }
}

// --- BOTAO 6x6 (parede +X): bolso-tubo (material) + corte (bolso + furo do plunger) ---
module button_socket(){                       // caixa em volta do corpo, encostada no lip
    x_out = outer_w/2 - btn_lip;              // face interna do lip
    w = btn_body + 2*btn_sock_wall + fit;     // Y/Z externo
    top = btn_z + w/2;                        // desce ate o CHAO (z0) -> sem overhang, apoia na mesa
    translate([x_out - btn_sock_d/2, btn_y, top/2]) cube([btn_sock_d, w, top], center=true);
}
module button_cut(){
    x_out = outer_w/2 - btn_lip;
    pl = btn_sock_d + 1;                       // tubo aberto pro interior -X (corpo encosta no lip)
    translate([x_out - pl/2 + 0.01, btn_y, btn_z]) cube([pl, btn_body+fit, btn_body+fit], center=true);
    translate([x_out-0.1, btn_y, btn_z]) rotate([0,90,0]) cylinder(h=btn_lip+0.3, d=btn_plunger_d, $fn=48);  // furo do plunger no lip
    translate([outer_w/2-0.4, btn_y, btn_z]) rotate([0,90,0]) cylinder(h=0.6, d1=btn_plunger_d, d2=btn_plunger_d+1.2, $fn=48);  // funil externo (land 0.4mm, sem fio de faca)
}

// --- CHAVE (parede +Y): bolso-tubo retangular (material) + corte (bolso + fenda do atuador) ---
function sw_pk_top() = sw_cz + (sw_bh+fit)/2;               // topo do bolso (base da cumeeira)
function sw_ridge_z() = sw_pk_top() + ((sw_bw+fit)/2 - sw_ridge_hw);   // altura da cumeeira (encostas 45)
module switch_socket(){
    w = sw_bw + 2*sw_sock_wall + fit;         // X externo
    d = sw_bt + sw_sock_wall + fit;           // Y prof (parede interna -> fundo do socket)
    top = sw_ridge_z() + sw_roof;             // z0 ate acima da cumeeira -> sem overhang embaixo, teto 2 aguas em cima
    translate([sw_cx, inner_l/2 - d/2 + 0.01, top/2]) cube([w, d, top], center=true);
}
module switch_cut(){
    fy = inner_l/2;                                          // frente do bolso = parede interna +Y
    by_ = inner_l/2 - (sw_bt + sw_sock_wall + fit) - 1;      // 1mm alem do fundo -> tubo aberto atras (-Y)
    translate([sw_cx, (fy+by_)/2, sw_cz]) cube([sw_bw+fit, fy-by_, sw_bh+fit], center=true);   // bolso do corpo
    // teto em DUAS AGUAS: remove o pico -> encostas 45 (auto-suporte) + cumeeira curta (ponte facil)
    half=(sw_bw+fit)/2;
    translate([sw_cx, (fy+by_)/2, 0]) rotate([90,0,0])
        linear_extrude(height=fy-by_+0.1, center=true)
        polygon([[-half,sw_pk_top()],[half,sw_pk_top()],[sw_ridge_hw,sw_ridge_z()],[-sw_ridge_hw,sw_ridge_z()]]);
    slotY(sw_cx, sw_cz, sw_slot_w, sw_slot_h, 1);            // fenda do atuador (desliza em X) + funil externo
    // counterbore externo na parede +Y: afina p/ ~1mm -> dedo alcanca o atuador
    translate([sw_cx, (outer_l/2) - sw_cb_deep + (sw_cb_deep+0.5)/2, sw_cz])
        cube([sw_cb_w, sw_cb_deep+0.5, sw_cb_h], center=true);
}

module txt(t,s,sp=1.25){ text(t, size=s, spacing=sp, halign="center", valign="center", font="DejaVu Sans:style=Bold"); }
// Gravacoes (2a cor): logo no fundo + rotulo de cada entrada na parede.
lbl_sz=2.4;
module engravings(){
    dw=logo_deep+0.02;    // paredes: recesso 1.0
    df=0.8+0.02;          // fundo do case: 0.8 (mais legivel; floor_th=2 aguenta)
    // fundo externo (raso) - spacing 1.1 (separa o "rm" sem exagerar; baseline alinhada)
    translate([0, logo_y, -floor_th-0.01]) mirror([1,0,0]) linear_extrude(df) txt(logo_txt, logo_size, 1.1);
    // parede -Y (carga): USB-C, abaixo do furo
    translate([usb_cx, -outer_l/2+logo_deep, usb_cz-4.2]) rotate([90,0,0]) linear_extrude(dw) txt("USB-C", lbl_sz);
    // parede +Y (oposta): ON/OFF. rotate 180 em Z -> glifos leem certo p/ quem olha do lado +Y
    translate([sw_cx, outer_l/2-logo_deep, sw_cz-4.2]) rotate([90,0,180]) linear_extrude(dw) txt("ON/OFF", lbl_sz);
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
            // postes de canto: param 0.4mm ABAIXO do rim -> a tampa assenta no RIM continuo
            // (nao nos topos dos postes, que imprimiam proeminentes e impediam o encaixe)
            corners(post_cx, post_cy) cylinder(h=inner_h-0.4, d=post_d);
            corners(post_cx, post_cy) translate([0,0,inner_h-0.9]) cylinder(h=0.5, d1=post_d, d2=post_d-1.5); // lead-in do topo
            // POSTES da placa: do CHAO ate a placa (invertido -> suportes na BASE)
            for(m=mount) translate([bx(m[0]), by(m[1]), 0]) cylinder(h=pb_z, d=bpost_d);
            // prateleira da bateria: 4 abas 9x9 -> apoio real ~3.5x3mm/canto sob a bateria 34x50.
            // Subface em CUNHA (hull ate o canto da parede, z baixo) -> rampa 45, imprime sem suporte.
            for(sx=[-1,1],sy=[-1,1]) hull(){
                translate([sx*(inner_w/2-4.5), sy*(inner_l/2-4.5), bat_ledge_z-0.75]) cube([9,9,1.5], center=true);
                translate([sx*(inner_w/2-0.5), sy*(inner_l/2-0.5), bat_ledge_z-7]) cube([1,1,1], center=true);
            }
            // bosses do TP4056 (parede -Y, mezanino)
            for(x=tp_boss_x) translate([x, tp_boss_y, 0]) cylinder(h=tp_z0, d=tp_boss_d);
            button_socket();   // bolso-tubo 6x6 do botao (parede +X)
            switch_socket();   // bolso-tubo retangular da chave (parede +Y)
        }
        // --- furos de rosca (M2 self) ---
        corners(post_cx, post_cy) translate([0,0,inner_h-5.5]) cylinder(h=6, d=screw_self);  // tampa (sai limpo no topo)
        // placa: piloto nos 4 postes (M2x5 cego -> ponta para no chao, nao fura o fundo)
        for(m=mount) translate([bx(m[0]), by(m[1]), -1]) cylinder(h=pb_z+1.5, d=screw_self);
        // TP4056: piloto M2 nos 2 postes (parafusa pelos 2 pads +/- furados a 2.0mm)
        for(x=tp_boss_x) translate([x, tp_boss_y, tp_z0-4.5]) cylinder(h=5, d=screw_self);

        // --- CORTES nas paredes ---
        slotY(usb_cx, usb_cz, usb_w, usb_h);      // USB-C stadium + lead-in (parede -Y)
        // counterbore externo na parede -Y: afina o vao do USB pra wall-usb_cb_deep (~0.8mm) e
        // aproxima a femea da face -> plugue mata facil mesmo com a porta recuada 0.6..1.0mm do PCB.
        translate([usb_cx, -(outer_l/2) + usb_cb_deep - (usb_cb_deep+0.5)/2, usb_cz])
            cube([usb_cb_w, usb_cb_deep+0.5, usb_cb_h], center=true);
        switch_cut();                             // bolso + fenda do atuador (parede +Y, oposta)
        button_cut();                             // bolso + furo do plunger (parede +X)
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
            // lips de registro por dentro das paredes +-X: RIB AFINADA (lead-in) c/ folga ~0.8mm
            // (antes cube justo 0.35mm -> prendia; agora entra guiando e assenta sem forcar)
            for(sx=[-1,1]) hull(){
                translate([sx*(inner_w/2-1.3), 0, -0.2]) cube([1.0, 15, 0.6], center=true);  // topo cruza z=0 (cola na tampa)
                translate([sx*(inner_w/2-1.9), 0, -1.5]) cube([0.2, 12, 0.4], center=true);  // ponta fina, recuada
            }
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
