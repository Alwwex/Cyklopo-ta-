#!/usr/bin/env python3
"""Schema zapojeni CykloPCB v1 (dokumentace) -> schema_zapojeni.svg / .png
Spoje mezi bloky jsou oznaceny jmeny siti (cervene) - stejne jako v generate_pcb.py.
Pouziti: pip install schemdraw cairosvg && python3 schematic.py"""
import os

import schemdraw
import schemdraw.elements as e

HERE = os.path.dirname(os.path.abspath(__file__))
schemdraw.config(fontsize=11, lw=1.3)
RED = '#c03000'


def netlabel(d, at, name, side='right'):
    d += e.Label().at(at).label(name, loc=side, fontsize=10, color=RED)


def title(d, xy, text):
    d += e.Label().at(xy).label(text, loc='center', halign='left', fontsize=13, color='#1a4fa0')


with schemdraw.Drawing(show=False) as d:
    d.config(unit=2.0)

    d += e.Label().at((-4, 2.5)).label(' ')     # okraj vlevo
    d += e.Label().at((-4, -22)).label(' ')
    # ======================================================= 1) USB-C + TP4056
    title(d, (-1, 2.0), '1) USB-C nabíjení  (TP4056, 600 mA)')
    j1 = e.Ic(pins=[e.IcPin(name='GND', side='right'), e.IcPin(name='CC2', side='right'),
                    e.IcPin(name='CC1', side='right'), e.IcPin(name='VBUS', side='right')],
              size=(2.2, 3.4), pinspacing=0.8, edgepadH=0.5).at((0, -3)).right().label('J1\nUSB-C', loc='center', ofst=(-0.4, 0))
    d += j1
    d += e.Line().at(j1.VBUS).right(0.8)
    netlabel(d, d.here, 'VBUS')
    d += e.Line().at(j1.CC1).right(3.0)
    d += e.Resistor().down(2.4).label('R1 5k1', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Line().at(j1.CC2).right(1.4)
    d += e.Resistor().down(1.6).label('R2 5k1', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Line().at(j1.GND).right(0.6)
    d += e.Ground()

    u1 = e.Ic(pins=[e.IcPin(name='VCC', pin='4', side='left'), e.IcPin(name='CE', pin='8', side='left'),
                    e.IcPin(name='BAT', pin='5', side='right'), e.IcPin(name='CHRG', pin='7', side='right'),
                    e.IcPin(name='STDBY', pin='6', side='right'), e.IcPin(name='TEMP', pin='1', side='left'),
                    e.IcPin(name='GND', pin='3,EP', side='left'), e.IcPin(name='PROG', pin='2', side='bottom')],
              size=(6.0, 3.0), pinspacing=0.9).at((10.5, -3.4)).right().label('U1  TP4056', loc='center')
    d += u1
    for p in (u1.VCC, u1.CE):
        d += e.Line().at(p).left(0.6)
        netlabel(d, d.here, 'VBUS', 'left')
    d += e.Line().at(u1.TEMP).left(0.6)
    d += e.Ground()
    d += e.Line().at(u1.GND).left(1.4)
    d += e.Ground()
    d += e.Resistor().at(u1.PROG).down(1.6).label('R3 2k\n→ 600 mA', loc='bottom', fontsize=9)
    d += e.Ground()
    # LED indikace
    for pin, dx, led, r in ((u1.STDBY, 1.4, 'D2 zelená\n(nabito)', 'R5 1k'), (u1.CHRG, 3.6, 'D1 červená\n(nabíjí)', 'R4 1k')):
        d += e.Line().at(pin).right(dx)
        d += e.LED().up(1.8).reverse().label(led, loc='bottom', fontsize=9)
        d += e.Resistor().up(1.6).label(r, loc='bottom', fontsize=9)
        netlabel(d, d.here, 'VBUS', 'top')
    d += e.Line().at(u1.BAT).right(5.6)
    d += e.Dot()
    bat = d.here
    netlabel(d, bat, 'BAT+', 'top')
    d += e.Capacitor().at(bat).down(1.6).label('C2 10µ', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Line().at(bat).right(2.2)
    d += e.Battery().down(1.8).label('J4  Li-Po 3,7 V\n(článek s PCM!)', loc='bottom', fontsize=9)
    d += e.Ground()

    # ======================================================= 2) napajeni
    title(d, (-1, -9.0), '2) Napájení: load-sharing → vypínač → 3,3 V,  měření baterie')
    y = -11.0
    netlabel(d, (0, y), 'VBUS', 'left')
    d += e.Line().at((0, y)).right(0.8)
    d += e.Dot()
    vb = d.here
    d += e.Capacitor().at(vb).down(1.6).label('C1 10µ', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Schottky().at(vb).right(2.4).label('D3 SS34', loc='top', fontsize=9)
    d += e.Dot()
    sys_n = d.here
    netlabel(d, sys_n, 'SYS', 'top')
    # Q1: BAT+ -> SYS, gate = VBUS (pri USB zavreny)
    d += e.Line().at(sys_n).down(1.0)
    q1 = e.PFet(bulk=False).right().anchor('source').label('Q1 AO3401A', loc='left', fontsize=9)
    d += q1
    d += e.Line().at(q1.drain).down(0.6)
    netlabel(d, d.here, 'BAT+', 'bottom')
    d += e.Line().at(q1.gate).right(0.9)
    d += e.Dot()
    g1 = d.here
    d += e.Line().at(g1).right(0.3)
    netlabel(d, d.here, 'VBUS')
    d += e.Resistor().at(g1).down(1.6).label('R6 100k', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Line().at(sys_n).right(4.6)
    d += e.Dot()
    s2 = d.here
    d += e.Capacitor().at(s2).down(1.6).label('C3 10µ', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Switch().at(s2).right(2.0).label('SW1 ON/OFF', loc='top', fontsize=9)
    d += e.Dot()
    vsw = d.here
    netlabel(d, vsw, 'VSW', 'top')
    d += e.Capacitor().at(vsw).down(1.6).label('C4 10µ', loc='bottom', fontsize=9)
    d += e.Ground()
    d += e.Line().at(vsw).right(1.4)
    u2 = e.Ic(pins=[e.IcPin(name='VIN', pin='1', side='left'), e.IcPin(name='EN', pin='3', side='left'),
                    e.IcPin(name='VOUT', pin='5', side='right'), e.IcPin(name='GND', pin='2', side='bottom')],
              size=(4.0, 2.2), pinspacing=0.8).right().anchor('VIN').label('U2  AP2112K-3.3', loc='top', fontsize=10)
    d += u2
    d += e.Line().at(u2.EN).left(0.5)
    d += e.Line().toy(u2.VIN)
    d += e.Line().at(u2.GND).down(0.5)
    d += e.Ground()
    d += e.Line().at(u2.VOUT).right(1.0)
    d += e.Dot()
    v33 = d.here
    netlabel(d, v33, '3V3', 'top')
    d += e.Capacitor().at(v33).down(1.6).label('C6 10µ\nC7 100n', loc='bottom', fontsize=9)
    d += e.Ground()
    # delic pro mereni baterie
    xdv = v33[0] + 3.0
    netlabel(d, (xdv, y + 1.0), 'VSW', 'top')
    d += e.Line().at((xdv, y + 1.0)).down(0.6)
    d += e.Resistor().down(1.6).label('R7 100k', loc='bottom', fontsize=9)
    d += e.Dot()
    bs = d.here
    d += e.Line().at(bs).right(0.8)
    netlabel(d, d.here, 'BAT_SENSE → GP0')
    d += e.Resistor().at(bs).down(1.6).label('R8 100k\nC5 100n', loc='bottom', fontsize=9)
    d += e.Ground()

    # ======================================================= 3) ESP32-C3-Zero
    title(d, (27, 2.0), '3) Waveshare ESP32-C3-Zero  (U3)')
    left = [('5V', '(nezapojeno)'), ('GND', 'GND'), ('3V3', '3V3'), ('GP0', 'BAT_SENSE'), ('GP1', 'TFT_RST'),
            ('GP2', 'TFT_DC'), ('GP3', 'TFT_CS'), ('GP4', 'TFT_SCLK'), ('GP5', 'BTN1 MODE (wake)')]
    right = [('GP21', 'GPS RX  (TX z ESP)'), ('GP20', 'GPS TX  (RX do ESP)'), ('GP19', '–'), ('GP18', '–'),
             ('GP10', 'GPS_EN'), ('GP9', 'BTN3 PAUZA (+BOOT)'), ('GP8', 'BTN2 START'), ('GP7', 'BL_PWM'), ('GP6', 'TFT_MOSI')]
    pins = [e.IcPin(name=n, side='left') for n, _ in reversed(left)]
    pins += [e.IcPin(name=n, side='right') for n, _ in reversed(right)]
    u3 = e.Ic(pins=pins, size=(3.4, 8.2), pinspacing=0.8).at((33, -6.8)).right().label('U3  (USB-C nahoře ▲)', loc='top', fontsize=10)
    d += u3
    for n, nn in left:
        d += e.Line().at(getattr(u3, n)).left(0.6)
        netlabel(d, d.here, nn, 'left')
    for n, nn in right:
        d += e.Line().at(getattr(u3, n)).right(0.6)
        netlabel(d, d.here, nn, 'right')

    # ======================================================= 4) spinace GPS / podsviceni
    title(d, (27, -10.2), '4) Spínání GPS a podsvícení  (P-MOSFET, aktivní v LOW)')
    for i, (q, rpu, rs, out, sig) in enumerate((('Q2', 'R9 100k', 'R10 1k', 'GPS_VCC → J3', 'GPS_EN (GP10)'),
                                                 ('Q3', 'R11 100k', 'R12 1k', 'BLK → J2', 'BL_PWM (GP7)'))):
        x0, y0 = 30 + i * 11, -11.8
        netlabel(d, (x0, y0), '3V3', 'top')
        d += e.Line().at((x0, y0)).down(0.4)
        d += e.Dot()
        top = d.here
        d += e.Line().down(1.2)
        f = e.PFet(bulk=False).right().anchor('source').label(f'{q}\nAO3401A', loc='left', fontsize=9)
        d += f
        d += e.Line().at(f.drain).down(0.6)
        netlabel(d, d.here, out, 'bottom')
        d += e.Line().at(f.gate).right(1.0)
        d += e.Dot()
        g = d.here
        d += e.Resistor().at(g).right(2.2).label(rs, loc='bottom', fontsize=9)
        netlabel(d, d.here, sig, 'right')
        d += e.Resistor().at(g).up().toy(top).label(rpu, loc='bottom', fontsize=9)
        d += e.Line().tox(top)

    # ======================================================= 5) konektory + tlacitka
    title(d, (-1, -19.2), '5) Konektory a tlačítka')
    d += e.Label().at((0.2, -20.0)).label(
        'J2 displej ST7789 (2,0" / 2,4" / 2,8" IPS):  1 GND · 2 VCC=3V3 · 3 SCL=TFT_SCLK · 4 SDA=TFT_MOSI · 5 RES · 6 DC · 7 CS · 8 BLK\n'
        'J3 GPS (ATGM336H / NEO-6M):  1 VCC=GPS_VCC · 2 GND · 3 TX modulu → GP20 · 4 RX modulu ← GP21\n'
        'J5 externí (vodotěsná) tlačítka:  1 MODE · 2 START · 3 PAUZA · 4 GND      (paralelně k SW2–SW4)\n'
        'SW2 MODE → GP5 (R13 100k na 3V3, probouzí z deep sleep) · SW3 START → GP8 (R14 10k) · SW4 PAUZA → GP9 · všechna proti GND\n'
        'C8 100n blokuje GPS_VCC · USB-C na modulu ESP slouží jen k programování (pin 5V modulu není zapojen).',
        loc='right', fontsize=10, halign='left', valign='top')

    d.save(os.path.join(HERE, 'schema_zapojeni.svg'))

try:
    import cairosvg
    cairosvg.svg2png(url=os.path.join(HERE, 'schema_zapojeni.svg'), write_to=os.path.join(HERE, 'schema_zapojeni.png'),
                     output_width=2400, background_color='white')
except ImportError:
    pass
print('schema_zapojeni.svg')
