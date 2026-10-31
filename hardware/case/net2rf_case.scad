// Net2RF LED carrier - printable case (base + snap-on lid)
//
// Coordinates follow the PCB: x across the board, y from the antenna end (0) to the connector end (H),
// z up from the bottom face of the PCB. bx()/cyl() flip y so the printed part is not mirrored.
//
//   part = "base" | "lid" | "assembly"     lid is exported upside down, outer face on the bed
//
// Sizes marked CHECK come from typical figures or one measurement and are the ones to verify.

part = "assembly";

REV = "R27";                      // case revision, engraved inside both parts. Bump it whenever the STLs change.
AMP = true;                        // false: the slim case, for builds without the amplifier (and so without the buck)
OLED = "ssd1306";                  // "ssd1306": the 27.9 x 28.3 module; "ssd1315": the 24.7 x 27 one (lid only differs)

// ---- board (EasyEDA Pro project, carrier as fabbed 2026-10: positions from the exports of 2026-10-19) ----
W = 57.15;  H = 62;  PCB_T = 1.6;
XM = 36.576;                       // centre line of the WT32-ETH01 and its RJ45 (socket rows at x = 25.146 and 48.006)
HOLES = [[24, 5.08], [53.65, 3.5], [24, 58.5], [53.65, 58.5]];
SUPPORTS = [[3, 6], [3, 31], [2.5, 43],       // plain posts under the left third (no screw), clear of the J9/J10 and DC jack pins
            [54.1, 35.05], [54.1, 50.8]];     // ...and right under USER (SW3) and EN (SW1), so pressing them does not flex the board
SUPPORT_R = 2.0;

// ---- case ----
WALL = 2;  FLOOR_T = 2;  LID_T = 2;
GAP = 1.5;                         // board to the right wall
// Board to the left wall: OLED cable out of J7 and the buck, then the amplifier bay. Slim: the OLED cable and the
// Ra-02 pigtail, which runs along this strip from the far-wall SMA (about 20 mm of shrink behind the bulkhead)
// and turns in to the Ra-02's u.FL about 21 mm from the board's far edge; or a spring antenna (about 6 mm across)
// tied to the left-wall tie bridge, clear of the Ra-02 adapter.
GAP_L = AMP ? 24.5 : 10.5;
// Board to the far wall. R25: +9 mm so a 17 mm right-angle SMA plug can be threaded onto the amp's IN with the amp
// screwed in. Slim: just room for the SMA's hex pocket and boss in front of the board edge.
GAP_FAR = AMP ? 21.5 : 3.5;
FLOOR_Z = -4.5;                    // inside floor: standoff height under the board
LID_Z = 24.5;                      // underside of the lid: the amplifier stands 27 mm tall
CLR = 0.2;                         // fit clearance between lid features and the walls
CORNER_R = 3;                      // outside corner radius; inside corners are CORNER_R - WALL
EDGE_CH = 0.6;                     // chamfer on the bottom edge of the base and the top edge of the lid
x0 = -GAP_L;  x1 = W + GAP;  y0 = -GAP_FAR;  y1 = H - WALL;      // inside faces; the connector wall is flush with the board edge
FRONT = 1.2;                       // the connector face stands this far in front of the board edge, so a 1 mm skin hides the PCB
X0 = x0 - WALL;  X1 = x1 + WALL;  Y0 = y0 - WALL;  Y1 = H + FRONT;

