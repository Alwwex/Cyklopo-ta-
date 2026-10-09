// =====================================================================
// CykloComp - 3D tistena krabicka pro CykloPCB v1 (OpenSCAD, parametricka)
//
// Dily (vyber promennou "part" nebo pouzij build.sh):
//   "base"        - vanicka (PCB, baterie, GPS), otvory pro USB-C, tlacitka, vypinac
//   "lid"         - vicko s oknem displeje (zespodu se do nej prisroubuje displej)
//   "tray"        - deska pod baterii (sroubuje se spolu s PCB)
//   "cleat"       - Garmin quarter-turn "zobacek" (prisroubuje se na dno)
//   "garmin_test" - jen zobacek s sipkou: 10 min tisku, vyzkousej na drzaku!
//   "assembly"    - nahled sestavy (barevne)
//
// PRED TISKEM zmer sve dily posuvkou a uprav parametry nize
// (hlavne rozmery displeje a baterie). Vse ostatni se dopocita.
// Tisk: PETG nebo ASA (PLA na slunci mekne!), 0.2 mm vrstva, 4 obvody.
// =====================================================================

part = "assembly";

/* [Krabicka] */
wall      = 2.0;    // tloustka sten
floor_t   = 2.0;    // tloustka dna
lid_t     = 2.4;    // tloustka vicka
r_out     = 4.0;    // zaobleni rohu
fit       = 0.25;   // vule mezi dily (zvetsi, kdyz to jde ztuha)

/* [Plosny spoj CykloPCB v1] */
pcb       = [43, 64, 1.6];
// montazni otvory M2.5 (souradnice KiCad: x zleva, y od horni hrany = konektory displeje)
pcb_holes = [[3.4, 9.0], [39.6, 9.0], [3.0, 60.6], [39.6, 37.6]];
esp_on_pins = true; // ESP32-C3-Zero na kolickove liste (true) / naplocho (false)
standoff_h = 2.5;   // mezera pod PCB (zkrat vyvody THT na 1.5 mm)
comp_h    = esp_on_pins ? 7.4 : 6.8;  // nejvyssi soucastka nad PCB (ESP+USB-C, uhlova tlacitka)
side_gap  = 0.6;    // PCB <-> bocni stena (aktuator tlacitka ma koukat ven)
rear_gap  = 0.5;    // PCB <-> zadni stena (USB-C)

/* [Baterie Li-Po] */
batt      = [34, 50, 10];   // 103450 = 34 x 50 x 10 mm (803450 = 8 mm -> tensi krabicka)
tray_t    = 1.0;

/* [Displej - ZMER SVUJ MODUL!] */
disp_pcb   = [36.5, 61.2, 1.6];   // deska modulu (2.0" IPS ST7789: cca 36.5 x 61.1)
disp_glass = [35.0, 51.5, 2.6];   // sklo LCD (sirka, delka, vyska nad deskou)
disp_glass_off = -4.0;            // posun stredu skla od stredu desky (minus = od konektoru)
disp_active = [30.6, 40.8];       // aktivni plocha 2.0" (2.4" = 36.7 x 48.96, 2.8" = 43.2 x 57.6)
disp_active_off = [0, 0];         // posun aktivni plochy od stredu skla
disp_holes = [[-15.5, 27.6], [15.5, 27.6]];   // M2 otvory modulu od stredu (u konektoru; [] = jen sevreni sklem)
window_sheet = true;              // zapustene okenko z 1 mm plexi / polykarbonatu (vodotesnost)

/* [GPS] */
gps_zone   = 28;    // delka prostoru pro antenu pred displejem
gps_patch  = [25.5, 25.5];
gps_stack_h = 10;   // vyska modul + keramicka antena (ATGM336H "sendvic")

