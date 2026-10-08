#!/usr/bin/env python3
"""
CykloPCB v1 - nosna deska pro CykloComp (ESP32-C3-Zero + displej + GPS + Li-Po).

Tento skript je "zdrojak" plosneho spoje: z popisu soucastek a spoju nize
vytvori KiCad desku, rozmisti soucastky, nechá cesty natahnout autorouterem
Freerouting, prida rozliti zeme, zkontroluje DRC a vyexportuje podklady
pro vyrobu u JLCPCB (Gerber + vrtani + BOM + CPL pro osazeni).

Pozadavky (Linux):  KiCad 7 (python modul pcbnew + kicad-cli), Java 17+,
                    freerouting-1.9.0.jar, xvfb-run
Spusteni:           python3 generate_pcb.py  [--freerouting /cesta/freerouting.jar]
"""
import argparse
import csv
import math
import os
import re
import shutil
import subprocess
import sys
import zipfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
FP_LIB = '/usr/share/kicad/footprints'
NAME = 'CykloPCB_v1'

W, H = 43.0, 64.0          # rozmer desky [mm]
CORNER = 1.0               # zaobleni rohu

mm = pcbnew.FromMM

# ---------------------------------------------------------------------------
# NAPAJENI (cela cesta):
#   USB-C (J1) --VBUS--> TP4056 (U1, nabijeni 600 mA) --BAT+--> Li-Po (J4)
#   VBUS --D3 Schottky--> SYS ;  BAT+ --Q1 P-MOSFET--> SYS   ("load sharing":
#   pri pripojenem USB jede system z USB a baterie se jen nabiji, jinak z baterie)
#   SYS --SW1 vypinac--> VSW --U2 LDO 3.3 V--> 3V3 (ESP, displej, GPS)
#   VSW --R7/R8 delic 1:2--> GPIO0 (mereni baterie / detekce USB)
#   3V3 --Q2--> GPS_VCC (GPIO10 vypina GPS ve spanku)
#   3V3 --Q3--> BLK displeje (GPIO7 = PWM jas)
# Baterie MUSI mit vlastni ochranny obvod (PCM) - u clanku 103450 standard.
# ---------------------------------------------------------------------------

# LCSC cisla pro osazeni u JLCPCB (pred objednavkou zkontroluj skladem)
LCSC = {
    'TP4056': 'C16581', 'AO3401A': 'C15127', 'SS34': 'C8678', 'AP2112K-3.3': 'C51118',
    'USB-C 16P': 'C165948', '10uF': 'C15850', '100nF': 'C14663', '5.1k': 'C23186',
    '2k': 'C22975', '1k': 'C21190', '100k': 'C25803', '10k': 'C25804',
    'LED red': 'C2286', 'LED green': 'C72043',
}

R0603 = ('Resistor_SMD', 'R_0603_1608Metric')
C0603 = ('Capacitor_SMD', 'C_0603_1608Metric')
C0805 = ('Capacitor_SMD', 'C_0805_2012Metric')
LED0603 = ('LED_SMD', 'LED_0603_1608Metric')
SOT23 = ('Package_TO_SOT_SMD', 'SOT-23')