// connector wall openings (0.5 mm clearance round each part)
SLOT = [-0.3, 1.9];                // z range left clear for the board edge
RJ = [XM - 8.5, XM + 8.5, -1.4, 13.1];
ST = [9.83, 20.83, 12.1];          // screw terminal J2 (10 mm body centred on x = 15.33): x range, up to this z
DC = [-1, 10.15, 13.1];            // barrel jack J1 (9 mm body centred on x = 4.93, against the left wall)
// ---- amplifier bay (AB-IOT-433, optional) ----
// The amp stands on its long edge along the left wall, heatsink towards the wall. Its OUT SMA goes through a hole
// in the far wall at the left corner, next to the Ra-02, and a 1/4-36 nut outside clamps it. Without the amp, the
// Ra-02 pigtail's bulkhead SMA uses the same hole, so the antenna is in the same place on every unit. The IN end
// (SMA + JST power plug) faces the connector end, beside the buck.
// R15: a plain round hole in the base wall again (R9-R14 had a top-open slot closed by a tab on the lid); the lid
// stays clear of the SMA. The pocket in the inside boss is open at the top, so the part drops in and slides into
// the hole. The pocket runs 0.5 mm into the wall, which is 1.5 mm thick at the hole, and the boss is slotted for
// the amp's board edge so its flange bottoms in the pocket: more thread left outside.
HEX_AF = 8.0 + 0.3;                             // hex across flats (+ clearance)   CHECK against the pigtail
BOSS_R = 6.5;  BOSS_T = 2.5;                    // boss on the inside face
WALL_AT_SMA = 1.5;                              // wall left at the hole
POCKET_D = BOSS_T + WALL - WALL_AT_SMA;         // pocket depth from the boss face
AMP_FL_D = 2.0;                                 // depth of the amp's SMA flange along the connector   CHECK
AMP_FL = 6.5 + 0.4;                             // square flange of the amp's edge-mount SMA (+ clearance)   CHECK (photo)
// The pocket is the hex and that square together, so either part seats in it and cannot turn. The amp's board
// edge stops on the boss face with its flange in the pocket, leaving about 7 mm of thread outside.
AMP_L = 49.5;  AMP_W = 27;  AMP_PCB = 1.6;      // board length, width (= height when standing), thickness   measured
AMP_HS = 6.5;  AMP_SH = 2.9;                    // heatsink side, shield side, beyond the board faces   CHECK (total 10.5-11)
AMP_SMA_UP = 6.5;                               // SMA axis above the board's lower long edge   CHECK (from a photo)
AMP_Y0 = y0 + BOSS_T - POCKET_D + AMP_FL_D;     // OUT end of the board: flange at the bottom of the pocket
AMP_Y1 = AMP_Y0 + AMP_L;                        // IN end; 30.5 mm to the front wall: 8.5 mm of SMA, 17 mm plug, 3 mm spare while threading
AMP_X = x0 + 1 + AMP_HS + AMP_PCB / 2;          // plane of the amp's board (the SMA axis lies in it)
AMP_RAIL = 2.0;  AMP_SEAT = 1.2;                // floor rail height; how deep the board's edge sits in its groove
AMP_Z0 = FLOOR_Z + AMP_RAIL - AMP_SEAT;         // lower edge of the amp's board
SMA_Z = AMP_Z0 + AMP_SMA_UP;
// Antenna hole x. Slim: the pigtail's shrink (about 4 mm across) runs 1.5 mm clear of the board's left edge, and the
// nut outside stays on the flat of the far wall.
ANT_X = AMP ? AMP_X : -3.5;
// The amp screws to two standoffs on the left wall through the plated holes near each end of its board
// (M2 self-tapping, from the shield side). Fit the amp before the carrier board: the board blocks the screwdriver.
AMP_HOLE_CC = 34;                               // hole centre to centre   measured
AMP_HOLE_IN = (AMP_L - AMP_HOLE_CC) / 2;  AMP_HOLE_UP = 12;     // from each end (assumed centred); above the lower edge   measured
AMP_TEST_Z = 13;                                // top of the amp_test piece (above the posts)
AMP_ST_R = 2.2;  AMP_PILOT_D = 1.7;             // standoff radius; pilot for an M2 self-tapping screw
SMA_R = 3.3;                                    // 1/4-36 thread, 6.35 mm
// The only hole out of the box. A boss on the inside of the far wall (the outside stays flat) holds a hex pocket
// that stops the no-amp pigtail's bulkhead SMA turning; with the amp, its flange sits on the boss and about
// 5 mm of thread is left outside for the antenna.
// 5 V buck (12 V from J6 in, 5 V to the amp's JST): R19 moved it to the far wall at the right, strapped flat
// against the wall by the second tie bridge (TIEB_X 38), away from the amp's IN connector and the Ra-02 pigtail.
// Heat-shrink it. It measures 19 x 13 x 5 mm: it stands with its face on the bridge's front face (bottom edge level
// with the bridge's bottom, about z 3.5), and the tie runs down the slot behind the bridge, under the buck and up
// its front. The floor groove beside the amp (buck_rail(), BUCK_*) is no longer built.
BUCK_PX = -5.5;  BUCK_PCB = 1.2;  BUCK_YC = 50.5; // board plane, board thickness, centre along y   CHECK when the modules arrive
BUCK_RAIL_L = 14;  BUCK_RAIL_H = 5;  BUCK_SEAT = 2.5;   // groove rail length and height; how deep the edge sits
TIE_W = 5;  TIE_H = 1.6;                        // tunnel for the cable tie (fits ties up to 4.8 mm wide)

// cable-tie bridge on the inside of the far wall: two vertical slots between it and the wall, open at the top and
// bottom, so a tie threads down behind it and straps a cable or an internal antenna running along the wall. The
// underside slopes at 45 degrees and the slots are vertical, so it prints without support.
TIEB_X = [8, 38];  TIEB_W = 16;  TIEB_D = 4;              // centre x, width along the wall, depth from the wall
TIEB_Z = [8, 16];                                  // bottom and top of the bridge's full-depth part
TIEB_SLOT_T = 1.8;  TIEB_SLOT_W = 5;  TIEB_SLOT_DX = 4;   // slot depth from the wall, width, offset of each slot from TIE_X
TIEB_LY = 16;                                      // slim case: one bridge on the left wall instead, centred at this y
                                                   // (y 8-24, clear of the J7 plug and its wires at about y 35)

// standoffs: M3 screws cut their own thread in a 2.6 mm pilot hole
STAND_R = 3.0;  PILOT_D = 2.6;
HEAD_R = 4.5;  HEAD_H = 3.6;       // clearance for an M3 head above the board: R26 up from 3.4 / 2.8, which still touched (9 mm across, 3.6 mm tall)

// snap fit
LIP_T = 1.2;  LIP_D = 3;  lz = LID_Z - LIP_D;
SNAP = 0.9;                        // ridge height. Engagement is SNAP - CLR. Raise for a tighter lid. (0.6 printed too loose)
SNAP_Y = [2, 42];  SNAP_L = 12;
SNAP_X = AMP ? [0, 40] : [14, 42]; // ridges on the far (antenna-end) lip too; slim: clear of the SMA pocket
SNAP_FRONT_X = -12;                // one ridge on a short lip inside the front wall, left of the DC jack (full-height base wall there)

