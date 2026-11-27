// =====================================================================
//  Enclosure for the LilyGo T-2CAN FD (ESP32-S3-WROOM-1U) + microSD retrofit
//  In-car install (Tesla Model Y, behind trim, S3XY harness on the
//  screw terminals).  Two parts: base + lid.
//
//  Coordinates: board coordinates in top view (component side up)
//    X = length, X=0 at the edge with USB-C / QWIIC / the 12-24V terminal,
//        X=89.87 at the edge with the CAN screw terminals
//    Y = width, Y=0 at the edge with the 12-24V terminal,
//        Y=38.53 at the edge with the BOOT/RST side buttons and USB-C
//    Z = 0 is the underside of the enclosure floor.
//
//  Dimension tags:
//    [DXF]   = from LilyGo mechanical data T-2Can_Top/Bot_AutoCAD2007
//              (github.com/Xinyuan-LilyGO/T-2CAN/structure, layout of the
//              non-FD version; FD photos show the same layout/outline)
//    [DS]    = standard datasheet dimension of the part
//    [PHOTO]  = measured from user photos with a ruler (perspective-rectified,
//              homography over THT solder joints/U8 pads, accuracy about ±0.5 mm)
//    [CALIPER]  = measured with calipers (user, 13 Nov 2026)
//    [EST]= estimated -> RE-MEASURE BEFORE PRINTING!
//  As of v2 (13 Nov 2026): pillars/hold-downs repositioned from photos,
//    part heights from calipers, taller lid (12-24V terminal 13.5 mm).
//
//  Print: PETG (up to ~75 °C) or, better, ASA/ABS (up to ~90-95 °C).
//         NO PLA (warps above ~55 °C in a car).
//  Base: floor on the print bed.  Lid: top face on the print bed.
//  Both parts print without supports.
//
//  Hardware:
//    4x M3x10 socket-head cap screws (ISO 4762) + 4x M3 heat-set inserts
//       (OD ~4.0-4.2, length 4-5.7)  -- alternatively insert_d=2.5 and self-tapping M3
//    U.FL(IPEX1)->SMA jack pigtail ~10 cm + 2.4 GHz whip antenna (optional)
//    Cable ties <= 4.8 mm wide (cable saddles at both ends)
//    3M Dual Lock / hook-and-loop or M4 in the mounting ears
//    SD module: locating pins (Ø 2.25 × 3.2 mm, through the M2.5 holes) plus hot glue/tape.
//  v5 (27 Nov 2026): centre lid hold-down pins removed; perimeter edge ribs on the
//    bare PCB edges instead.  SD locating pins go through the board (3.2 mm, Ø 2.25).
//  v6 (27 Nov 2026): all identifiers translated to English; hinged SD door removed.
//    The Y=0 base ledge resumes right after the SD pocket (sd_x1+tol+2).
// =====================================================================

/* [View] */
// What should be rendered?
part = "assembled"; // [assembled, exploded, section, antenna, base, lid, build_plate]
// Show the board placeholder with components (preview only)
show_board = true;
// Lid offset in the exploded view
explode_offset = 28;
// Section plane (Y) for part="section"
section_y = 8;

/* [T-2CAN FD board] */
pcb_l   = 89.87;  // [DXF], [PHOTO] confirmed (±0.5) length (X)
pcb_w   = 38.53;  // [DXF], [PHOTO] confirmed (±0.5) width (Y)
pcb_t   = 1.6;    // [EST] PCB thickness (standard 1.6, re-measure)
// Part heights above the TOP of the PCB  [CALIPER] (caliper reading minus pcb_t)
h_pwr_terminal = 13.53; // 12-24V terminal, 2-pin: 15.13 including PCB
h_capacitor    = 10.29; // 330µF/50V electrolytic: 11.89 including PCB
h_can_terminal = 8.53;  // CAN-A/B terminals, 4-pin: 10.13 including PCB
h_transceiver   = 7.69;  // TD501MCANFD (isolated transceiver): 9.29 including PCB
// THT pins under the board: 18.07 (terminal+PCB+pins) - 15.13 = 2.94  [CALIPER]
pin_below    = 2.94;
// Tallest part above the top of the PCB (= 12-24V terminal)
component_max_h = max(h_pwr_terminal, h_capacitor, h_can_terminal, h_transceiver);

/* [Enclosure, general] */
wall      = 2.4;   // Wall thickness
floor_t     = 2.0;   // Base floor thickness
lid_t     = 2.0;   // Lid thickness
tol       = 0.4;   // Clearance board <-> inner wall (per side)
fit_gap   = 0.2;   // Clearance between lid and base (rebate)
// Clearance under the board (wires to U8 pads, SD module, THT pins 2.94 [CALIPER])
gap_below = 6.0;   // >= pin_below + 3 mm clearance
// Clearance above parts up to the inside of the lid
clearance_above = 1.0;   // above the 12-24V terminal (tallest part)
// Base wall extends this far above the top of the PCB (snap lugs)
seam_above = 1.0;
// Rebate (lid skirt on the outside)
rebate_w = 1.0;
rebate_h = 2.0;
corner_r  = 2.5;      // Outer corner radius of the enclosure

/* [Lid screws, M3] */
boss_r       = 4.0;   // Radius of the corner screw bosses
boss_offset  = 3.0;   // Boss centre diagonally outside the inner corner
// Hole in the base: 4.0 for an M3 heat-set insert (short), 2.5 for self-tapping M3
insert_d    = 4.0;
insert_depth = 6.0;
screw_d      = 3.4;   // Through-hole in the lid
head_d      = 6.2;   // Counterbore (M3 socket head)
head_stock  = 4.0;   // Material under the screw head in the lid -> M3x10