# (ref, (lib, footprint) | 'C3ZERO', value, x, y, rot, {pad: net}, osazuje JLC)
PARTS = [
    # --- USB-C nabijeni ---
    ('J1', ('Connector_USB', 'USB_C_Receptacle_HRO_TYPE-C-31-M-12'), 'USB-C 16P', 11.5, 60.65, 0,
     {'A1': 'GND', 'B1': 'GND', 'A12': 'GND', 'B12': 'GND', 'S1': 'GND',
      'A4': 'VBUS', 'B4': 'VBUS', 'A9': 'VBUS', 'B9': 'VBUS', 'A5': 'CC1', 'B5': 'CC2'}, True),
    ('R1', R0603, '5.1k', 8.0, 52.6, 90, {'1': 'CC1', '2': 'GND'}, True),
    ('R2', R0603, '5.1k', 15.6, 52.6, 90, {'1': 'CC2', '2': 'GND'}, True),
    ('C1', C0805, '10uF', 18.6, 50.6, 90, {'1': 'VBUS', '2': 'GND'}, True),
    # --- nabijecka TP4056 ---
    ('U1', ('Package_SO', 'SOIC-8-1EP_3.9x4.9mm_P1.27mm_EP2.41x3.3mm'), 'TP4056', 12.0, 46.6, 0,
     {'1': 'GND', '2': 'PROG', '3': 'GND', '4': 'VBUS', '5': 'BAT+', '6': 'STDBY', '7': 'CHRG', '8': 'VBUS', '9': 'GND'}, True),
    ('R3', R0603, '2k', 7.3, 43.0, 0, {'1': 'PROG', '2': 'GND'}, True),
    ('C2', C0805, '10uF', 12.0, 41.4, 0, {'1': 'BAT+', '2': 'GND'}, True),
    ('R4', R0603, '1k', 19.6, 54.2, 0, {'1': 'VBUS', '2': 'LED_R'}, True),
    ('R5', R0603, '1k', 19.6, 56.0, 0, {'1': 'VBUS', '2': 'LED_G'}, True),
    ('D1', LED0603, 'LED red', 19.6, 58.0, 180, {'1': 'CHRG', '2': 'LED_R'}, True),
    ('D2', LED0603, 'LED green', 19.6, 60.0, 180, {'1': 'STDBY', '2': 'LED_G'}, True),
    # --- load sharing + vypinac + LDO ---
    ('D3', ('Diode_SMD', 'D_SMA'), 'SS34', 12.6, 37.6, 0, {'1': 'SYS', '2': 'VBUS'}, True),
    ('Q1', SOT23, 'AO3401A', 18.3, 38.6, 0, {'1': 'VBUS', '2': 'SYS', '3': 'BAT+'}, True),
    ('R6', R0603, '100k', 18.3, 42.2, 0, {'1': 'VBUS', '2': 'GND'}, True),
    ('C3', C0805, '10uF', 18.3, 35.4, 0, {'1': 'SYS', '2': 'GND'}, True),
    ('SW1', ('Button_Switch_THT', 'SW_Slide_SPDT_Angled_CK_OS102011MA1Q'), 'ON/OFF', 2.7, 45.0, -90,
     {'1': 'SYS', '2': 'VSW', '3': ''}, False),
    ('U2', ('Package_TO_SOT_SMD', 'SOT-23-5'), 'AP2112K-3.3', 9.6, 31.6, 0,
     {'1': 'VSW', '2': 'GND', '3': 'VSW', '4': '', '5': '3V3'}, True),
    ('C4', C0805, '10uF', 5.0, 30.0, 90, {'1': 'VSW', '2': 'GND'}, True),
    ('C6', C0805, '10uF', 14.4, 31.6, 90, {'1': '3V3', '2': 'GND'}, True),
    ('C7', C0603, '100nF', 14.4, 28.2, 0, {'1': '3V3', '2': 'GND'}, True),
    # --- mereni baterie (delic 1:2 -> GPIO0) ---
    ('R7', R0603, '100k', 26.0, 38.4, 0, {'1': 'VSW', '2': 'BAT_SENSE'}, True),
    ('R8', R0603, '100k', 26.0, 36.6, 0, {'1': 'BAT_SENSE', '2': 'GND'}, True),
    ('C5', C0603, '100nF', 26.0, 34.8, 0, {'1': 'BAT_SENSE', '2': 'GND'}, True),
    # --- baterie ---
    ('J4', ('Connector_JST', 'JST_PH_S2B-PH-K_1x02_P2.00mm_Horizontal'), 'LiPo 3.7V', 9.0, 22.6, 90,
     {'1': 'BAT+', '2': 'GND'}, False),
    # --- ESP32-C3-Zero ---
    ('U3', 'C3ZERO', 'ESP32-C3-Zero', 32.0, 52.7, 180,
     {'1': '', '2': 'GND', '3': '3V3', '4': 'BAT_SENSE', '5': 'TFT_RST', '6': 'TFT_DC', '7': 'TFT_CS',
      '8': 'TFT_SCLK', '9': 'BTN1', '10': 'TFT_MOSI', '11': 'BL_PWM', '12': 'BTN2', '13': 'BTN3',
      '14': 'GPS_EN', '15': '', '16': '', '17': 'GPS_TXD', '18': 'GPS_RXD'}, False),
    # --- displej (ST7789, poradi pinu jako bezny 2.0"/2.4"/2.8" IPS modul) ---
    ('J2', ('Connector_PinHeader_2.54mm', 'PinHeader_1x08_P2.54mm_Vertical'), 'TFT', 7.8, 3.2, 90,
     {'1': 'GND', '2': '3V3', '3': 'TFT_SCLK', '4': 'TFT_MOSI', '5': 'TFT_RST', '6': 'TFT_DC', '7': 'TFT_CS', '8': 'TFT_BLK'}, False),
    ('Q3', SOT23, 'AO3401A', 22.0, 9.6, 90, {'1': 'BL_G', '2': '3V3', '3': 'TFT_BLK'}, True),
    ('R11', R0603, '100k', 25.2, 9.0, 90, {'1': '3V3', '2': 'BL_G'}, True),
    ('R12', R0603, '1k', 25.2, 12.6, 90, {'1': 'BL_G', '2': 'BL_PWM'}, True),
    # --- GPS ---
    ('J3', ('Connector_PinHeader_2.54mm', 'PinHeader_1x04_P2.54mm_Vertical'), 'GPS', 29.9, 3.2, 90,
     {'1': 'GPS_VCC', '2': 'GND', '3': 'GPS_TXD', '4': 'GPS_RXD'}, False),
    ('Q2', SOT23, 'AO3401A', 30.4, 9.6, 90, {'1': 'GPS_G', '2': '3V3', '3': 'GPS_VCC'}, True),
    ('R9', R0603, '100k', 33.6, 9.0, 90, {'1': '3V3', '2': 'GPS_G'}, True),
    ('R10', R0603, '1k', 33.6, 12.6, 90, {'1': 'GPS_G', '2': 'GPS_EN'}, True),
    ('C8', C0603, '100nF', 27.6, 9.0, 90, {'1': 'GPS_VCC', '2': 'GND'}, True),
    # --- tlacitka (boční, uhlova) + konektor pro externi vodotesna tlacitka ---
    ('SW2', ('Button_Switch_THT', 'SW_Tactile_SPST_Angled_PTS645Vx31-2LFS'), 'MODE', 2.5, 19.6, 90,
     {'1': 'BTN1', '2': 'GND'}, False),
    ('SW3', ('Button_Switch_THT', 'SW_Tactile_SPST_Angled_PTS645Vx31-2LFS'), 'START', 40.5, 15.6, -90,
     {'1': 'BTN2', '2': 'GND'}, False),
    ('SW4', ('Button_Switch_THT', 'SW_Tactile_SPST_Angled_PTS645Vx31-2LFS'), 'PAUSE', 40.5, 26.4, -90,
     {'1': 'BTN3', '2': 'GND'}, False),
    ('R13', R0603, '100k', 19.0, 23.0, 0, {'1': '3V3', '2': 'BTN1'}, True),
    ('R14', R0603, '10k', 30.0, 21.0, 0, {'1': '3V3', '2': 'BTN2'}, True),
    ('J5', ('Connector_PinHeader_2.54mm', 'PinHeader_1x04_P2.54mm_Vertical'), 'BTN ext', 13.0, 14.0, 90,
     {'1': 'BTN1', '2': 'BTN2', '3': 'BTN3', '4': 'GND'}, False),
    # --- montazni otvory M2.5 ---
    ('H1', ('MountingHole', 'MountingHole_2.7mm_M2.5'), 'M2.5', 3.4, 9.0, 0, {}, False),
    ('H2', ('MountingHole', 'MountingHole_2.7mm_M2.5'), 'M2.5', 39.6, 9.0, 0, {}, False),
    ('H3', ('MountingHole', 'MountingHole_2.7mm_M2.5'), 'M2.5', 3.0, 60.6, 0, {}, False),
    ('H4', ('MountingHole', 'MountingHole_2.7mm_M2.5'), 'M2.5', 39.6, 37.6, 0, {}, False),
]