// OLED module                                                         CHECK all of these
// SSD1315: from the seller's drawing (board 24.7 x 27 +-0.2, 1.2 thick, glass about 1.5 above it, 2.3 max; visible
// area 22.74 x 11.86, centred across the board, 6.08 mm from the header edge). Not yet fitted.
S15 = OLED == "ssd1315";
OLW = S15 ? 24.7 : 27.9;  OLH = S15 ? 27 : 28.3;   // board, nominal; the pocket width comes from FEN_G
O_PCB = S15 ? 1.2 : 1.4;  O_GLASS = 1.6;           // board thickness, glass height above the board
WIN_C = S15 ? 12.01 : 12.15;                       // display centre, from the header edge
// Placed so the display window is centred on the lid (WIN below sits 5.35-18.95 mm from the header edge, so its
// centre is 12.15 mm in); the header edge faces the antenna end. Slim: no further right than x 22, about 2.8 mm left
// of the lid's centre, so the right rail stays 1.5 mm clear of the USER tongue's root.
ox0 = min((X0 + X1) / 2, 22) - OLW / 2;  oy0 = (Y0 + Y1) / 2 - WIN_C;  ox1 = ox0 + OLW;  oy1 = oy0 + OLH;
oz = LID_Z - O_GLASS - O_PCB;      // back face of the OLED board
ocx = (ox0 + ox1) / 2;
// Test piece G (oled_test.scad) fitted best: pocket 27.3 mm wide (FEN_G = -0.30) with a 0.3 mm wedge.
// Nothing latches the OLED: it is held by the rails; a dab of glue at the header end stops it sliding out.
// SSD1315: 25.0 until a test piece says otherwise. Its glass is as wide as the board (24.74 +-0.2), so no squeeze.
FEN_G = S15 ? 0.15 : -0.30;  FEN_T = 1.2;  FEN_A = 4;          // side gap (negative: tighter than nominal), wall thickness, end-stop length
FEN_END = 0.15;                                             // gap at the end stops
WEDGE = 0.3;                                                // the sloped face starts this far towards the lid from the board's back face
RAIL_IN = 1.2;  RAIL_T = 0.8;                               // rail ledge reach under the board; ledge thickness below the slope
WIN = [ocx - 11.5, ocx + 11.5, oy0 + WIN_C - 6.8, oy0 + WIN_C + 6.8];   // 23 x 13.6, as on the test pieces
WIN_CH = 0.8;                                              // chamfer round the window on the outer face

// USER button flexure
BTN = [54.1, 35.05];  BTN_TOP = 3.1;                       // SW3; switch cap height above the PCB bottom   CHECK
TONGUE_W = 6;  TONGUE_T = 1.2;  SLIT = 0.8;  POST_R = 2;  POST_GAP = 0.3;
FLARE_R = 2.9;  FLARE_H = 3;       // cone where the post joins the tongue
RAMP = 4;                          // length over which the tongue thins, so the root has no sharp step
// EN (reset) button SW1 gets a tongue too, parallel to USER's; BOOT (SW2), between them, stays covered.
ENB = [54.1, 50.8];
LABEL_SIZE = 3.4;  LABEL_D = 0.3;                     // depth of the USER / RST labels engraved on the tongues (they are only TONGUE_T thick)
BTN_TIP = 56.3;  TONGUE_L = 18;    // both tongues run across the lid, root towards the middle, tip at the right edge

$fn = 48;

module bx(xa, xb, ya, yb, za, zb) {
    translate([min(xa, xb), H - max(ya, yb), min(za, zb)])
        cube([abs(xb - xa), abs(yb - ya), abs(zb - za)]);
}
module cyl(x, y, za, zb, r) { translate([x, H - y, za]) cylinder(r = r, h = zb - za); }
module rrect(xa, xb, ya, yb, za, zb, r) {      // box with rounded vertical edges
    hull() for (x = [xa + r, xb - r]) for (y = [ya + r, yb - r]) cyl(x, y, za, zb, r);
}

// ------------------------------------------------------------------ base
module base() {
    difference() {
        union() {
            difference() {
                hull() {
                    rrect(X0 + EDGE_CH, X1 - EDGE_CH, Y0 + EDGE_CH, Y1 - EDGE_CH, FLOOR_Z - FLOOR_T, FLOOR_Z - FLOOR_T + 0.01, CORNER_R - EDGE_CH);
                    rrect(X0, X1, Y0, Y1, FLOOR_Z - FLOOR_T + EDGE_CH, LID_Z, CORNER_R);
                }
                rrect(x0, x1, y0, y1, FLOOR_Z, LID_Z + 1, CORNER_R - WALL);
            }
            for (h = HOLES) cyl(h[0], h[1], FLOOR_Z - 0.01, 0, STAND_R);
            translate([ANT_X, H - y0 - BOSS_T, SMA_Z]) rotate([-90, 0, 0]) cylinder(r = BOSS_R, h = BOSS_T + 0.01);   // antenna boss, inside
            for (p = SUPPORTS) cyl(p[0], p[1], FLOOR_Z - 0.01, 0, SUPPORT_R);
            if (AMP) {
                amp_rail();
                amp_standoffs();
                tie_bridge();
            } else {
                tie_bridge_left(TIEB_LY);
            }
        }
        // connector wall: only a low sill under the board stays on the base; the rest hangs from the lid
        bx(-0.3, W + 0.3, y1 - 0.1, H + CLR, SLOT[0], LID_Z + 1);               // where the board edge sits
        bx(-0.3, W + 0.3, H + CLR - 0.01, Y1 + 0.1, SLOT[1], LID_Z + 1);        // skin in front of the board edge stops at board-top height
        bx(RJ[0], RJ[1], y1 - 0.1, Y1 + 0.1, RJ[2], LID_Z + 1);
        // antenna: the amp's OUT SMA, or without the amp the Ra-02 pigtail's bulkhead SMA, through the far wall
        // (an internal antenna: blank it with ant_plug())
        translate([ANT_X, H - y0 - BOSS_T - 0.1, SMA_Z]) rotate([-90, 0, 0]) cylinder(r = SMA_R, h = WALL + BOSS_T + 0.3);   // round hole
        // pocket in the boss, from the inside: hex (flats vertical, a corner at the bottom), open at the top so
        // the part drops in
        hull() for (z = [SMA_Z, LID_Z + 1])
            translate([ANT_X, H - y0 - BOSS_T - 0.1, z]) rotate([-90, 0, 0]) rotate([0, 0, 90]) cylinder(r = HEX_AF / sqrt(3), h = POCKET_D + 0.1, $fn = 6);
        if (AMP) {
            // ...plus the square of the amp's SMA flange, and a slot for the amp's board edge
            bx(AMP_X - AMP_FL / 2, AMP_X + AMP_FL / 2, y0 + BOSS_T + 0.1, y0 + BOSS_T - POCKET_D, SMA_Z - AMP_FL / 2, LID_Z + 1);
            bx(AMP_X - AMP_PCB / 2 - 0.3, AMP_X + AMP_PCB / 2 + 0.3, y0 + BOSS_T + 0.1, y0 - 0.01, FLOOR_Z, LID_Z + 1);
            // vents in the left wall, opposite the amp's heatsink
            for (y = [AMP_Y0 + 8 : 5 : AMP_Y1 - 8]) hull() {
                translate([x0 + 0.1, H - y, AMP_Z0 + 5]) rotate([0, -90, 0]) cylinder(r = 1, h = WALL + 0.2);
                translate([x0 + 0.1, H - y, AMP_Z0 + 19]) rotate([0, -90, 0]) cylinder(r = 1, h = WALL + 0.2);
            }
        }
        // grooves for the lid's snap ridges (slim: no front ridge, the strip left of the DC jack is too short)
        if (AMP) bx(SNAP_FRONT_X - SNAP_L / 2 - 0.3, SNAP_FRONT_X + SNAP_L / 2 + 0.3, y1 - 0.01, y1 + (SNAP - CLR) + 0.15, lz + 0.25, lz + 1.75);
        for (x = SNAP_X)
            bx(x - SNAP_L / 2 - 0.3, x + SNAP_L / 2 + 0.3, y0 - (SNAP - CLR) - 0.15, y0 + 0.01, lz + 0.25, lz + 1.75);
        for (y = SNAP_Y) {
            bx(x0 - (SNAP - CLR) - 0.15, x0 + 0.01, y - SNAP_L / 2 - 0.3, y + SNAP_L / 2 + 0.3, lz + 0.25, lz + 1.75);
            bx(x1 - 0.01, x1 + (SNAP - CLR) + 0.15, y - SNAP_L / 2 - 0.3, y + SNAP_L / 2 + 0.3, lz + 0.25, lz + 1.75);
        }
        for (h = HOLES) cyl(h[0], h[1], FLOOR_Z - FLOOR_T + 1.0, 1, PILOT_D / 2);   // 5.5 mm below the board: M3x6 thread-forming, 1 mm of floor left
        // revision mark, engraved in the floor under the WT32
        translate([XM, H - 28, FLOOR_Z - 0.5]) linear_extrude(0.6)
            text(REV, size = 6, font = "Arial:style=Bold", halign = "center", valign = "center");
    }
}

