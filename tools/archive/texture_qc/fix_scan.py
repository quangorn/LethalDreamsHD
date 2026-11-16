#!/usr/bin/env python3
"""Find every texture exported with the wrong dims.

Canonical dims are read from the header: the two u32 right after the file-name field
(w, h), with bpp = blob_len / (w*h).  parse_btf instead scans all 4-aligned pairs of the
header and prefers a SQUARE pair, so for w != h it can pick a spurious square pair
(262144 bytes -> both 512x128 bpp4 and 256x256 bpp4 fit) and the export used that.

For every .btf in the original backup:
  canon   = header dims of the original file
  game    = header dims of the current game file (installed 2x, or untouched original)
  classes: 'square-export-mismatch' (needs re-export), 'game-not-2x-of-canon', 'sidecar',
  'bpp-not-integer', 'no-game-file'

import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
Writes paths.scratch_dir()/affected.txt (names needing re-export) and a full report.
"""
import os, sys, struct, zlib

ORIG = paths.assets_dir('LethalDreams_textures_original_btf2x')
GAME = paths.game_path('data', 'textures')

def u32(b, i):
    return struct.unpack('<I', b[i:i+4])[0]

def payload_of(path):
    d = open(path, 'rb').read()
    if d.startswith(b'LDPACKER'):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = d.find(m, 8)
            if i >= 0:
                zi = i
                break
        return zlib.decompress(d[zi:])
    return d

def hdr(path):
    pl = payload_of(path)
    plen = len(pl)
    pos = 4
    name = None
    while pos + 8 <= plen:
        L = u32(pl, pos)
        if 3 <= L <= 60 and pos + 4 + L <= plen and all(32 <= c < 127 for c in pl[pos+4:pos+4+L]):
            name = pl[pos+4:pos+4+L].decode('ascii')
            pos += 4 + L
            break
        pos += 4
    w, h, a, b = u32(pl, pos), u32(pl, pos+4), u32(pl, pos+8), u32(pl, pos+12)
    blob_len = u32(pl, pos+36)
    if blob_len <= 0 or blob_len > plen:
        return None
    return (w, h, blob_len // (w*h) if w and h else 0, blob_len, a, b, name)

rows = []
affected, odd = [], []
for f in sorted(os.listdir(ORIG)):
    if not f.endswith('.btf'):
        continue
    n = f[:-4]
    if n.endswith('_43') or n.endswith('_169'):
        rows.append('%s\tsidecar' % n)
        continue
    o = hdr(ORIG + '/' + f)
    g = hdr(GAME + '/' + f) if os.path.exists(GAME + '/' + f) else None
    if o is None:
        rows.append('%s\torig-header-unparsable' % n)
        continue
    w, h, bpp, blob, a, b, tga = o
    if blob % (w*h) != 0:
        rows.append('%s\tnon-integer-bpp %dx%d blob=%d' % (n, w, h, blob))
        odd.append((n, 'non-integer bpp'))
        continue
    if g is None:
        rows.append('%s\tmissing-in-game' % n)
        odd.append((n, 'missing in game'))
        continue
    gw, gh, gbpp, gblob, ga, gb, gtga = g
    if (gw, gh) == (w, h):
        cls = 'original-in-game'
    elif (gw, gh) == (w*2, h*2):
        cls = 'installed-2x-ok'
    else:
        cls = 'AFFECTED game=%dx%d canon=%dx%d' % (gw, gh, w, h)
        affected.append(n)
    rows.append('%s\tcanon %dx%d bpp=%d\tgame %dx%d\t%s' % (n, w, h, bpp, gw, gh, cls))

with open(os.path.join(paths.scratch_dir(), 'affected_report.txt'), 'w') as f:
    f.write('\n'.join(rows) + '\n')
with open(os.path.join(paths.scratch_dir(), 'affected.txt'), 'w') as f:
    f.write('\n'.join(affected) + '\n')
print('originals scanned:', len(rows))
print('AFFECTED (game dims not 2x of canonical header dims):', len(affected))
for n in affected:
    print('  ', n)
print('odd:', odd)