POWER_NETS = ['GND', 'VBUS', 'BAT+', 'SYS', 'VSW', '3V3', 'GPS_VCC', 'TFT_BLK']

# popisky pinu ESP32-C3-Zero (pady 1-9 vlevo od USB dolu, 10-18 vpravo zdola nahoru, USB nahore)
C3_LABELS = ['5V', 'GND', '3V3', 'GP0', 'GP1', 'GP2', 'GP3', 'GP4', 'GP5',
             'GP6', 'GP7', 'GP8', 'GP9', 'GP10', 'GP18', 'GP19', 'GP20', 'GP21']

SILK = [  # (text, x, y, size, rot)
    ('CykloPCB v1', 21.5, 27.4, 1.2, 0),
    ('CykloComp', 21.5, 29.4, 0.9, 0),
    ('TFT', 3.6, 3.2, 1.0, 0),
    ('GPS', 27.75, 3.2, 0.8, 90),
    ('+', 7.3, 22.6, 0.9, 0),
    ('-', 7.3, 20.6, 0.9, 0),
    ('ON', 5.2, 40.6, 1.0, 0),
    ('MODE', 3.4, 25.0, 0.8, 0),
    ('START', 36.0, 12.0, 0.8, 0),
    ('PAUZA', 34.5, 23.4, 0.8, 0),
    ('NABIJENI', 11.5, 50.3, 0.8, 0),
    ('ESP USB', 32.0, 39.6, 0.8, 0),
]
# popisky pinu konektoru (pod kazdym pinem)
for i, t in enumerate(['G', 'V', 'CK', 'DA', 'RS', 'DC', 'CS', 'BL']):
    SILK.append((t, 7.8 + i * 2.54, 5.6, 0.8, 0))