// floor rail with a groove the amp's lower edge drops into. The shield can runs almost to the board edge, so
// the wall on that side is kept low.
module amp_rail() {
    g = AMP_PCB + 0.4;  za = FLOOR_Z - 0.01;  zt = FLOOR_Z + AMP_RAIL;
    ya = AMP_Y0 + 3;  yb = AMP_Y1 - 3;
    bx(AMP_X - g / 2 - 2, AMP_X - g / 2, ya, yb, za, zt);                      // heatsink side
    bx(AMP_X + g / 2, AMP_X + g / 2 + 1.5, ya, yb, za, zt - AMP_SEAT + 0.6);   // shield side, low
    bx(AMP_X - g / 2 - 0.01, AMP_X + g / 2 + 0.01, ya, yb, za, AMP_Z0);        // groove floor
}

// horizontal standoffs from the left wall to the heatsink face of the amp's board, each on a web down to the floor
// so it prints without support
module amp_standoffs() {
    xf = AMP_X - AMP_PCB / 2;                    // heatsink-side face of the amp's board
    z = AMP_Z0 + AMP_HOLE_UP;
    for (y = [AMP_Y1 - AMP_HOLE_IN]) difference() {     // IN end only: at the OUT end the post would block the pigtail
        union() {
            hull() {                             // teardrop: flat-topped point underneath
                translate([x0 - 0.01, H - y, z]) rotate([0, 90, 0]) cylinder(r = AMP_ST_R, h = xf - x0 + 0.01);
                translate([x0 - 0.01, H - y - 0.01, z - AMP_ST_R * 1.41]) cube([xf - x0 + 0.01, 0.02, 0.01]);
            }
            bx(x0 - 0.01, xf - 2.5, y - 0.8, y + 0.8, FLOOR_Z - 0.01, z);              // web, short of the board
            hull() {                             // 45 degree gusset under the free end, so nothing overhangs
                bx(xf - 2.51, xf - 2.5, y - 0.8, y + 0.8, z - AMP_ST_R * 1.41 - 2.5, z);
                bx(xf - 0.01, xf, y - 0.8, y + 0.8, z - AMP_ST_R * 1.41, z);
            }
        }
        translate([x0 + 1, H - y, z]) rotate([0, 90, 0]) cylinder(d = AMP_PILOT_D, h = xf - x0);
    }
}

// floor rail for the buck: groove for its lower edge (wall on the parts side kept low), with a tunnel underneath
// for a cable tie
module buck_rail() {
    g = BUCK_PCB + 0.4;  za = FLOOR_Z - 0.01;  zt = FLOOR_Z + BUCK_RAIL_H;  ze = zt - BUCK_SEAT;
    ya = BUCK_YC - BUCK_RAIL_L / 2;  yb = BUCK_YC + BUCK_RAIL_L / 2;
    difference() {
        union() {
            bx(BUCK_PX - g / 2 - 1.5, BUCK_PX - g / 2, ya, yb, za, zt);                 // amp side, full height
            bx(BUCK_PX + g / 2, BUCK_PX + g / 2 + 1.5, ya, yb, za, zt - 1);             // parts side, lower
            bx(BUCK_PX - g / 2 - 0.01, BUCK_PX + g / 2 + 0.01, ya, yb, za, ze);         // groove floor
        }
        bx(BUCK_PX - g / 2 - 2, BUCK_PX + g / 2 + 2, BUCK_YC - TIE_W / 2, BUCK_YC + TIE_W / 2, za - 0.1, FLOOR_Z + TIE_H);
    }
}