/* [Board supports] */
ledge_w   = 1.4;    // Ledges on the walls (under the board edge)
pillar_d    = 4.0;    // Support pillars under the board
// [PHOTO] all on clear bottom area, >= 4.5 mm (clearance >= 2.5 mm) from every THT solder joint,
// outside the U8 pad field (X 7-39, Y 26-36) and the SD pocket.
pillars    = [[42,21],[61,20.1],[69,20.1],[79,20.0]];  // old v1: [[44,20],[66,19.5],[78,19.5]]
snap_lugs    = true; // Snap lugs retain the board in the base
lug_x     = [[56,66]];
lug_overlap = 0.5;    // Snap-lug overlap over the board edge (in addition to tol)
lug_clearance = 1.0;    // Lid edge ribs keep this much X-clearance to the snap lugs
// v5: the two centre lid pins (old hold-down pins at [[61,20.1],[69,20.1]], on the
// LILYGO logo) are gone — they were long (~14 mm) and easy to miss the narrow
// bare strip.  The lid now has short perimeter edge ribs that press only on the
// bare outer PCB edges, directly above the base ledges (no components).
// ~1.1 mm inward overlap (ledge_w = 1.4), 0.25 mm Z-gap, 0.5 mm tip chamfer.
// Printed with the lid upside-down (top face on the bed): the chamfer is at the
// print-top, so the ribs stay 100 % support-free.
rib_overlap = 1.1;    // inward overlap over the PCB edge
rib_gap      = 0.25;   // Z gap above z_top_pcb
rib_chamfer  = 0.5;    // tip chamfer (narrows towards the PCB)
// Wall Y=0 (12-24V / CAN-A): ledge resumes at sd_x1+tol+2 (just after the
// SD pocket); CAN-A transformer inset to Y=2.6; snap lugs X=56..66;
// CAN-A terminal at X=82.
rib_y0   = [[47.0, 55.0], [67.0, 80.0]];
// Wall Y=max (USB / BOOT / ESP32 / CAN-B): no ribs under USB, BOOT/RST or the
// ESP32 module (X=22.4..40.4, Y up to 38.6), and none along the antenna coax
// (placeholder runs inside the wall from X≈39.5-43.5, Y≈38.1-38.7, Z≈12.4-14.6
// on its way to the U.FL).  The lid wall is cut away at the antenna tower
// (X=39..82); these ribs hang from the roof only and stay inward of
// cy1-fit_gap so they miss the tower.  CAN-B transformer inset to Y=36.6;
// snap lugs X=56..66; CAN-B terminal at X=82.
rib_y1   = [[46.0, 55.0], [67.0, 80.0]];

/* [Included FPC antenna (2.4 GHz, adhesive)] */
// Antenna sticks to the INSIDE of the base long wall at Y=max (wall nearest the U.FL),
// laid flat (length along X). The base has an "antenna tower" there that reaches
// almost to the roof; the lid is cut away at that spot. This keeps the
// U.FL connection mated when the lid is opened (U.FL is only good for ~30 cycles).
antenna      = true;
ant_l        = 37.0;  // [PHOTO] FPC length (±1)
ant_w        = 9.2;   // [PHOTO] FPC width (±0.5)
ant_t        = 0.2;   // [EST] Thickness including adhesive
ant_margin   = 1.0;   // Clearance per side in the pocket
ant_pocket_d = 0.6;   // Pocket depth into the wall (wall remains wall-0.6)
ant_x_cable  = 41.0;  // X of the pocket edge at the cable end (U.FL at X 37.3)
ant_z_above  = 2.5;   // Pocket lower edge above the top of the PCB
ant_tower_margin = 2.0;  // Tower extends this far past the pocket (X)
ant_cable_d  = 1.13;  // [EST] Coax Ø (typ. 1.13 mm)
ufl_pos      = [37.3, 35.7]; // [PHOTO]

/* [microSD module: Adafruit 4682, 3.3 V only] */
// Official (Eagle/CAD/fab print, adafruit.com/product/4682):
//   PCB 25.40 × 22.86 mm (1.00" × 0.90"), corner radius 2.54, overall height 3.45 mm
//   (board 1.57 mm + push-push socket ≈ 1.88 mm), product listing 3.5 mm.
//   An inserted microSD protrudes 2.54 mm (0.10") past the slot edge.
//   M2.5 holes (official Ø 2.5, pad Ø 3.2) at (2.54, 20.32) and (22.86, 20.32),
//   Board coordinates: Y=0 header edge, Y=22.86 slot edge, X=0..25.40.
//   User measurement 27 Nov 2026: hole Ø 2.6 mm, outer-to-outer 22.9 mm
//   → centre-to-centre 20.3 mm (matches 22.86 − 2.54 = 20.32).
//   Socket 14.2 × ~15 mm, centre X≈12.61 (0.09 mm off the board centre 12.70).
// User measurement 26 Nov 2026: 25.40 × 23.41 mm (slot edge including any socket lip).
// The pocket uses the measured outer size; hole positions are relative to the header edge.
sd_x0      = 12.0;   // left edge of the module (X); short wires to U8 pads 1/2 and 13–16
sd_l       = 25.40;  // [CALIPER] width along the 9-pin header (= official 25.40)
sd_w       = 23.41;  // [CALIPER] header edge → slot edge (official 22.86)
sd_pcb_t   = 1.6;    // [DS/CAD] 1.57 in the Adafruit STL, standard 1.6
sd_socket_h = 1.9;   // [DS/CAD] 3.45 − 1.57 ≈ 1.88 mm above the module PCB
sd_card_protrusion = 2.54; // [DS] card protrusion past the slot edge (inserted)
sd_inner_gap  = 0.3;  // Clearance between the slot edge and the inner wall (Y=cy0)
sd_slot_w  = 14.0;   // Card-slot width (card 11, socket mouth ≈ 14.2)
sd_slot_h  = 2.8;    // Card-slot height
sd_pins = true;   // Locating pins into the M2.5 mounting holes
sd_pin_d = 2.25; // [CALIPER] hole Ø 2.6 (official 2.5); snug for PETG/ASA
sd_pin_h = 3.2;  // sd_pcb_t + 1.6: goes all the way through the 1.6 mm PCB
sd_pin_chamfer = 0.5;   // lead-in cone at the tip
sd_pin_chamfer_d2 = 1.4;
sd_hole_x  = [2.54, 22.86]; // [DS] from the left board edge
sd_hole_from_header = 20.32; // [DS] from the header edge (Y=0 officially)

