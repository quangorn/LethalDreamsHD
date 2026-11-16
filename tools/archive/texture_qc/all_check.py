#!/usr/bin/env python3
"""Whole-game verification after the dims fix.

Per .btf in the original backup:
  canon(ORIG) = header w,h,bpp,blob ; canon(GAME) = same for the current game file
  classes: sidecar / canon-unparsable / untouched-original / DIMS-MISMATCH / dims-ok
  for dims-ok names with a matching PNG in LethalDreams_textures_dst:
     game blob must equal bytes built from that PNG at 2x dims (byte-for-byte)

import os
import sys
sys.path.insert(0, _ROOT)
import paths
Writes paths.scratch_dir()/all_check.txt.
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U
import pack_btf as P

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
GAME = paths.game_path('data', 'textures')
DST = paths.assets_dir('LethalDreams_textures_dst')

def payload_of(path):
    d = open(path, 'rb').read()
    if d.startswith(b'LDPACKER'):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = d.find(m, 8)
            if i >= 0:
                zi = i
                break
        if zi < 0:
            return d
        return zlib.decompress(d[zi:])
    return d

dst_names = set(f[:-4] for f in os.listdir(DST) if f.endswith('.png'))
rows = []
cls = {}
byteok, bytebad, nodata = 0, [], []
for f in sorted(os.listdir(ORIG)):
    if not f.endswith('.btf'):
        continue
    n = f[:-4]
    if n.endswith('_43') or n.endswith('_169'):
        cls['sidecar'] = cls.get('sidecar', 0) + 1
        continue
    c = U.parse_btf(open(ORIG + '/' + f, 'rb').read())['canon']
    g = U.parse_btf(open(GAME + '/' + f, 'rb').read())['canon']
    if c is None:
        cls['canon-unparsable'] = cls.get('canon-unparsable', 0) + 1
        rows.append('%s\tcanon-unparsable' % n)
        continue
    w0, h0, bpp, blob0 = c[0], c[1], c[2], c[3]
    if g is None:
        cls['game-unparsable'] = cls.get('game-unparsable', 0) + 1
        rows.append('%s\tgame-unparsable' % n)
        continue
    gw, gh, gbpp, gblob = g[0], g[1], g[2], g[3]
    if (gw, gh) == (w0, h0) and gblob == blob0:
        cls['untouched-original'] = cls.get('untouched-original', 0) + 1
        rows.append('%s\tuntouched %dx%d' % (n, w0, h0))
        continue
    if (gw, gh) != (w0*2, h0*2) or gblob != 4*blob0 or gbpp != bpp:
        cls['DIMS-MISMATCH'] = cls.get('DIMS-MISMATCH', 0) + 1
        rows.append('%s\tDIMS game %dx%d blob %d vs canon %dx%d blob %d' % (n, gw, gh, gblob, w0, h0, blob0))
        continue
    cls['dims-ok'] = cls.get('dims-ok', 0) + 1
    if n in dst_names:
        pw, ph, px = P.png_decode(DST + '/' + n + '.png')
        exp = P.build_blob(px, pw, ph, bpp)
        pl = payload_of(GAME + '/' + f)
        blob = pl[len(pl)-gblob:]
        if blob == exp and (pw, ph) == (gw, gh):
            byteok += 1
            rows.append('%s\tdims-ok bytes-ok %dx%d' % (n, pw, ph))
        else:
            bytebad.append((n, len(blob), len(exp), (pw, ph), (gw, gh)))
            rows.append('%s\tdims-ok BYTES-DIFFER blob %d exp %d png %dx%d game %dx%d' % (n, len(blob), len(exp), pw, ph, gw, gh))
    else:
        nodata.append(n)
        rows.append('%s\tdims-ok no-png-to-compare %dx%d' % (n, gw, gh))

with open(os.path.join(paths.scratch_dir(), 'all_check.txt'), 'w') as f:
    f.write('\n'.join(rows) + '\n')
print('classes:', sorted(cls.items(), key=lambda kv: -kv[1]))
print('byte-verified vs dst PNG: %d' % byteok)
print('byte mismatches: %d' % len(bytebad))
for t in bytebad[:20]:
    print('   ', t)
print('dims-ok without a dst PNG (batch 1, pngs deleted): %d' % len(nodata))
print('sample:', nodata[:12])