module tie_bridge() {
    for (cx = TIEB_X) tie_bridge_at(cx);
}
module tie_bridge_at(cx) {
    xa = cx - TIEB_W / 2;  xb = cx + TIEB_W / 2;
    difference() {
        hull() {
            bx(xa, xb, y0 - 0.01, y0 + TIEB_D, TIEB_Z[0], TIEB_Z[1]);
            bx(xa, xb, y0 - 0.01, y0, TIEB_Z[0] - TIEB_D, TIEB_Z[1]);      // 45 degree underside
        }
        for (dx = [-TIEB_SLOT_DX, TIEB_SLOT_DX])
            bx(cx + dx - TIEB_SLOT_W / 2, cx + dx + TIEB_SLOT_W / 2, y0 - 0.02, y0 + TIEB_SLOT_T,
               TIEB_Z[0] - TIEB_D - 0.1, TIEB_Z[1] + 0.1);
    }
}

module tie_bridge_left(cy) {      // the same bridge on the left wall, slots running along y
    ya = cy - TIEB_W / 2;  yb = cy + TIEB_W / 2;
    difference() {
        hull() {
            bx(x0 - 0.01, x0 + TIEB_D, ya, yb, TIEB_Z[0], TIEB_Z[1]);
            bx(x0 - 0.01, x0, ya, yb, TIEB_Z[0] - TIEB_D, TIEB_Z[1]);
        }
        for (dy = [-TIEB_SLOT_DX, TIEB_SLOT_DX])
            bx(x0 - 0.02, x0 + TIEB_SLOT_T, cy + dy - TIEB_SLOT_W / 2, cy + dy + TIEB_SLOT_W / 2,
               TIEB_Z[0] - TIEB_D - 0.1, TIEB_Z[1] + 0.1);
    }
}

// Blanking plug for the antenna hole, for an internal antenna in a slim base printed with the SMA hole. It drops into
// the hex pocket like the bulkhead SMA, a slip fit, and the lid locks it: the hex stops it going out, the stem fills
// the hole flush with the outside, its tab stops 0.3 mm under the lid's far lip (can't lift out), and a finger on the
// slim lid comes down behind the tab (can't fall in). Take the lid off and lift it out to fit an SMA after all.
// Crush ribs on the stem make it a press fit in the hole as well, so it stays put with the lid off. They print
// upright (stem up), along the layers; pull it back in with pliers on the tab to remove it.
PLUG_CL = 0.15;                    // clearance per side in the pocket and the hole
PLUG_FINGER_Z = 12;                // bottom of the lid's finger: above the bulkhead SMA's hex (top about z 7.6)
PLUG_RIBS = 3;                     // crush ribs round the stem
PLUG_PRESS = 0.15;                 // rib interference in the hole, per side. Too stiff to push home: lower it; loose: raise it
PLUG_RIB_W = 0.8;                  // rib width
module ant_plug(ribs = true) {
    r = (HEX_AF - 2 * PLUG_CL) / sqrt(3);                 // hex corner radius
    ya = y0 + BOSS_T;                                     // inner face, flush with the boss face
    yb = y0 - (WALL - WALL_AT_SMA) + 0.1;                 // outer face of the hex, short of the pocket bottom
    zt = lz - 0.3 - r;                                    // tab top 0.3 under the lid's far lip
    hull() for (z = [SMA_Z, zt])
        translate([ANT_X, H - ya, z]) rotate([-90, 0, 0]) rotate([0, 0, 90]) cylinder(r = r, h = ya - yb, $fn = 6);
    translate([ANT_X, H - yb, SMA_Z]) rotate([-90, 0, 0]) {
        cylinder(r = SMA_R - PLUG_CL, h = yb - Y0 - 0.1);   // stem ends 0.1 inside the outer face
        if (ribs) for (i = [0 : PLUG_RIBS - 1]) rotate([0, 0, 90 + i * 360 / PLUG_RIBS]) hull() {
            // full height near the hex, ramping down over the last 0.6 mm as a lead-in
            translate([0, -PLUG_RIB_W / 2, -0.01]) cube([SMA_R + PLUG_PRESS, PLUG_RIB_W, yb - Y0 - 0.7]);
            translate([0, -PLUG_RIB_W / 2, -0.01]) cube([SMA_R - PLUG_CL, PLUG_RIB_W, yb - Y0 - 0.1]);
        }
    }
}
// The lid's finger behind the plug's tab (slim lid only: in the amp case the amp's board is there). It clears the
// bulkhead SMA and its pigtail, which run below PLUG_FINGER_Z, so the same lid serves every slim build.
module plug_finger() {
    ya = y0 + BOSS_T + 0.2;  yb = ya + 1.6;  xa = ANT_X - 2.5;  xb = ANT_X + 2.5;
    hull() {
        bx(xa, xb, ya, yb, PLUG_FINGER_Z + 1.6, LID_Z + 0.01);
        bx(xa, xb, ya, ya + 0.01, PLUG_FINGER_Z, LID_Z + 0.01);     // lead-in: the back face slopes at 45 degrees
    }
}