/* [Cutouts] */
usb_y = 33.15;  // [DXF] USB-C centre (shield tabs y 28.82/37.47)
usb_h = 3.26;   // [DS] USB-C jack height
usb_win_w = 12.5; // Cutout for the overmolded plug
usb_win_h = 7.0;
qwiic_open = true;
qwiic_y = [15.77, 23.62]; // [DXF] QWIIC CNC1 / CNC2 centres
pwr_y   = [0.8, 10.9];    // [DXF] 12-24V terminal (pins y 3.56 / 8.64, 5.08 pitch)
can_a_y = [1.3, 18.3];    // [DXF] CAN-A terminal (pins y 3.68..15.11, 3.81 pitch)
can_b_y = [20.1, 37.1];   // [DXF] CAN-B terminal (pins y 23.37..34.80)
can_win_h = 7.0;        // Height of the CAN-terminal cable windows above the board (terminal 8.53 [CALIPER])
pwr_win_h   = 10.0;       // Height of the 12-24V terminal cable window (terminal 13.53 [CALIPER])
button_x = [9.82, 15.28]; // [DXF] BOOT/RST side buttons (wall Y=max)
sma_bore = false;      // optional SMA bulkhead jack (U.FL->SMA pigtail + whip antenna); v2: true
sma_x   = 70;             // SMA position on the wall at Y=max
sma_z_above = 7.5;        // SMA centre above the top of the PCB (v1: 6.5; more clearance above CAN-B SMD parts)
sma_d   = 6.6;            // SMA bulkhead: 6.5 mm (1/4"-36)
labels = true;

/* [In-car mounting] */
ears      = true;
ear_x      = [50, 80];   // X positions of the mounting ears (both long sides)
ear_l      = 15;         // Projection
ear_w      = 12;
ear_t      = 3;
ear_slot = [4.5, 5];  // Slot for M4 / cable ties
cable_saddles = true;       // Cable saddles with a cable-tie tunnel at both ends
saddle_l      = 13;
saddle_h   = 10.0;       // Top of the saddle (absolute z)

$fn = 48;

// ------------------------- derived dimensions -------------------------
z_pcb   = floor_t + gap_below;     // underside of the PCB
z_top_pcb = z_pcb + pcb_t;         // top of the PCB
z_seam  = z_top_pcb + seam_above;  // base/lid parting plane (inside)
space_above = component_max_h + clearance_above;
z_roof  = z_top_pcb + space_above;   // inside of the lid
z_total   = z_roof + lid_t;       // overall height

cx0 = -tol; cx1 = pcb_l + tol;
cy0 = -tol; cy1 = pcb_w + tol;
ox0 = cx0 - wall; ox1 = cx1 + wall;
oy0 = cy0 - wall; oy1 = cy1 + wall;

bosses = [[cx0-boss_offset, cy0-boss_offset], [cx1+boss_offset, cy0-boss_offset],
        [cx0-boss_offset, cy1+boss_offset], [cx1+boss_offset, cy1+boss_offset]];

sd_y0 = cy0 + sd_inner_gap;        // slot edge (header faces inward, towards U8)
sd_x1 = sd_x0 + sd_l;
sd_slot_cx = sd_x0 + sd_l/2;        // slot centre (socket is essentially centred)
sd_slot_z0 = floor_t + sd_pcb_t - 0.4;
sd_slot_z1 = floor_t + sd_pcb_t + sd_slot_h;
ant_tx0 = ant_x_cable - ant_tower_margin;                 // tower X range
ant_tx1 = ant_x_cable + ant_l + 2*ant_margin + ant_tower_margin;
ant_z0  = z_top_pcb + ant_z_above;                       // pocket lower edge
ant_z1  = ant_z0 + ant_w + 2*ant_margin;                   // pocket upper edge
assert(!antenna || ant_z1 < z_roof - fit_gap - 0.4, "Antenna pocket is too tall for the interior");

echo(str("Outer body size: ", ox1-ox0, " x ", oy1-oy0, " x ", z_total, " mm"));
echo(str("Including bosses: ", (cx1+boss_offset+boss_r)-(cx0-boss_offset-boss_r), " x ",
         (cy1+boss_offset+boss_r)-(cy0-boss_offset-boss_r)));
