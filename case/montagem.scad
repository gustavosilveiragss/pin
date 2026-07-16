// ============================================================
//  MONTAGEM do pin: case + TODOS os componentes nos lugares reais
//  Layout invertido (placa no chao, bateria mezanino no topo).
//  show = "open" | "closed" | "xray"
// ============================================================
use <pin-case.scad>
$fn=48;
show = "open";

module rbox(w,l,h,r=0.6){ hull() for(sx=[-1,1],sy=[-1,1]) translate([sx*(w/2-r),sy*(l/2-r),0]) cylinder(r=max(0.1,r),h=h); }

// ---- componentes (cores realistas) ----
module perfboard(){
    color("#c9a24b") difference(){                     // FR4 tan
        translate([0,0,3]) rbox(25.4,30.5,1.6,1);
        for(c=[0:9]) for(r=[0:11])
            translate([-11.43+c*2.54, 13.97-r*2.54, 2.9]) cylinder(d=1.0,h=2);
    }
}
module esp(){
    translate([-2.5,7.6,4.6]){
        color("#1c2a49") rbox(18,15,1.2,1);            // PCB azul-escuro
        color("#111")    translate([2.5,0,1.2]) cube([5,5,1.4],center=true);   // chip
        color("#c8c8cc") translate([-9-1.6,0,0.4])  cube([3.4,9,3.2],center=true); // USB-C (-X)
        color("#c9a24b") translate([-3,6.4,1.2]) cube([11,1.6,0.6],center=true);   // antena
    }
}
module resistors(){
    for(y=[9,12]) color("#2a2a2a") translate([bx(7), by(y), 4.6]) cube([5,2,1.4],center=true);
}
module tp4056(){
    translate([4,-9,11]){                              // -Y, USB pra -Y
        color("#123a6b") rbox(18,26,1.2,1);            // PCB azul (o modulo real e azul)
        color("#c8c8cc") translate([0,-13-1.3,0.6]) cube([9,3.2,3.2],center=true); // USB-C na borda -Y
        color("#111")    translate([0,2,1.2]) cube([5,5,1.6],center=true);         // TP4056/DW01
        color("#111")    translate([0,-4,1.2]) cube([4,3,1.4],center=true);
    }
}
module battery(){
    translate([0,0,17]) color("#b9bcc2") rbox(30,35,5.5,3);   // pouch prata
    translate([0,0,17]) color("#8f9298") rbox(30,35,0.1,3);
    color("#d8b24a") translate([0,17,19.5]) cube([8,2,3],center=true);  // fita kapton (fios)
}
module switch_ss(){
    translate([-11,-21.3,6]){
        color("#1a1a1a") cube([8.5,3.5,3.5],center=true);        // corpo preto
        color("#c8c8cc") translate([0,-2,0]) cube([9.5,1,3.5],center=true); // frame metal
        color("#111")    translate([0,-2.2,0]) cube([1.6,1.5,2.4],center=true); // knob
    }
}
module button_t(){
    translate([20.5,0,8]) rotate([0,90,0]){                     // parede +X
        color("#1a1a1a") translate([0,0,-3.5]) cube([6,6,3.5],center=true); // corpo 6x6 (dentro)
        color("#222")    cylinder(d=3.5,h=2.5);                 // plunger p/ fora
    }
}
// (OLED fora da simulacao — ele fica solto por fora, ligado por jumpers pelo notch)

module all_internals(){ perfboard(); esp(); resistors(); tp4056(); switch_ss(); button_t(); }

// ---- cena ----
if(show=="closed"){                  // broche pronto
    color("#e0892a") base();
    color("#e0892a") translate([0,0,24]) lid();
    battery();
}
else if(show=="guts"){               // eletronica montada (sem bateria/tampa)
    color("#e0892a") base();
    perfboard(); esp(); resistors(); switch_ss(); button_t(); tp4056();
}
else if(show=="exploded"){           // tudo separado
    color("#e0892a") base();
    perfboard(); esp(); resistors(); switch_ss(); button_t();
    translate([0,0,12]) tp4056();
    translate([0,0,26]) battery();
    color("#e0892a") translate([0,0,64]) lid();
}
else if(show=="section"){            // corte vertical: mostra o empilhamento
    difference(){
        union(){
            color("#e0892a") base();
            color("#e0892a") translate([0,0,24]) lid();
            all_internals(); battery();
        }
        translate([-60,0.1,-10]) cube([120,60,90]);   // corta y>0
    }
}
else if(show=="xray"){
    color([0.88,0.54,0.16,0.25]) base();
    color([0.88,0.54,0.16,0.25]) translate([0,0,24]) lid();
    all_internals(); battery(); oled_ext();
}
else {  // open
    color("#e0892a") base();
    all_internals(); battery();
}