// ------------------------------------------------------------------ lid
module ridge(xface, dir, y) {      // chamfered snap ridge on a lip's outer face
    hull() {
        bx(xface, xface + dir * 0.01, y - SNAP_L / 2, y + SNAP_L / 2, lz + 0.2, lz + 1.8);
        bx(xface + dir * (SNAP - 0.01), xface + dir * SNAP, y - SNAP_L / 2, y + SNAP_L / 2, lz + 0.8, lz + 1.2);
    }
}
module ridge_y(yface, dir, x) {   // the same ridge on a lip that faces along y
    hull() {
        bx(x - SNAP_L / 2, x + SNAP_L / 2, yface, yface + dir * 0.01, lz + 0.2, lz + 1.8);
        bx(x - SNAP_L / 2, x + SNAP_L / 2, yface + dir * (SNAP - 0.01), yface + dir * SNAP, lz + 0.8, lz + 1.2);
    }
}
module oled_cradle() {
    // Two fixed rails, geometry of test piece G. The face that holds the board slopes at 45 degrees, so with the lid
    // printed outer face down nothing overhangs. The board slides in, ribbon end first, until it meets the end stops.
    g = FEN_G;  t = FEN_T;  e = FEN_END;  top = oz + WEDGE;  bot = top - RAIL_IN - RAIL_T;
    bx(ox0 - g, ox0 + FEN_A, oy1 + e, oy1 + e + t, oz - 0.4, LID_Z);          // end stops, ribbon end, corners only
    bx(ox1 - FEN_A, ox1 + g, oy1 + e, oy1 + e + t, oz - 0.4, LID_Z);
    for (k = [[ox0, -1], [ox1, 1]]) {
        xa = k[0] + k[1] * g;
        bx(xa, xa + k[1] * t, oy0 - 1.2, oy1 + e + t, bot, LID_Z);                            // rail wall
        hull() {                                                                              // ledge: sloped holding face
            bx(xa, xa + k[1] * 0.01, oy0 - 1.2, oy1 - 3, bot, top);
            bx(xa, xa - k[1] * RAIL_IN, oy0 - 1.2, oy1 - 3, bot, top - RAIL_IN);
        }
    }
}
// polarity marks for the screw terminal, engraved: + over pin 1 (x = 12.83), - over pin 2 (x = 17.83).
// Engraved rather than raised because the lid prints with its outer face on the bed.
MARK = 3;  MARK_W = 0.8;  MARK_D = 0.5;
TERM_X = [12.827, 17.827];  TERM_Y = 57.912;               // screw positions (J2)
MARK_Y = TERM_Y - 4.1;
module polarity_marks() {
    top = LID_Z + LID_T;
    for (m = [[TERM_X[0], true], [TERM_X[1], false]]) {
        x = m[0];
        // on the lid, beside each screwdriver hole
        bx(x - MARK / 2, x + MARK / 2, MARK_Y - MARK_W / 2, MARK_Y + MARK_W / 2, top - MARK_D, top + 0.1);
        if (m[1]) bx(x - MARK_W / 2, x + MARK_W / 2, MARK_Y - MARK / 2, MARK_Y + MARK / 2, top - MARK_D, top + 0.1);
        // on the front of the apron, above each wire entry
        bx(x - MARK / 2, x + MARK / 2, Y1 - MARK_D, Y1 + 0.1, 16 - MARK_W / 2, 16 + MARK_W / 2);
        if (m[1]) bx(x - MARK_W / 2, x + MARK_W / 2, Y1 - MARK_D, Y1 + 0.1, 16 - MARK / 2, 16 + MARK / 2);
    }
}
// Net2RF mark, engraved above the display: the name plus three radio arcs. Reads the same way up as the OLED.
LOGO = [ocx, WIN[2] - 8];  LOGO_SIZE = 6.5;  LOGO_D = 0.5;          // centre of the word (board x, y), cap height, depth
LOGO_STYLE = "E";                                          // "A" name then arcs, "E" arcs over the 2
module arc2d(r, w, a0, a1) {       // ring segment from angle a0 to a1 (degrees)
    intersection() {
        difference() { circle(r = r + w / 2); circle(r = r - w / 2); }
        polygon(concat([[0, 0]], [for (a = [a0 : 5 : a1]) [2 * r * cos(a), 2 * r * sin(a)]]));
    }
}
module logo2d() {
    f = "Arial:style=Bold";  s = LOGO_SIZE;
    if (LOGO_STYLE == "E") {
        translate([-0.44 * s, -s / 2]) text("Net", size = s, font = f, halign = "right");
        translate([0, -s / 2]) text("2", size = s, font = f, halign = "center");
        translate([0.44 * s, -s / 2]) text("RF", size = s, font = f, halign = "left");
        translate([0, s * 0.42]) { arc2d(s * 0.5, 0.9, 40, 140); arc2d(s * 0.85, 0.9, 40, 140); }
    } else {
        text("Net2RF", size = s, font = f, halign = "center", valign = "center");
        translate([s * 2.55, -s / 2]) { circle(r = 0.95); for (r = [2.6, 4.5, 6.4]) arc2d(r, 0.9, 0, 90); }
    }
}
module logo() { translate([LOGO[0], H - LOGO[1], LID_Z + LID_T - LOGO_D]) linear_extrude(LOGO_D + 0.1) logo2d(); }
module rev_mark_lid() {            // engraved in the underside; mirrored so it reads correctly from below
    translate([42, H - 22, LID_Z - 0.1]) mirror([1, 0, 0]) linear_extrude(0.6)
        text(REV, size = 6, font = "Arial:style=Bold", halign = "center", valign = "center");
}
module lid() { difference() { lid_solid(); polarity_marks(); logo(); rev_mark_lid(); } }
module lid_solid() {
    union() {
        difference() {
            hull() {
                rrect(X0, X1, Y0, Y1, LID_Z, LID_Z + LID_T - EDGE_CH, CORNER_R);
                rrect(X0 + EDGE_CH, X1 - EDGE_CH, Y0 + EDGE_CH, Y1 - EDGE_CH, LID_Z + LID_T - 0.01, LID_Z + LID_T, CORNER_R - EDGE_CH);
            }
            bx(WIN[0], WIN[1], WIN[2], WIN[3], LID_Z - 1, LID_Z + LID_T + 1);                 // display window
            hull() {                                                                          // ...with a chamfered outer edge
                bx(WIN[0], WIN[1], WIN[2], WIN[3], LID_Z + LID_T - WIN_CH, LID_Z + LID_T - WIN_CH + 0.01);
                bx(WIN[0] - WIN_CH, WIN[1] + WIN_CH, WIN[2] - WIN_CH, WIN[3] + WIN_CH, LID_Z + LID_T, LID_Z + LID_T + 0.1);
            }
            for (x = TERM_X) cyl(x, TERM_Y, LID_Z - 1, LID_Z + LID_T + 1, 2.0);          // screwdriver access
            bx(X0 - 0.1, X0 + 1.4, 28, 38, LID_Z - 0.1, LID_Z + 1.0);                          // pry notch
            if (AMP) for (y = [AMP_Y0 + 8 : 5 : AMP_Y1 - 8])                               // vents above the amp
                hull() { cyl(AMP_X - 6, y, LID_Z - 1, LID_Z + LID_T + 1, 1); cyl(AMP_X + 3, y, LID_Z - 1, LID_Z + LID_T + 1, 1); }
            // USER and EN (RST) buttons: three slits leave a springy tongue, thinned from below, running across the
            // lid with its tip at the right edge; the label is engraved under it
            for (k = [[BTN, "USER"], [ENB, "RST"]]) {
                ey0 = k[0][1] - TONGUE_W / 2;  ey1 = k[0][1] + TONGUE_W / 2;  er = BTN_TIP - TONGUE_L;
                bx(er, BTN_TIP + SLIT, ey0 - SLIT, ey0, LID_Z - 1, LID_Z + LID_T + 1);
                bx(er, BTN_TIP + SLIT, ey1, ey1 + SLIT, LID_Z - 1, LID_Z + LID_T + 1);
                bx(BTN_TIP, BTN_TIP + SLIT, ey0 - SLIT, ey1 + SLIT, LID_Z - 1, LID_Z + LID_T + 1);
                hull() {                 // thinning from below, ramped in from the root
                    bx(er + RAMP, BTN_TIP, ey0, ey1, LID_Z - 0.1, LID_Z + LID_T - TONGUE_T);
                    bx(er, BTN_TIP, ey0, ey1, LID_Z - 0.1, LID_Z - 0.05);
                }
                for (y = [ey0 - SLIT / 2, ey1 + SLIT / 2]) cyl(er, y, LID_Z - 1, LID_Z + LID_T + 1, 0.8);   // round the slit ends
                // label engraved on the tongue itself, shallow so the spring barely changes, between the root and the post
                translate([(er + 1 + k[0][0] - FLARE_R) / 2, H - k[0][1], LID_Z + LID_T - LABEL_D])
                    linear_extrude(LABEL_D + 0.1) text(k[1], size = LABEL_SIZE, font = "Arial:style=Bold", halign = "center", valign = "center");
            }
        }
        for (b = [BTN, ENB]) {
            cyl(b[0], b[1], BTN_TOP + POST_GAP, LID_Z + LID_T - TONGUE_T + 0.01, POST_R);       // post down to the switch
            translate([b[0], H - b[1], LID_Z + LID_T - TONGUE_T - FLARE_H])                    // flared root
                cylinder(r1 = POST_R, r2 = FLARE_R, h = FLARE_H + 0.01);
        }
        // locating lip (none on the connector side: the apron does that job)
        bx(x0 + CLR, x0 + CLR + LIP_T, y0 + CORNER_R, y1 - CORNER_R, lz, LID_Z);          // lips stop short of the rounded corners
        bx(x1 - CLR - LIP_T, x1 - CLR, y0 + CORNER_R, y1 - CORNER_R, lz, LID_Z);
        bx(x0 + CORNER_R, x1 - CORNER_R, y0 + CLR, y0 + CLR + LIP_T, lz, LID_Z);
        for (y = SNAP_Y) { ridge(x0 + CLR, -1, y); ridge(x1 - CLR, 1, y); }
        for (x = SNAP_X) ridge_y(y0 + CLR, -1, x);
        if (AMP) {
            bx(x0 + CORNER_R, -1.5, y1 - CLR - LIP_T, y1 - CLR, lz, LID_Z);             // front-left lip, inside the base wall
            ridge_y(y1 - CLR, 1, SNAP_FRONT_X);
        }
        // apron: the upper part of the connector wall, open downwards round each connector
        difference() {
            bx(-0.3 + CLR, W + 0.3 - CLR, y1, Y1, SLOT[1] + CLR, LID_Z);
            bx(RJ[0], RJ[1], y1 - 1, Y1 + 1, SLOT[1] - 1, RJ[3]);
            bx(DC[0] - 0.01, DC[1], y1 - 1, Y1 + 1, SLOT[1] - 1, DC[2]);
            bx(ST[0], ST[1], y1 - 1, Y1 + 1, SLOT[1] - 1, ST[2]);
            // pockets over the heads of the two front board screws (either side of the RJ45); the front skin of the
            // apron stays full height so the board edge is still hidden
            for (h = HOLES) if (h[1] > H - 10)
                bx(h[0] - HEAD_R, h[0] + HEAD_R, y1 - 1, H + 0.3, SLOT[1] - 1, PCB_T + HEAD_H);
        }
        oled_cradle();
        if (!AMP) plug_finger();
    }
}