echo(str("SD pocket: ", sd_l, " x ", sd_w, " at X=", sd_x0, "..", sd_x1,
         " Y=", sd_y0, "..", sd_y0+sd_w));
echo(str("Pillar (42,21) clearance to SD edge X: ", 42 - pillar_d/2 - sd_x1, " mm"));
echo(str("Inserted card tip Y=", sd_y0 - sd_card_protrusion,
         "  outer wall Y=", oy0));
echo(str("Clearance, socket to T-2CAN underside: ",
         z_pcb - (floor_t + sd_pcb_t + sd_socket_h), " mm"));
echo(str("Clearance, header/wires to T-2CAN underside: ",
         z_pcb - (floor_t + sd_pcb_t), " mm"));
assert(sd_x1 + 0.2 <= 42 - pillar_d/2,
       "SD module collides with support pillar (42,21)");
assert(sd_y0 - sd_card_protrusion >= oy0 - 0.05,
       "inserted card protrudes past the outer wall");
assert(floor_t + sd_pcb_t + sd_socket_h + 1.0 <= z_pcb,
       "SD module is too tall for gap_below");
assert(floor_t + sd_pin_h + 1.0 <= z_pcb,
       "SD locating pins are too tall for clearance under the T-2CAN PCB");
echo(str("SD locating pins: Ø ", sd_pin_d, " x ", sd_pin_h,
         " mm; clearance under T-2CAN PCB: ",
         z_pcb - (floor_t + sd_pin_h), " mm"));

// Lid edge ribs: stay on the ledges, miss snap lugs, terminals, ESP32, transformers
assert(rib_y0[0][0] >= sd_x1 + tol + 2 - 0.05,
       "Y=0 rib A starts before the base ledge (sd_x1+tol+2)");
assert(rib_y0[0][1] <= lug_x[0][0] - lug_clearance,
       "Y=0 rib A collides with the snap lugs");
assert(rib_y0[1][0] >= lug_x[0][1] + lug_clearance,
       "Y=0 rib B collides with the snap lugs");
assert(rib_y0[1][1] <= 82.0 - 1.5,
       "Y=0 rib B is too close to the CAN-A terminal (X=82)");
assert(rib_overlap + 0.5 <= 2.6,
       "Y=0 rib overlap would hit the CAN-A transformer (Y>=2.6)");
assert(rib_y1[0][0] >= 45.0,
       "Y=max rib A is too close to the antenna coax / ESP32 (coax to X≈43.5)");
assert(rib_y1[0][1] <= lug_x[0][0] - lug_clearance,
       "Y=max rib A collides with the snap lugs");
assert(rib_y1[1][0] >= lug_x[0][1] + lug_clearance,
       "Y=max rib B collides with the snap lugs");
assert(rib_y1[1][1] <= 82.0 - 1.5,
       "Y=max rib B is too close to the CAN-B terminal (X=82)");
assert(pcb_w - rib_overlap >= 36.6 + 0.4,
       "Y=max rib overlap would hit the CAN-B transformer (Y<=36.6)");
echo(str("Lid edge ribs Y=0 X=", rib_y0, "  Y=max X=", rib_y1,
         "  overlap=", rib_overlap, " mm  Z-gap=", rib_gap, " mm"));

// ------------------------- helpers -------------------------
module rrect(x0,y0,x1,y1,r) {
    translate([x0+r,y0+r]) offset(r=r) square([x1-x0-2*r, y1-y0-2*r]);
}
module body2d()  { rrect(ox0,oy0,ox1,oy1,corner_r); }
module bosses2d(extra=0) { for (d=bosses) translate(d) circle(r=boss_r+extra); }
// Teardrop hole (horizontal, support-free) along Y
// tip=+1: tip points towards +Z (for parts printed upright),
// tip=-1: towards -Z (the lid is printed upside down)
module teardrop_y(d, l, tip=1) {
    rotate([-90,0,0]) linear_extrude(l, center=true)
        hull() { circle(d=d); translate([0,-tip*d/2*1.3]) square(0.01,center=true); }
}

// rounded rectangular cutout in YZ (for a wall at X)
module window_x(yc, zc, b, h, r, x0, x1) {
    translate([x0, yc, zc]) rotate([0,90,0])
        linear_extrude(x1-x0) offset(r=r) square([h-2*r, b-2*r], center=true);
}

// ------------------------- cutouts -------------------------
// lid=true: also open the windows down through the lid skirt
module cutouts(lid=false) {
    zs = lid ? z_seam - rebate_h - 1 : z_top_pcb;
    // USB-C (wall X=0)
    window_x(usb_y, z_top_pcb + usb_h/2, usb_win_w, usb_win_h, 2, ox0-6, cx0+0.5);
    // QWIIC CNC1/CNC2
    if (qwiic_open) for (y=qwiic_y)
        translate([ox0-6, y-3.75, (lid?zs:z_top_pcb-0.4)]) cube([cx0+0.5-(ox0-6), 7.5, z_top_pcb+4.0-(lid?zs:z_top_pcb-0.4)]);
    // 12-24V terminal
    translate([ox0-6, pwr_y[0], zs]) cube([cx0+0.5-(ox0-6), pwr_y[1]-pwr_y[0], z_top_pcb+pwr_win_h-zs]);
    // CAN-A / CAN-B terminals (wall X=max)
    for (r=[can_a_y, can_b_y])
        translate([cx1-0.5, r[0], zs]) cube([ox1+6-(cx1-0.5), r[1]-r[0], z_top_pcb+can_win_h-zs]);
    // BOOT/RST side buttons: slot for a pin/paperclip
    for (x=button_x)
        translate([x-1.4, cy1-0.5, z_top_pcb-0.3]) cube([2.8, wall+1, 3.3]);
    // SMA bulkhead jack
    if (sma_bore) translate([sma_x, (cy1+oy1)/2, z_top_pcb+sma_z_above]) teardrop_y(sma_d, wall+2, -1);
    // microSD card slot (wall Y=0): the card only, not the full module width
    translate([sd_slot_cx - sd_slot_w/2, oy0-1, sd_slot_z0])
        cube([sd_slot_w, wall+2, sd_slot_z1 - sd_slot_z0]);
}