/* [Garmin quarter-turn] */
// rozmery podle open-source drzaku github.com/chadkirby/quarter-turn-mount
cleat_neck_d = 24.6;   // krcek (orig. 24.9, FDM "naroste")
cleat_neck_h = 1.6;    // vyska krcku = mezera pro limec drzaku
cleat_lug_d  = 28.4;   // rozpeti ousek (orig. 28.6)
cleat_lug_w  = 10.6;   // sirka ousek (orig. 11)
cleat_lug_t  = 1.5;    // tloustka ousek
cleat_angle  = 0;      // 0 = ouska napric pocitacem (vlevo/vpravo); kdyz po zacvaknuti stoji bokem, dej 90
cleat_plate  = [40, 34, 3];

$fn = 48;

// ------------------------------------------------------------- odvozene
W_in = pcb[0] + 2 * side_gap;
L_in = rear_gap + pcb[1] + gps_zone;
pcb_z0 = standoff_h;
pcb_top = pcb_z0 + pcb[2];
tray_z0 = pcb_top + comp_h;
batt_z0 = tray_z0 + tray_t + 0.2;
disp_z0 = batt_z0 + batt[2] + 0.4;          // spodek desky displeje
glass_top = disp_z0 + disp_pcb[2] + disp_glass[2];
glass_pocket = 0.6;                          // sklo zapustene do vicka
H_in = glass_top - glass_pocket;             // vnitrni vyska vanicky (= horni hrana sten)
skirt_d = 3.0;                               // lem vicka zasahujici do vanicky
disp_c = [W_in / 2, rear_gap + pcb[1] / 2];  // stred modulu displeje
boss_d = 7.0;
front_bosses = [[boss_d / 2 + 0.2, L_in - boss_d / 2 - 0.2], [W_in - boss_d / 2 - 0.2, L_in - boss_d / 2 - 0.2]];
boss_top = H_in - skirt_d - 0.3;

// PCB souradnice (KiCad) -> krabicka
function P(x, y) = [side_gap + x, rear_gap + (pcb[1] - y)];

echo(str("Vnitrni rozmer: ", W_in, " x ", L_in, " x ", H_in, " mm"));
echo(str("Vnejsi rozmer: ", W_in + 2 * wall, " x ", L_in + 2 * wall, " x ", H_in + floor_t + lid_t, " mm (+ Garmin ", cleat_neck_h + cleat_lug_t, " mm)"));

module rrect(size, r, h) {
    // zaobleny obdelnik [x,y] od 0,0
    r2 = max(0.01, min(r, size[0] / 2 - 0.01, size[1] / 2 - 0.01));
    translate([r2, r2, 0]) linear_extrude(h) offset(r = r2) square([size[0] - 2 * r2, size[1] - 2 * r2]);
}

module rounded_slot(w, h, depth) {
    // vodorovny zaobleny otvor (osa = Y), stred v 0
    hull() for (s = [-1, 1]) translate([s * (w / 2 - h / 2), 0, 0]) rotate([90, 0, 0]) cylinder(d = h, h = depth, center = true);
}