// ------------------------------------------------------------------ mounting bracket
// A separate plate the closed case clips into, for mounting inside a weatherproof enclosure. Screw the plate to
// the enclosure's backplate first (four countersunk M4 / #8 holes under the case), then push the case straight
// down: the hooks' sloped tops spread the arms and they snap over the lid's top edge, which also holds the lid on.
// Pull one arm outwards to release. Arms sit clear of the left-wall vents and the pry notch; low lips at the front
// and back stop the case sliding lengthways and stay below the connector openings and the antenna.
BR_T = 3;  BR_CL = 0.3;            // plate thickness; clearance round the case
BR_ARM_T = 2.2;  BR_ARM_W = 12;    // snap arm thickness and width
BR_HOOK = 1.8;  BR_HOOK_T = 1.6;   // hook reach past the case side (the lid edge has a 0.6 chamfer, so ~1.2 bites); holding part thickness
BR_ARMS_L = [50];  BR_ARMS_R = [0, 46];      // arm centres along y (board coordinates), left and right side
BR_LIP_H = 3;  BR_LIP_T = 2;       // front and back stop lips
BR_SCREW_D = 4.5;  BR_CSK_D = 8.8; // M4 clearance, countersink diameter at the plate's top face
module bracket() {
    zb = FLOOR_Z - FLOOR_T;  zt = LID_Z + LID_T;        // case bottom and top
    xa = X0 - BR_CL;  xb = X1 + BR_CL;  ya = Y0 - BR_CL;  yb = Y1 + BR_CL;
    holes = [[X0 + 10, Y0 + 10], [X1 - 10, Y0 + 10], [X0 + 10, Y1 - 10], [X1 - 10, Y1 - 10]];
    difference() {
        union() {
            rrect(xa - BR_ARM_T, xb + BR_ARM_T, ya - BR_LIP_T, yb + BR_LIP_T, zb - BR_T, zb, CORNER_R);   // plate
            bx(X0 + 6, X1 - 6, yb, yb + BR_LIP_T, zb - 0.01, zb + BR_LIP_H);                            // front lip
            bx(X0 + 6, X1 - 6, ya - BR_LIP_T, ya, zb - 0.01, zb + BR_LIP_H);                            // back lip
            for (k = [[BR_ARMS_L, xa, -1], [BR_ARMS_R, xb, 1]]) for (y = k[0]) {
                xo = k[1] + k[2] * BR_ARM_T;                                                            // outer face
                bx(k[1], xo, y - BR_ARM_W / 2, y + BR_ARM_W / 2, zb - 0.01, zt + 0.2 + BR_HOOK_T);         // arm
                hull() {                                                                                // hook, sloped top
                    bx(xo, k[1] - k[2] * (BR_HOOK + BR_CL), y - BR_ARM_W / 2, y + BR_ARM_W / 2, zt + 0.2, zt + 0.2 + 0.4);
                    bx(xo, k[1] - k[2] * 0.01, y - BR_ARM_W / 2, y + BR_ARM_W / 2, zt + 0.2, zt + 0.2 + BR_HOOK_T + BR_HOOK);
                }
            }
        }
        for (h = holes) {
            cyl(h[0], h[1], zb - BR_T - 1, zb + 1, BR_SCREW_D / 2);
            translate([h[0], H - h[1], zb - (BR_CSK_D - BR_SCREW_D) / 2]) cylinder(d1 = BR_SCREW_D, d2 = BR_CSK_D, h = (BR_CSK_D - BR_SCREW_D) / 2 + 0.01);
        }
    }
}