// ------------------------- base -------------------------
module base() {
    difference() {
        union() {
            // Wall up to under the rebate (solid) + inner portion up to the parting plane
            linear_extrude(z_seam - rebate_h) body2d();
            linear_extrude(z_seam) offset(r=-rebate_w) body2d();
            // Corner bosses up to the parting plane
            linear_extrude(z_seam) bosses2d();
            if (ears) ears();
            if (cable_saddles) tie_saddles();
            if (antenna) antenna_tower();
        }
        // Interior
        translate([cx0, cy0, floor_t]) cube([cx1-cx0, cy1-cy0, 100]);
        // Heat-set inserts
        for (d=bosses) translate([d[0], d[1], z_seam-insert_depth]) cylinder(d=insert_d, h=insert_depth+1);
        cutouts(false);
        if (antenna) antenna_pocket();
    }
    // --- interior features ---
    intersection() {
        translate([cx0, cy0, 0]) cube([cx1-cx0, cy1-cy0, 100]);
        union() {
            // Ledges under the board edge
            // Wall Y=0: leave the SD module pocket clear
            ledge([cx0, 0], [sd_x0-tol-1, 0]);
            ledge([sd_x1 + tol + 2, 0], [cx1, 0]);
            // Wall Y=max: leave the USB tabs and button THT (x<19) clear
            ledge([19, 1], [cx1, 1]);
            // End walls
            translate([cx0, 13, floor_t]) cube([ledge_w+tol, 13, z_pcb-floor_t]);           // X=0 between the terminal and USB
            translate([cx1-ledge_w-tol, 17.6, floor_t]) cube([ledge_w+tol, 3.2, z_pcb-floor_t]); // X=max between the terminals
            // Support pillars
            for (p=pillars) translate([p[0], p[1], floor_t]) cylinder(d=pillar_d, h=z_pcb-floor_t);
            // SD module guide (side rails + rear stops at the corners)
            sd_guide();
            sd_locating_pins();
            // Snap lugs
            if (snap_lugs) for (r=lug_x) for (s=[0,1]) snap_lug(r, s);
        }
    }
}

module ledge(a, b) {   // a,b: [x, side]  side 0 -> wall Y=0, 1 -> wall Y=max
    y = (a[1]==0) ? cy0 : cy1 - ledge_w - tol;
    translate([a[0], y, floor_t]) cube([b[0]-a[0], ledge_w+tol, z_pcb-floor_t]);
}

module sd_guide() {
    h = floor_t + sd_pcb_t + 1.0;
    // Side rails outside the board
    for (x=[sd_x0-tol-1.6, sd_x1+tol])
        translate([x, cy0, floor_t-0.01]) cube([1.6, sd_y0+sd_w-cy0, h-floor_t]);
    // Rear stops at the corners (centre left open for the 9-pin header / wires to U8)
    for (x=[sd_x0-tol-1.6, sd_x1-2])
        translate([x, sd_y0+sd_w+tol, floor_t-0.01]) cube([3.6, 1.6, h-floor_t]);
}

module sd_locating_pins() {
    // Ø 2.25 shaft through the 2.6 mm holes, 3.2 mm tall, 0.5 mm lead-in cone
    if (sd_pins)
        for (lx = sd_hole_x)
            translate([sd_x0 + lx, sd_y0 + sd_w - sd_hole_from_header, floor_t]) {
                cylinder(d=sd_pin_d, h=sd_pin_h - sd_pin_chamfer + 0.01);
                translate([0, 0, sd_pin_h - sd_pin_chamfer])
                    cylinder(d1=sd_pin_d, d2=sd_pin_chamfer_d2, h=sd_pin_chamfer);
            }
}

module snap_lug(r, side) {
    // Wedge at the top of the wall, overlaps the board edge
    u = tol + lug_overlap;
    y0 = side==0 ? cy0 : cy1;
    m  = side==0 ? 1 : -1;
    translate([r[0], y0, z_top_pcb + 0.05])
        rotate([90,0,90]) linear_extrude(r[1]-r[0])
            polygon([[0,0],[m*u,0],[0,z_seam - z_top_pcb + 0.6]]);
}

