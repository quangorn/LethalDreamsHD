#!/usr/bin/env python3
"""Classify dst PNGs against the game's current .btf: installed / pending.

For each PNG in LethalDreams_textures_dst: read its dims, read the game .btf dims
(decompress header), compare against the exported original dims from MANIFEST.txt.
  installed = game blob already at the PNG's size (and size == 2x original)
  pending   = game blob still at original size, PNG is 2x

import os
import sys
sys.path.insert(0, _ROOT)
import paths
Writes paths.scratch_dir()/pending.txt (names) and prints the summary.
"""
import os, sys, struct, zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

DST = paths.assets_dir('LethalDreams_textures_dst')
GAME = paths.game_path('data', 'textures')
SRC = paths.assets_dir('LethalDreams_textures_src')

man = {}
for line in open(SRC + '/MANIFEST.txt').read().splitlines()[1:]:
    n, w, h, b, blob = line.split('\t')
    if '/' in n:
        continue
    man[n] = (int(w), int(h), int(b))

def ihdr(p):
    d = open(p, 'rb').read()
    return struct.unpack('>II', d[16:24])

installed, pending, odd = [], [], []
for f in sorted(os.listdir(DST)):
    if not f.endswith('.png'):
        continue
    n = f[:-4]
    pw, ph = ihdr(DST + '/' + f)
    if n not in man:
        odd.append((n, 'not in manifest')); continue
    ow, oh, ob = man[n]
    try:
        d = U.parse_btf(open(GAME + '/' + n + '.btf', 'rb').read())
        gw, gh, bpp, P = d['dims'][0]
    except Exception as e:
        odd.append((n, 'game parse %s' % e)); continue
    if (pw, ph) != (ow * 2, oh * 2):
        odd.append((n, 'png %dx%d not 2x of %dx%d' % (pw, ph, ow, oh))); continue
    if (gw, gh) == (pw, ph):
        installed.append(n)
    elif (gw, gh) == (ow, oh):
        pending.append(n)
    else:
        odd.append((n, 'game dims %dx%d' % (gw, gh)))

with open(os.path.join(paths.scratch_dir(), 'pending.txt'), 'w') as f:
    f.write('\n'.join(pending) + '\n')
print('dst pngs: installed %d, pending %d, odd %d' % (len(installed), len(pending), len(odd)))
for n, e in odd:
    print('  ODD %s: %s' % (n, e))