for i, t in enumerate(['V', 'G', 'TX', 'RX']):
    SILK.append((t, 29.9 + i * 2.54, 5.6, 0.8, 0))
for i, t in enumerate(['M', 'S', 'P', 'G']):
    SILK.append((t, 13.0 + i * 2.54, 16.7, 0.8, 0))


# ---------------------------------------------------------------------------
def load_fp(lib, name):
    fp = pcbnew.FootprintLoad(f'{FP_LIB}/{lib}.pretty', name)
    if fp is None:
        sys.exit(f'Footprint {lib}:{name} nenalezen')
    return fp


def make_c3zero(board):
    """Waveshare ESP32-C3-Zero (18 x 23.5 mm). Pady 2.54 mm, rady +-8.89 mm.
    Pady jsou protazene (2.6 mm), takze jde modul zapajet pres kolíkovou listu
    i naplocho za "castellated" pulotvory na okraji."""
    fp = pcbnew.FOOTPRINT(board)
    fp.SetFPID(pcbnew.LIB_ID('CykloPCB', 'ESP32-C3-Zero'))
    for i in range(18):
        if i < 9:
            x, y = -8.89, -10.16 + i * 2.54
        else:
            x, y = 8.89, 10.16 - (i - 9) * 2.54
        p = pcbnew.PAD(fp)
        p.SetNumber(str(i + 1))
        p.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
        p.SetShape(pcbnew.PAD_SHAPE_RECT if i == 0 else pcbnew.PAD_SHAPE_OVAL)
        p.SetSize(pcbnew.VECTOR2I(mm(2.6), mm(1.6)))
        p.SetDrillSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
        p.SetLayerSet(p.PTHMask())
        fp.Add(p)
        p.SetPos0(pcbnew.VECTOR2I(mm(x), mm(y)))
        p.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        t = pcbnew.FP_TEXT(fp)
        t.SetText(C3_LABELS[i])
        t.SetLayer(pcbnew.F_SilkS)
        t.SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        t.SetTextThickness(mm(0.15))
        t.SetPosition(pcbnew.VECTOR2I(mm(x + (3.0 if i < 9 else -3.0)), mm(y)))
        fp.Add(t)
    # obrys modulu + USB
    def line(layer, x1, y1, x2, y2, w=0.12):
        s = pcbnew.FP_SHAPE(fp)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(layer)
        fp.Add(s)
        s.SetStart0(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        s.SetEnd0(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        s.SetDrawCoord()
        s.SetWidth(mm(w))
    for layer, d in ((pcbnew.F_Fab, 0), (pcbnew.F_CrtYd, 0.25)):
        x0, y0, x1, y1 = -9 - d, -11.75 - d, 9 + d, 11.75 + d
        line(layer, x0, y0, x1, y0, 0.05); line(layer, x1, y0, x1, y1, 0.05)
        line(layer, x1, y1, x0, y1, 0.05); line(layer, x0, y1, x0, y0, 0.05)
    return fp


def add_edge(board):
    def seg(x1, y1, x2, y2):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
        s.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
        s.SetWidth(mm(0.1))
        board.Add(s)

    def arc(cx, cy, sx, sy, angle):
        a = pcbnew.PCB_SHAPE(board)
        a.SetShape(pcbnew.SHAPE_T_ARC)
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetCenter(pcbnew.VECTOR2I(mm(cx), mm(cy)))
        a.SetStart(pcbnew.VECTOR2I(mm(sx), mm(sy)))
        a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T), True)
        a.SetWidth(mm(0.1))
        board.Add(a)
    r = CORNER
    seg(r, 0, W - r, 0); seg(W, r, W, H - r); seg(W - r, H, r, H); seg(0, H - r, 0, r)
    arc(W - r, r, W - r, 0, 90); arc(W - r, H - r, W, H - r, 90)
    arc(r, H - r, r, H, 90); arc(r, r, 0, r, 90)