// ------------------------------------------------------------- VANICKA
module base() {
    difference() {
        union() {
            translate([-wall, -wall, -floor_t]) rrect([W_in + 2 * wall, L_in + 2 * wall], r_out, H_in + floor_t);
        }
        // vnitrni prostor
        rrect([W_in, L_in], r_out - wall, H_in + 1);

        // --- zadni stena: USB-C nabijeni (J1) a USB-C ESP (programovani)
        j1 = P(11.5, 64);
        translate([j1[0], -wall / 2, pcb_top + 1.6]) rounded_slot(12.5, 7.0, wall + 2);
        esp = P(32.0, 64);
        esp_z = pcb_top + (esp_on_pins ? 2.5 : 0) + 1.0 + 1.6;
        translate([esp[0], -wall / 2, esp_z]) rounded_slot(12.5, 7.5, wall + 2);
        // svetlovod nabijeci LED (zalij kapkou cireho lepidla)
        // LED D1/D2 jsou na PCB na x = 18.5; otvor je o 1 mm vedle, aby mezi nim
        // a otvorem USB-C zustala stena (svetlo LED se v krabicce rozptyli)
        led = P(19.6, 59);
        translate([led[0], -wall / 2, pcb_top + 0.8]) rotate([90, 0, 0]) cylinder(d = 2.2, h = wall + 2, center = true);

        // --- leva stena: vypinac + tlacitko MODE
        sw1 = P(2.7, 47.0);
        translate([-wall / 2, sw1[1], pcb_top + 1.8]) rotate([0, 0, 90]) rounded_slot(7.0, 3.2, wall + 2);
        btn1 = P(2.5, 17.35);
        translate([-wall / 2, btn1[1], pcb_top + 3.4]) rotate([0, 90, 0]) cylinder(d = 4.2, h = wall + 2, center = true);
        // --- prava stena: START + PAUZA
        for (y = [17.85, 28.65]) {
            b = P(40.5, y);
            translate([W_in + wall / 2, b[1], pcb_top + 3.4]) rotate([0, 90, 0]) cylinder(d = 4.2, h = wall + 2, center = true);
        }

        // --- drazky pro zadni zobacky vicka
        for (x = [W_in * 0.27, W_in * 0.73])
            translate([x - 4.2, -1.3, H_in - skirt_d + 0.4]) cube([8.4, 1.4, 1.9]);

        // --- otvory ve dne: srouby PCB (zespodu), zobacek Garmin, srouby vicka
        for (h = pcb_holes) {   // slepe otvory: srouby M2.5x12 shora skrz desku pod baterii a PCB
            p = P(h[0], h[1]);
            translate([p[0], p[1], -floor_t + 0.8]) cylinder(d = 2.2, h = standoff_h + floor_t);
        }
        for (s = [-1, 1]) {
            c = [W_in / 2 + s * 16, L_in / 2];
            translate([c[0], c[1], -floor_t - 1]) cylinder(d = 3.4, h = floor_t + 2);
            translate([c[0], c[1], -1.75]) cylinder(d1 = 3.4, d2 = 6.6, h = 1.76);     // zapustena hlava M3 zevnitr
        }
        for (b = front_bosses) {
            translate([b[0], b[1], -floor_t - 1]) cylinder(d = 3.4, h = boss_top + floor_t + 2);  // M3 skrz
            translate([b[0], b[1], -floor_t - 0.01]) cylinder(d = 6.2, h = 3.0);                 // hlava zespodu
        }
    }
    // sloupky pod PCB
    for (h = pcb_holes) {
        p = P(h[0], h[1]);
        difference() {
            translate([p[0], p[1], 0]) cylinder(d = 5.0, h = standoff_h);
            translate([p[0], p[1], -1.2]) cylinder(d = 2.2, h = standoff_h + 2);
        }
    }
    // predni sloupky pro srouby vicka (M3 skrz zespodu do vicka)
    for (b = front_bosses)
        difference() {
            translate([b[0], b[1], 0]) cylinder(d = boss_d, h = boss_top);
            translate([b[0], b[1], -1]) cylinder(d = 3.4, h = boss_top + 2);
        }
    // podstavec pro GPS (antena co nejbliz vicku, keramikou nahoru)
    gx0 = 8; gx1 = W_in - 8;
    gy0 = rear_gap + pcb[1] + 1.0; gy1 = L_in - 0.01;
    gz = H_in - gps_stack_h - 0.3;
    difference() {
        translate([gx0, gy0, 0]) cube([gx1 - gx0, gy1 - gy0, gz]);
        translate([gx0 + 1.2, gy0 - 1, -1]) cube([gx1 - gx0 - 2.4, gy1 - gy0 - 0.2, gz - 0.2]);   // dutina (otevrena dozadu)
        translate([W_in / 2 - (gps_patch[0] + 0.6) / 2, gy0 + (gy1 - gy0) / 2 - (gps_patch[1] + 0.6) / 2, gz - 0.8])
            cube([gps_patch[0] + 0.6, gps_patch[1] + 0.6, 2]);                                    // lozko pro modul/antenu
    }
}

