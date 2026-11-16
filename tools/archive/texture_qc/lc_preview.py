#!/usr/bin/env python3
"""Nearest-zoom side-by-side preview: original (left) vs upscale (right)."""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
from pack_btf import png_decode
import unpack_btf as U

SRC = os.path.join(paths.assets_dir('LethalDreams_textures_src'), 'tmp')
DST = paths.assets_dir('LethalDreams_textures_dst')
Z = 8
GAP = 6

def scale_nearest(rows, w, h, z):
    out = []
    for r in rows:
        line = bytearray()
        for x in range(w):
            line += r[x * 4:x * 4 + 4] * z
        out.extend([bytes(line)] * z)
    return out

names = ['minimap_pt02', 'minimap_pt03', 'minimap_pt04', 'water_rings01']
for n in names:
    ow, oh, orows = png_decode(SRC + '/' + n + '.png')
    uw, uh, urows = png_decode(DST + '/' + n + '.png')
    a = scale_nearest(orows, ow, oh, Z)
    b = scale_nearest(urows, uw, uh, Z)
    W = ow * Z + GAP + uw * Z
    H = max(len(a), len(b))
    canvas = [bytes([20, 20, 20, 255] * W) for _ in range(H)]
    for i, r in enumerate(a):
        row = bytearray(canvas[i])
        row[0:ow * Z * 4] = r
        canvas[i] = bytes(row)
    for i, r in enumerate(b):
        row = bytearray(canvas[i])
        row[ow * Z * 4 + GAP * 4:W * 4] = r
        canvas[i] = bytes(row)
    U.png_write(os.path.join(paths.scratch_dir(), 'lc_%s.png') % n, W, H, canvas)
    print('wrote lc_%s.png (%dx%d): left %dx%d orig, right %dx%d upscale' % (n, W, H, ow, oh, uw, uh))