def add_text(board, text, x, y, size, rot=0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(0.12, size * 0.15)))
    t.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    t.SetTextAngle(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
    board.Add(t)


def add_zone(board, net, layer, poly, rule_area=False):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    o = z.Outline()
    o.NewOutline()
    for x, y in poly:
        o.Append(mm(x), mm(y))
    if rule_area:
        z.SetIsRuleArea(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
    else:
        z.SetNetCode(net.GetNetCode())
        z.SetLocalClearance(mm(0.3))
        z.SetMinThickness(mm(0.25))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
        z.SetThermalReliefGap(mm(0.4))
        z.SetThermalReliefSpokeWidth(mm(0.5))
        z.SetAssignedPriority(0)
    board.Add(z)
    return z


def courtyard_bbox(fp):
    xs, ys = [], []
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_CrtYd:
            bb = g.GetBoundingBox()
            xs += [bb.GetX(), bb.GetRight()]
            ys += [bb.GetY(), bb.GetBottom()]
    if not xs:
        bb = fp.GetBoundingBox(False, False)
        return bb.GetX(), bb.GetY(), bb.GetRight(), bb.GetBottom()
    return min(xs), min(ys), max(xs), max(ys)


def check_overlaps(board):
    fps = list(board.GetFootprints())
    boxes = {f.GetReference(): courtyard_bbox(f) for f in fps}
    bad = []
    refs = sorted(boxes)
    for i, a in enumerate(refs):
        ax0, ay0, ax1, ay1 = boxes[a]
        for b in refs[i + 1:]:
            bx0, by0, bx1, by1 = boxes[b]
            if ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1:
                ov = min(ax1, bx1) - max(ax0, bx0), min(ay1, by1) - max(ay0, by0)
                bad.append((a, b, round(pcbnew.ToMM(ov[0]), 2), round(pcbnew.ToMM(ov[1]), 2)))
    return bad


# ---------------------------------------------------------------------------
def build_board():
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(2)
    ds = board.GetDesignSettings()
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(0.25))
    nc.SetClearance(mm(0.2))
    nc.SetViaDiameter(mm(0.6))
    nc.SetViaDrill(mm(0.3))
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_MinClearance = mm(0.15)
    ds.m_ViasMinSize = mm(0.5)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleClearance = mm(0.25)
    ds.m_HoleToHoleMin = mm(0.25)

    nets = {}
    def net(name):
        if not name:
            return None
        if name not in nets:
            ni = pcbnew.NETINFO_ITEM(board, name)
            board.Add(ni)
            nets[name] = ni
        return nets[name]

    for ref, fpdef, value, x, y, rot, padmap, _jlc in PARTS:
        fp = make_c3zero(board) if fpdef == 'C3ZERO' else load_fp(*fpdef)
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
        fp.Reference().SetTextThickness(mm(0.12))
        fp.Reference().SetVisible(False)   # popisky jsou vlastni (SILK), refs jsou v BOM/CPL
        fp.Value().SetVisible(False)
        board.Add(fp)
        fp.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
        fp.SetOrientationDegrees(rot)
        for p in fp.Pads():
            n = padmap.get(p.GetNumber())
            ni = net(n)
            if ni is not None:
                p.SetNet(ni)
        if ref == 'J1':
            fp.SetZoneConnection(pcbnew.ZONE_CONNECTION_FULL)  # pevne pripojeni stineni USB-C
        lcsc = LCSC.get(value)
        if lcsc and hasattr(fp, 'SetProperty'):
            fp.SetProperty('LCSC', lcsc)

    add_edge(board)
    for t in SILK:
        add_text(board, *t)
    # 0.45 mm pas podel okraju: zadne cesty ani prokovy (JLC: med >= 0.3 mm od hrany)
    k = 0.45
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        for poly in ([(-1, -1), (W + 1, -1), (W + 1, k), (-1, k)], [(-1, H - k), (W + 1, H - k), (W + 1, H + 1), (-1, H + 1)],
                     [(-1, -1), (k, -1), (k, H + 1), (-1, H + 1)], [(W - k, -1), (W + 1, -1), (W + 1, H + 1), (W - k, H + 1)]):
            add_zone(board, None, layer, poly, rule_area=True)
    # 4 prokovy v chladici plosce TP4056 -> teplo do spodni zeme
    u1 = board.FindFootprintByReference('U1')
    c = u1.GetPosition()
    for dx in (-0.6, 0.6):
        for dy in (-0.9, 0.9):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(c.x + mm(dx), c.y + mm(dy)))
            v.SetWidth(mm(0.6))
            v.SetDrill(mm(0.3))
            v.SetNet(nets['GND'])
            board.Add(v)
    # pod modulem ESP nesmi byt na horni strane cesty ani prokovy (zkrat na jeho spodni pady)
    add_zone(board, None, pcbnew.F_Cu,
             [(24.5, 42.0), (39.5, 42.0), (39.5, 64.5), (24.5, 64.5)], rule_area=True)
    return board, nets