if (part == "base") base();
else if (part == "lid") translate([0, H, LID_Z + LID_T]) rotate([180, 0, 0]) lid();
else if (part == "logo2d") logo2d();               // the mark on its own, for SVG export
else if (part == "lid_asm") lid();                 // lid in its assembled position, for the web preview
else if (part == "closed") { base(); lid(); }      // assembled, lid on
else if (part == "bracket") translate([0, 0, BR_T - FLOOR_Z + FLOOR_T]) bracket();    // print orientation: plate on the bed
else if (part == "bracket_asm") { bracket(); base(); lid(); }                         // the case clipped into the bracket
else if (part == "head_clash")                     // M3 heads up to 7 mm across and 3.2 mm tall on the front screws vs the lid: should be empty
    intersection() { lid(); for (h = HOLES) if (h[1] > H - 10) cyl(h[0], h[1], PCB_T, PCB_T + 3.2, 3.5); }
else if (part == "plug")                           // print orientation: inner face on the bed, stem up
    translate([0, 0, 0]) rotate([90, 0, 0]) translate([-ANT_X, -(H - y0 - BOSS_T), -SMA_Z]) ant_plug();
else if (part == "plug_asm") { base(); color("Tomato") ant_plug(); color("DarkSeaGreen") lid(); }
else if (part == "plug_clash") intersection() { ant_plug(ribs = false); union() { lid(); base(); } }   // should be empty (the ribs overlap the hole on purpose)
else if (part == "rib_bite") intersection() { ant_plug(); base(); }                // just the ribs' interference, PLUG_PRESS deep
else if (part == "finger_clash") intersection() { plug_finger(); base(); }                 // should be empty
else if (part == "bracket_clash") intersection() { bracket(); translate([0, 0, 0.01]) union() { base(); lid(); } }   // should be empty
else if (part == "amp_test")                       // the amp bay corner of the base, cut off above the posts: a quick fit test
    intersection() { base(); bx(X0 - 1, -3, Y0 - 1, 59, FLOOR_Z - FLOOR_T - 1, AMP_TEST_Z); }
else if (part == "oled_test")                      // just the OLED pocket and latch, print orientation: a quick fit test
    translate([0, H, LID_Z + LID_T]) rotate([180, 0, 0])
        intersection() { lid(); bx(ox0 - 4, ox1 + 4, oy0 - 4, oy1 + 4, 0, LID_Z + LID_T + 1); }
else { color("SlateGray") base(); color("DarkSeaGreen", 0.85) translate([0, 0, 14]) lid(); }
