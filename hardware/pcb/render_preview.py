#!/usr/bin/env python3
"""Realisticky nahled desky (zelena maska, zlate plosky, bily potisk) z KiCad vrstev.
Pouziti: python3 render_preview.py   -> build/CykloPCB_v1_preview_{top,bottom}.png
Potrebuje: kicad-cli, pip install cairosvg pillow numpy"""
import io
import os
import subprocess
import tempfile

import cairosvg
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(HERE, 'CykloPCB_v1.kicad_pcb')
OUT = os.path.join(HERE, 'build')
WIDTH = 900

FR4 = (40, 70, 40)          # deska bez medi pod maskou
MASK_CU = (30, 120, 60)     # med pod zelenou maskou
GOLD = (205, 170, 90)       # odkryte plosky (ENIG/HASL)
SILK = (245, 245, 240)
HOLE = (20, 20, 20)
BG = (255, 255, 255)


def layer_mask(layers, mirror=False, drill=0):
    with tempfile.TemporaryDirectory() as td:
        svg = os.path.join(td, 'l.svg')
        cmd = ['kicad-cli', 'pcb', 'export', 'svg', '--output', svg, '--layers', layers, '--black-and-white',
               '--exclude-drawing-sheet', '--page-size-mode', '2', '--drill-shape-opt', str(drill)]
        if mirror:
            cmd.append('--mirror')
        subprocess.run(cmd + [PCB], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        png = cairosvg.svg2png(url=svg, output_width=WIDTH, background_color='white')
    im = np.array(Image.open(io.BytesIO(png)).convert('L'))
    return im < 128   # True = je tam kresba


def board_area(edge):
    """vyplni obrys desky (Edge.Cuts) - flood fill z rohu = mimo desku"""
    from collections import deque
    h, w = edge.shape
    outside = np.zeros_like(edge)
    q = deque([(0, 0), (0, w - 1), (h - 1, 0), (h - 1, w - 1)])
    while q:
        y, x = q.popleft()
        if 0 <= y < h and 0 <= x < w and not outside[y, x] and not edge[y, x]:
            outside[y, x] = True
            q.extend(((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)))
    return ~outside


def render(side):
    m = side == 'bottom'
    p = 'B' if m else 'F'
    edge = layer_mask('Edge.Cuts', m)
    cu = layer_mask(f'{p}.Cu', m)
    mask_open = layer_mask(f'{p}.Mask', m)
    silk = layer_mask(f'{p}.SilkS', m)
    # vrtani: plosky s otvorem (drill=2) maji uprostred "diru" -> rozdil proti plnym ploskam
    holes = cu & ~layer_mask(f'{p}.Cu', m, drill=2)
    holes |= mask_open & ~layer_mask(f'{p}.Mask', m, drill=2)
    inside = board_area(edge)
    img = np.zeros(edge.shape + (3,), np.uint8)
    img[:] = BG
    img[inside] = FR4
    img[inside & cu] = MASK_CU
    img[inside & mask_open & cu] = GOLD
    img[inside & silk & ~mask_open] = SILK
    img[inside & holes] = HOLE
    img[edge] = (90, 90, 90)
    out = os.path.join(OUT, f'CykloPCB_v1_preview_{side}.png')
    Image.fromarray(img).save(out)
    print(out)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    render('top')
    render('bottom')