def export_dsn(board, path):
    if not pcbnew.ExportSpecctraDSN(board, path):
        sys.exit('Export DSN selhal')
    # sirsi cesty pro napajeni (trida "power")
    s = open(path).read()
    m = re.search(r'\(class kicad_default "" (.*?)\(circuit', s, re.S)
    if m:
        members = m.group(1).split()
        power = [n for n in members if n.strip('"') in POWER_NETS]
        rest = [n for n in members if n not in power]
        old = m.group(0)
        new = '(class kicad_default "" ' + ' '.join(rest) + '\n      (circuit'
        s = s.replace(old, new)
        cls = ('    (class power ' + ' '.join(power) +
               '\n      (circuit\n        (use_via "Via[0-1]_600:300_um")\n      )\n'
               '      (rule\n        (width 500)\n        (clearance 200)\n      )\n    )\n')
        # vloz novou tridu pred konec sekce network
        idx = s.rfind('(class kicad_default')
        end = s.find('\n  )\n  (wiring', idx)
        s = s[:end] + '\n' + cls.rstrip('\n') + s[end:]
    open(path, 'w').write(s)


def run_freerouting(jar, dsn, ses, passes=40):
    cmd = ['java', '-jar', jar, '-de', dsn, '-do', ses, '-mp', str(passes)]
    if shutil.which('xvfb-run') and not os.environ.get('DISPLAY'):
        cmd = ['xvfb-run', '-a'] + cmd
    print('>>', ' '.join(cmd))
    subprocess.run(cmd, check=False, timeout=1800, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if not os.path.exists(ses):
        sys.exit('Freerouting nevytvoril SES')


def import_ses(board, nets, ses):
    """Nacte cesty a prokovy ze SES (Specctra session) do desky."""
    s = open(ses).read()
    res = re.search(r'\(resolution\s+(\w+)\s+(\d+)\)', s)
    unit, per = res.group(1), int(res.group(2))
    scale = {'um': 1e-3, 'mm': 1.0, 'mil': 0.0254, 'inch': 25.4}[unit] / per  # -> mm
    layers = {'F.Cu': pcbnew.F_Cu, 'B.Cu': pcbnew.B_Cu}
    count_t = count_v = 0
    existing = {(v.GetPosition().x, v.GetPosition().y) for v in board.GetTracks() if v.GetClass() == 'PCB_VIA'}
    # projdeme bloky (net ...) v sekci network_out
    pos = s.find('(network_out')
    body = s[pos:]
    net_re = re.compile(r'\(net\s+("([^"]*)"|(\S+))')
    for m in net_re.finditer(body):
        name = m.group(2) if m.group(2) is not None else m.group(3)
        start = m.end()
        nxt = net_re.search(body, start)
        block = body[start: nxt.start() if nxt else len(body)]
        ni = nets.get(name)
        if ni is None:
            continue
        for w in re.finditer(r'\(path\s+(\S+)\s+(\d+)\s+([-\d\s]+)\)', block):
            layer = layers.get(w.group(1))
            width = int(w.group(2)) * scale
            nums = list(map(int, w.group(3).split()))
            pts = [(nums[i] * scale, -nums[i + 1] * scale) for i in range(0, len(nums) - 1, 2)]
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(mm(x1), mm(y1)))
                t.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(y2)))
                t.SetWidth(mm(width))
                t.SetLayer(layer)
                t.SetNet(ni)
                board.Add(t)
                count_t += 1
        for v in re.finditer(r'\(via\s+"?([^"\s]+)"?\s+(-?\d+)\s+(-?\d+)', block):
            mv = re.search(r'(\d+):(\d+)_um', v.group(1))
            dia, drill = (int(mv.group(1)) / 1000, int(mv.group(2)) / 1000) if mv else (0.6, 0.3)
            vp = pcbnew.VECTOR2I(mm(int(v.group(2)) * scale), mm(-int(v.group(3)) * scale))
            if any(abs(vp.x - ex) < mm(0.05) and abs(vp.y - ey) < mm(0.05) for ex, ey in existing):
                continue
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(vp)
            via.SetWidth(mm(dia))
            via.SetDrill(mm(drill))
            via.SetNet(ni)
            board.Add(via)
            count_v += 1
    print(f'importovano {count_t} usecek, {count_v} prokovu')