module ears() {
    for (x=ear_x) for (s=[0,1]) {
        y = s==0 ? oy0 : oy1;
        m = s==0 ? -1 : 1;
        difference() {
            union() {
                hull() {
                    translate([x-ear_w/2, s==0 ? y : y-1, 0]) cube([ear_w, 1, ear_t]);
                    translate([x, y + m*(ear_l-ear_w/2), 0]) cylinder(d=ear_w, h=ear_t);
                }
                // Support ribs to the wall
                for (dx=[-ear_w/2, ear_w/2-1.6]) hull() {
                    translate([x+dx, s==0 ? y-0.01 : y-0.01, 0]) cube([1.6, 0.02, ear_t+5]);
                    translate([x+dx, y + m*3, 0]) cube([1.6, 0.02, ear_t]);
                }
            }
            // Slot (M4 or cable tie)
            hull() for (k=[0,1]) translate([x, y + m*(ear_l-ear_w/2 - k*ear_slot[1]+ ear_slot[1]/2), -1])
                cylinder(d=ear_slot[0], h=ear_t+2);
        }
    }
}

module tie_saddles() {
    // End wall X=max: CAN leads
    saddle(ox1, 1, [[can_a_y[0]-1, can_a_y[1]+1], [can_b_y[0]-1, can_b_y[1]+1]]);
    // End wall X=0: 12-24V lead (USB/QWIIC stay clear)
    saddle(ox0, -1, [[oy0+0.5, pwr_y[1]+2.5]]);
}
module saddle(xw, m, ranges) {
    for (b=ranges) {
        y0 = max(b[0], oy0); y1 = min(b[1], oy1);
        difference() {
            union() {
                // Floor plate
                translate([m>0 ? xw-0.01 : xw-saddle_l+0.01, y0, 0]) cube([saddle_l, y1-y0, 3]);
                // Saddle block
                translate([m>0 ? xw+3 : xw-9, y0, 0]) cube([6, y1-y0, saddle_h]);
                // Ramp to the wall (support-free, ends under the lid skirt)
                hull() {
                    translate([xw-0.01, y0, 0]) cube([0.02, y1-y0, z_seam-rebate_h-0.3]);
                    translate([m>0 ? xw+3 : xw-3.5, y0, 0]) cube([0.5, y1-y0, saddle_h]);
                }
            }
            // Cable-tie tunnel across (Y direction), 5 x 2.2
            translate([m>0 ? xw+3.5 : xw-8.5, y0-1, saddle_h-5.5]) cube([5, y1-y0+2, 2.2]);
            // Exit holes at the top edge so the tie can wrap the bundle
            for (yy=[y0+1.0, y1-3.2]) translate([m>0 ? xw+3.5 : xw-8.5, yy, saddle_h-5.5]) cube([5, 2.2, 10]);
            // Cable recess
            translate([m>0 ? xw-1 : xw-saddle_l-1, (y0+y1)/2, saddle_h+3]) rotate([0,90,0])
                scale([1,(y1-y0-6)/8,1]) cylinder(d=8, h=saddle_l+2);
        }
    }
}

// ------------------------- antenna -------------------------
module antenna_tower() {
    // full wall thickness on the wall at Y=max almost up to the roof
    intersection() {
        linear_extrude(z_roof - fit_gap) body2d();
        translate([ant_tx0, cy1, 0]) cube([ant_tx1-ant_tx0, oy1-cy1+1, z_roof]);
    }
}
module antenna_pocket() {
    // shallow pocket for the FPC antenna (inner face of the wall)
    translate([ant_x_cable, cy1-0.01, ant_z0]) cube([ant_l+2*ant_margin, ant_pocket_d+0.01, ant_w+2*ant_margin]);
    // Cable exit: groove at the lower edge of the pocket towards the U.FL, running down/forward
    hull() {
        translate([ant_x_cable+1, cy1-0.01, ant_z0+0.2]) cube([4, ant_pocket_d+0.01, ant_cable_d+0.6]);
        translate([ant_tx0-0.01, cy1-0.01, ant_z0-0.6]) cube([0.02, ant_pocket_d+0.5, ant_cable_d+0.6]);
    }
    // Finger recess at the other end for peeling off / aligning
    translate([ant_x_cable+ant_l+2*ant_margin-0.01, cy1-0.01, ant_z0+ant_margin+ant_w/2-2]) cube([1.5, ant_pocket_d+0.01, 4]);
}

// ------------------------- lid -------------------------
module lid() {
    difference() {
        union() {
            // Roof + walls
            translate([0,0,z_seam]) linear_extrude(z_total - z_seam) union() { body2d(); bosses2d(); }
            // Rebate skirt on the outside
            translate([0,0,z_seam-rebate_h]) linear_extrude(rebate_h+0.01)
                difference() { body2d(); offset(r=-rebate_w+fit_gap/2) body2d(); bosses2d(fit_gap); }
        }
        // Interior
        translate([cx0, cy0, z_seam-rebate_h-1]) cube([cx1-cx0, cy1-cy0, z_roof-(z_seam-rebate_h-1)]);
        // Screw holes + counterbores
        for (d=bosses) translate([d[0], d[1], 0]) {
            translate([0,0,z_seam-1]) cylinder(d=screw_d, h=50);
            translate([0,0,z_seam+head_stock]) cylinder(d=head_d, h=50);
        }
        cutouts(true);
        // Cut-out for the antenna tower on the base
        if (antenna) translate([ant_tx0-fit_gap, cy1-0.01, z_seam-rebate_h-1])
            cube([ant_tx1-ant_tx0+2*fit_gap, oy1-cy1+1, z_roof-fit_gap/2-(z_seam-rebate_h-1)]);
        if (labels) lid_labels();
    }
    // Perimeter edge ribs (press the bare PCB edges onto the base ledges)
    lid_edge_ribs();
}