// ------------------------------------------------------------- VICKO
module lid() {
    gc = [disp_c[0], disp_c[1] + disp_glass_off];                     // stred skla
    ac = [gc[0] + disp_active_off[0], gc[1] + disp_active_off[1]];      // stred aktivni plochy
    win = [disp_active[0] + 1.0, disp_active[1] + 1.0];
    sheet = [disp_active[0] + 6.0, disp_active[1] + 6.0];
    difference() {
        union() {
            translate([-wall, -wall, H_in]) rrect([W_in + 2 * wall, L_in + 2 * wall], r_out, lid_t);
            // lem do vanicky
            difference() {
                translate([fit, fit, H_in - skirt_d]) rrect([W_in - 2 * fit, L_in - 2 * fit], r_out - wall - fit, skirt_d + 0.01);
                translate([fit + 1.2, fit + 1.2, H_in - skirt_d - 1]) rrect([W_in - 2 * fit - 2.4, L_in - 2 * fit - 2.4], 1, skirt_d + 2);
            }
            // zadni zobacky (zapadnou do drazek ve stene)
            for (x = [W_in * 0.27, W_in * 0.73])
                translate([x - 3.9, fit - 1.0, H_in - skirt_d + 0.55]) cube([7.8, 1.2, 1.5]);
            // sloupky pro predni srouby
            for (b = front_bosses) translate([b[0], b[1], boss_top + 0.3]) cylinder(d = boss_d, h = H_in - boss_top);
            // sloupky pro prisroubovani displeje (M2 samorezne)
            for (h = disp_holes) translate([disp_c[0] + h[0], disp_c[1] + h[1], disp_z0 + disp_pcb[2]]) cylinder(d = 4.5, h = H_in - disp_z0 - disp_pcb[2] + 0.01);
        }
        // okno displeje se zkosenim
        translate([ac[0], ac[1], H_in - 1]) linear_extrude(lid_t + 2) square(win, center = true);
        translate([ac[0], ac[1], H_in + lid_t - 0.8]) linear_extrude(0.81, scale = [(win[0] + 1.6) / win[0], (win[1] + 1.6) / win[1]]) square(win, center = true);
        // zahloubeni pro ochranne okenko (1 mm plexi/PC, prilep po obvode)
        if (window_sheet)
            translate([ac[0], ac[1], H_in + lid_t - 1.0]) linear_extrude(1.01) offset(r = 1) square([sheet[0] - 2, sheet[1] - 2], center = true);
        // kapsa pro sklo LCD
        translate([gc[0], gc[1], H_in - 0.01 - 0.0]) translate([0, 0, -0.01]) linear_extrude(glass_pocket + 0.01) square([disp_glass[0] + 0.4, disp_glass[1] + 0.4], center = true);
        translate([gc[0], gc[1], H_in - skirt_d - 1]) linear_extrude(skirt_d + 1.01) square([disp_glass[0] + 0.4, disp_glass[1] + 0.4], center = true);
        // otvory pro srouby displeje
        for (h = disp_holes) translate([disp_c[0] + h[0], disp_c[1] + h[1], disp_z0]) cylinder(d = 1.6, h = H_in - disp_z0 - 0.4);
        // predni srouby M3 (samorezne zespodu)
        for (b = front_bosses) translate([b[0], b[1], boss_top]) cylinder(d = 2.6, h = H_in - boss_top + lid_t - 0.8);
        // napis
        translate([W_in / 2, rear_gap + pcb[1] + gps_zone / 2, H_in + lid_t - 0.4])
            linear_extrude(1) text("CykloComp", size = 5, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
}

// ------------------------------------------------------------- DESKA POD BATERII
module tray() {
    ty1 = min(rear_gap + batt[1] + 6, rear_gap + pcb[1] - 7);   // vpredu nechat misto na draty displeje/GPS
    difference() {
        union() {
            translate([side_gap + 0.5, 0.3, tray_z0]) rrect([pcb[0] - 1, ty1 - 0.3], 2, tray_t);
            for (h = pcb_holes) {
                p = P(h[0], h[1]);
                if (p[1] < ty1) translate([p[0], p[1], pcb_top]) cylinder(d = 5.0, h = comp_h + tray_t);
            }
            // baterii prilep oboustrannou penovou paskou (drzi a tlumi otresy)
        }
        for (h = pcb_holes) {
            p = P(h[0], h[1]);
            translate([p[0], p[1], pcb_top - 1]) cylinder(d = 2.9, h = comp_h + tray_t + 4);       // M2.5 x 16 skrz do sloupku
        }
    }
}

// ------------------------------------------------------------- GARMIN ZOBACEK
module cleat_shape() {
    cylinder(d = cleat_neck_d, h = cleat_neck_h + cleat_lug_t);
    rotate([0, 0, cleat_angle]) translate([0, 0, cleat_neck_h]) intersection() {
        cylinder(d = cleat_lug_d, h = cleat_lug_t);
        cube([cleat_lug_d + 1, cleat_lug_w, 2 * cleat_lug_t + 1], center = true);
    }
}

module cleat(test = false) {
    difference() {
        union() {
            translate([-cleat_plate[0] / 2, -cleat_plate[1] / 2, 0]) rrect([cleat_plate[0], cleat_plate[1]], 6, cleat_plate[2]);
            translate([0, 0, cleat_plate[2]]) cleat_shape();
        }
        if (!test) for (s = [-1, 1]) translate([s * 16, 0, -1]) cylinder(d = 2.6, h = cleat_plate[2] - 0.4 + 1);  // M3 samorezne
        // sipka na spodni plose = predek pocitace (smer jizdy); po namontovani na kolo ji vidis
        translate([0, 0, -0.01]) linear_extrude(0.61)
            mirror([0, 1, 0]) polygon([[0, 12], [-5, 4], [-2, 4], [-2, -8], [2, -8], [2, 4], [5, 4]]);
    }
}

// ------------------------------------------------------------- NAHLED SESTAVY
module dummy_parts() {
    color("forestgreen") translate([side_gap, rear_gap, pcb_z0]) cube(pcb);
    color("dimgray") { p = P(32, 52.7); translate([p[0] - 9, p[1] - 11.75, pcb_top + (esp_on_pins ? 2.5 : 0)]) cube([18, 23.5, 1]); }
    color("silver") { p = P(11.5, 61); translate([p[0] - 4.5, p[1] - 3.65, pcb_top]) cube([9, 7.3, 3.2]); }
    color("silver") translate([W_in / 2 - batt[0] / 2, 1.5, batt_z0]) cube(batt);
    color("black") translate([disp_c[0] - disp_pcb[0] / 2, disp_c[1] - disp_pcb[1] / 2, disp_z0]) cube(disp_pcb);
    color([0.1, 0.15, 0.25]) translate([disp_c[0] - disp_glass[0] / 2, disp_c[1] + disp_glass_off - disp_glass[1] / 2, disp_z0 + disp_pcb[2]]) cube(disp_glass);
    color("tan") translate([W_in / 2 - gps_patch[0] / 2, rear_gap + pcb[1] + 1 + (gps_zone - 1) / 2 - gps_patch[1] / 2, H_in - gps_stack_h - 0.3 - 0.8]) cube([gps_patch[0], gps_patch[1], gps_stack_h]);
}

module assembly(explode = 0) {
    color("#2b2f36") base();
    color("#3a3f48") tray();
    dummy_parts();
    color("#2b2f36", 0.85) translate([0, 0, explode]) lid();
    color("#ff7a1a") translate([W_in / 2, L_in / 2, -floor_t]) rotate([180, 0, 0]) cleat();
}

// ------------------------------------------------------------- VYSTUP
if (part == "base") base();
else if (part == "lid") translate([0, 0, H_in + lid_t]) rotate([180, 0, 0]) lid();   // tisk okenkem dolu
else if (part == "tray") translate([0, 0, -tray_z0]) tray();
else if (part == "cleat") cleat();
else if (part == "garmin_test") cleat(test = true);
else if (part == "assembly") assembly(0);
else if (part == "exploded") assembly(18);