def add_zones_only(board, nets):
    poly = [(0, 0), (W, 0), (W, H), (0, H)]
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        add_zone(board, nets['GND'], layer, poly)


def fill_zones(board):
    board.BuildListOfNets()
    board.BuildConnectivity()
    zones = pcbnew.ZONES()
    for z in board.Zones():
        if not z.GetIsRuleArea():
            zones.append(z)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(zones)


def run_drc(board, path):
    pcbnew.WriteDRCReport(board, path, pcbnew.EDA_UNITS_MILLIMETRES, True)
    txt = open(path).read()
    viol = re.search(r'\*\* Found (\d+) DRC violations', txt)
    unconn = re.search(r'\*\* Found (\d+) unconnected pads', txt)
    return int(viol.group(1)) if viol else -1, int(unconn.group(1)) if unconn else -1, txt


def fab_outputs(pcb_path, outdir, board):
    gdir = os.path.join(outdir, 'gerber')
    os.makedirs(gdir, exist_ok=True)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'gerbers', '--output', gdir + '/',
                    '--layers', 'F.Cu,B.Cu,F.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts',
                    '--no-x2', '--subtract-soldermask', pcb_path], check=True)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'drill', '--output', gdir + '/', '--format', 'excellon',
                    '--excellon-separate-th', '--generate-map', '--map-format', 'gerberx2', pcb_path], check=True)
    zpath = os.path.join(outdir, f'{NAME}_gerber_JLCPCB.zip')
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            if not f.endswith('drl_map.gbr'):
                z.write(os.path.join(gdir, f), f)

    # BOM + CPL ve formatu JLCPCB (jen SMD dily, ktere osadi JLC)
    jlc_refs = {p[0] for p in PARTS if p[7]}
    groups = {}
    cpl = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref not in jlc_refs:
            continue
        val = fp.GetValue()
        fpname = fp.GetFPID().GetLibItemName().wx_str()
        groups.setdefault((val, fpname), []).append(ref)
        pos = fp.GetPosition()
        cpl.append([ref, f'{pcbnew.ToMM(pos.x):.3f}mm', f'{-pcbnew.ToMM(pos.y):.3f}mm', 'Top',
                    f'{fp.GetOrientationDegrees() % 360:.0f}'])
    with open(os.path.join(outdir, f'{NAME}_BOM_JLCPCB.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #'])
        for (val, fpname), refs in sorted(groups.items()):
            w.writerow([val, ','.join(sorted(refs, key=lambda r: (r[0], int(re.sub(r'\D', '', r) or 0)))),
                        fpname, LCSC.get(val, '')])
    with open(os.path.join(outdir, f'{NAME}_CPL_JLCPCB.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        w.writerows(sorted(cpl))

    # rucne pajene dily (THT) - seznam pro nakup
    with open(os.path.join(outdir, f'{NAME}_rucni_osazeni.csv'), 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['Oznaceni', 'Dil', 'Pouzdro'])
        for ref, fpdef, value, *_rest in PARTS:
            if not _rest[-1] and not ref.startswith('H'):
                w.writerow([ref, value, fpdef if isinstance(fpdef, str) else fpdef[1]])

    # nahledy desky
    for side, layers in (('top', 'F.Cu,F.SilkS,F.Mask,Edge.Cuts'), ('bottom', 'B.Cu,B.SilkS,B.Mask,Edge.Cuts')):
        subprocess.run(['kicad-cli', 'pcb', 'export', 'svg', '--output', os.path.join(outdir, f'{NAME}_{side}.svg'),
                        '--layers', layers, '--exclude-drawing-sheet', '--page-size-mode', '2'] +
                       (['--mirror'] if side == 'bottom' else []) + [pcb_path], check=False)
    subprocess.run(['kicad-cli', 'pcb', 'export', 'step', '--output', os.path.join(outdir, f'{NAME}.step'),
                    '--subst-models', '--force', pcb_path], check=False,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--freerouting', default=os.environ.get('FREEROUTING_JAR', 'freerouting-1.9.0.jar'))
    ap.add_argument('--passes', type=int, default=40)
    ap.add_argument('--place-only', action='store_true')
    ap.add_argument('--reuse-ses', action='store_true', help='nepoustet Freerouting, pouzit build/*.ses')
    args = ap.parse_args()

    out = os.path.join(HERE, 'build')
    os.makedirs(out, exist_ok=True)
    board, nets = build_board()
    bad = check_overlaps(board)
    if bad:
        print('PREKRYVY courtyard:')
        for b in bad:
            print('  ', b)
    pcb_path = os.path.join(HERE, f'{NAME}.kicad_pcb')
    if args.place_only:
        board.Save(pcb_path)
        return

    dsn = os.path.join(out, f'{NAME}.dsn')
    ses = os.path.join(out, f'{NAME}.ses')
    export_dsn(board, dsn)
    if not args.reuse_ses:
        if os.path.exists(ses):
            os.remove(ses)
        run_freerouting(args.freerouting, dsn, ses, args.passes)
    import_ses(board, nets, ses)
    add_zones_only(board, nets)
    board.Save(pcb_path)
    # zony se spolehlive vyleji jen na desce nactene ze souboru (s projektem)
    board = pcbnew.LoadBoard(pcb_path)
    fill_zones(board)
    board.Save(pcb_path)

    nviol, nunc, txt = run_drc(board, os.path.join(out, 'drc.rpt'))
    cats = re.findall(r'^\[(\w+)\]', txt, re.M)
    real = [c for c in cats if c != 'lib_footprint_issues']   # jen chybejici tabulka knihoven
    print(f'DRC: {len(real)} poruseni ({", ".join(sorted(set(real))) or "zadna"}), {nunc} nepripojenych padu (build/drc.rpt)')
    fab_outputs(pcb_path, out, board)
    print('Hotovo ->', out)


if __name__ == '__main__':
    main()