module lid_edge_ribs() {
    for (r = rib_y0) lid_edge_rib(r[0], r[1], 0);
    for (r = rib_y1) lid_edge_rib(r[0], r[1], 1);
}

// side 0 = wall Y=0, side 1 = wall Y=max.
// Hangs from the lid roof down to z_top_pcb + rib_gap.  The inward face is
// chamfered at the tip so the lid still prints support-free upside-down.
// On Y=max, stay inward of cy1-fit_gap so the rib misses the antenna tower.
module lid_edge_rib(x0, x1, side) {
    z0 = z_top_pcb + rib_gap;
    z1 = z_roof + 0.02;
    y_wall = (side == 0) ? cy0 : (antenna ? cy1 - fit_gap : cy1);
    y_in   = (side == 0) ? rib_overlap : pcb_w - rib_overlap;
    y_tip  = (side == 0) ? (rib_overlap - rib_chamfer) : (pcb_w - rib_overlap + rib_chamfer);
    y_full0 = min(y_wall, y_in);
    y_full1 = max(y_wall, y_in);
    y_tip0  = min(y_wall, y_tip);
    y_tip1  = max(y_wall, y_tip);
    hull() {
        translate([x0, y_tip0, z0])        cube([x1 - x0, y_tip1 - y_tip0, 0.02]);
        translate([x0, y_full0, z0 + rib_chamfer]) cube([x1 - x0, y_full1 - y_full0, 0.02]);
        translate([x0, y_full0, z1 - 0.02]) cube([x1 - x0, y_full1 - y_full0, 0.02]);
    }
}

module lid_labels() {
    f = "Liberation Sans:style=Bold";
    translate([0,0,z_total-0.6]) linear_extrude(1) {
        translate([pcb_l/2-4, pcb_w/2]) text("T-2CAN FD", size=6, halign="center", valign="center", font=f);
        translate([pcb_l-5, (can_a_y[0]+can_a_y[1])/2]) rotate(90) text("CAN-A", size=3.2, halign="center", valign="center", font=f);
        translate([pcb_l-5, (can_b_y[0]+can_b_y[1])/2]) rotate(90) text("CAN-B", size=3.2, halign="center", valign="center", font=f);
        translate([5, (pwr_y[0]+pwr_y[1])/2]) rotate(90) text("12V", size=3.2, halign="center", valign="center", font=f);
        translate([5, usb_y]) rotate(90) text("USB", size=3.0, halign="center", valign="center", font=f);
        translate([14.5, pcb_w-2.5]) text("BOOT RST", size=2.2, halign="center", valign="center", font=f);
        if (sma_bore) translate([sma_x, pcb_w-3]) text("ANT", size=2.6, halign="center", valign="center", font=f);
        translate([(sd_x0+sd_x1)/2, 3]) text("SD", size=3, halign="center", valign="center", font=f);
    }
}

// ------------------------- board placeholder -------------------------
module board() {
    // PCB
    color("#1a1a1a") translate([0,0,z_pcb]) cube([pcb_l, pcb_w, pcb_t]);
    zt = z_top_pcb;
    // ESP32-S3-WROOM-1U 18 x 19.2 x 3.2 [DS], position [DXF]
    color("silver") translate([22.4, 19.4, zt]) cube([18, 19.2, 3.2]);
    // U.FL [PHOTO] (37.3/35.7) on the module PCB, see antenna placeholder
    // 330µF/50V electrolytic, Ø10 [DXF] (photo ±1), height [CALIPER]
    color("#333") translate([19.27, 7.24, zt]) cylinder(d=10, h=h_capacitor);
    // Isolated CAN transceivers TD501MCANFD [DXF]/[PHOTO] height [CALIPER]
    color("#222") translate([45.5, 2.6, zt]) cube([10.8, 14.6, h_transceiver]);
    color("#222") translate([45.5, 22.0, zt]) cube([10.8, 14.6, h_transceiver]);
    // LILYGO logo (bare centre area; v1/v2 hold-downs used to press here) [PHOTO]
    color("white") translate([55, 18.4, zt]) cube([17, 3.5, 0.02]);
    // Power inductor "100" [EST]
    color("#444") translate([36.6, 6.2, zt]) cube([6.4, 6.9, 4.5]);
    // MCP2518FD SOIC-14 [DXF]
    color("#111") translate([11.1, 16.1, zt]) cube([6.4, 8.7, 1.75]);
    // 12-24V terminal, 2-pin 5.08 [DXF] height [CALIPER]
    color("#2e7d32") translate([0.2, 0.9, zt]) cube([8.0, 10.2, h_pwr_terminal]);
    // CAN-A / CAN-B terminals, 4-pin 3.81 [DXF] height [CALIPER]; front face flush with the board edge [PHOTO]
    color("#2e7d32") for (r=[can_a_y, can_b_y]) translate([82.0, r[0]+0.4, zt]) cube([7.9, r[1]-r[0]-0.8, h_can_terminal]);
    // USB-C [DXF] (overhangs the edge by 1.46)
    color("silver") translate([-1.46, usb_y-4.47, zt]) cube([8.7, 8.94, usb_h]);
    // QWIIC SH1.0 4P, horizontal [DXF]
    color("ivory") for (y=qwiic_y) translate([0.3, y-3.0, zt]) cube([4.3, 6.0, 2.95]);
    // BOOT/RST side buttons [DXF] (actuator ~0.4 past the edge, [EST])
    color("#888") for (x=button_x) translate([x-1.75, 35.4, zt]) cube([3.5, 3.5, 3.5]);
    // THT solder joints on the bottom [PHOTO] positions, length pin_below [CALIPER] (conservative, used for all)
    color("gold") {
        for (y=[3.68,7.49,11.3,15.11,23.37,27.18,30.99,34.8]) translate([85.9, y, z_pcb-pin_below]) cylinder(d=1.8, h=pin_below);
        for (y=[3.56,8.64]) translate([5.16, y, z_pcb-pin_below]) cylinder(d=2.0, h=pin_below);
        // Transformer pins: column X 46.82, 4 pins each; column X 54.44, 3 pins each (photo)
        for (y=[6.22,8.76,11.3,13.84,26.54,29.08,31.62,34.16]) translate([46.82, y, z_pcb-pin_below]) cylinder(d=1.6, h=pin_below);
        for (y=[6.22,8.76,11.3,26.54,29.08,31.62]) translate([54.44, y, z_pcb-pin_below]) cylinder(d=1.6, h=pin_below);
        // USB-C shield tabs (slots) [PHOTO]
        for (x=[1.2,5.4]) for (y=[28.9,37.7]) translate([x, y, z_pcb-1.2]) cylinder(d=2.4, h=1.2);
    }
    // U8 SMD pads, bottom side [DXF]
    color("orange") for (i=[0:12]) for (y=[28.0,33.6]) translate([38.41-i*2.54-0.51, y-1.56, z_pcb-0.05]) cube([1.02, 3.12, 0.05]);
    // microSD Adafruit 4682 in the pocket (slot edge towards Y=0)
    color("#1565c0") translate([sd_x0, sd_y0, floor_t]) cube([sd_l, sd_w, sd_pcb_t]);
    color("silver") translate([sd_x0+(sd_l-14.2)/2, sd_y0+0.8, floor_t+sd_pcb_t])
        cube([14.2, 14.5, sd_socket_h]);
    color("#222") translate([sd_x0+(sd_l-11)/2, sd_y0-sd_card_protrusion, floor_t+sd_pcb_t+0.3])
        cube([11, 15, 0.7]);  // inserted card, 2.54 mm protrusion
    if (sd_pins) color("#ccc")
        for (lx = sd_hole_x)
            translate([sd_x0+lx, sd_y0+sd_w-sd_hole_from_header, floor_t-0.01])
                cylinder(d=2.6, h=sd_pcb_t+0.02);  // [CALIPER] hole Ø 2.6
    // FPC antenna in the wall pocket + coax arc to the U.FL (bend radius >= 5)
    if (antenna) {
        color("#111") translate([ant_x_cable+ant_margin, cy1+ant_pocket_d-ant_t, ant_z0+ant_margin]) cube([ant_l, ant_t, ant_w]);
        color("#333") hull() {
            translate([ant_x_cable+ant_margin+1, cy1-0.2, ant_z0+ant_margin+1.5]) sphere(d=ant_cable_d);
            translate([ant_tx0+0.5, cy1-0.8, ant_z0+0.3]) sphere(d=ant_cable_d);
        }
        color("#333") hull() {
            translate([ant_tx0+0.5, cy1-0.8, ant_z0+0.3]) sphere(d=ant_cable_d);
            translate([ufl_pos[0]+0.8, ufl_pos[1]+1.0, z_top_pcb+2.4]) sphere(d=ant_cable_d);
        }
        color("gold") translate([ufl_pos[0], ufl_pos[1], z_top_pcb+0.8]) cylinder(d=3.2, h=2.2); // U.FL sits on the module PCB (WROOM-1U)
    }
    // SMA bulkhead (pigtail)
    if (sma_bore) color("gold") translate([sma_x, cy1-8, z_top_pcb+sma_z_above]) rotate([-90,0,0]) cylinder(d=6.35, h=8+wall+8);
}

// ------------------------- output -------------------------
module section_cut() { intersection() { children(); translate([-60, section_y, -10]) cube([250, 100, 100]); } }
module assembled(exp=0) {
    color("#546e7a") base();
    color("#90a4ae", 0.55) translate([0,0,exp]) lid();
    if (show_board) board();
}

if (part == "assembled") assembled(0);
else if (part == "exploded") assembled(explode_offset);
else if (part == "section") {
    section_cut() color("#546e7a") base();
    section_cut() color("#b0bec5") lid();
    if (show_board) section_cut() board();
}
else if (part == "antenna") { color("#546e7a") base(); if (show_board) board(); }
else if (part == "base") base();
else if (part == "lid") translate([0,0,z_total]) rotate([180,0,0]) lid();   // print orientation
else if (part == "lid_underside") lid();   // installed orientation (view the underside / edge ribs)
else if (part == "section_rib") {
    // Thin YZ slab at X=70.5 (through the long edge ribs at 67-80, before the CAN terminals)
    intersection() {
        translate([70.2, -20, -10]) cube([0.8, 100, 100]);
        union() {
            color("#546e7a") base();
            color("#b0bec5") lid();
            if (show_board) board();
        }
    }
}
else if (part == "build_plate") {
    base();
    translate([0, oy1+ 2*boss_r + 25 + (oy1-oy0), 0]) translate([0,0,z_total]) rotate([180,0,0]) lid();
}

// Collision check (must be empty): part="collision_base" / "collision_lid" / "collision_parts"
if (part == "collision_base") intersection() { base(); board(); }
if (part == "collision_lid")  intersection() { lid();  board(); }
if (part == "collision_parts") intersection() { base(); lid(); }
