#!/usr/bin/env python3
"""State scan: canonical header dims vs the installed .btf.

Canonical dims: w,h = the first two u32 fields after the file-name field of the payload,
bpp = blob_len / (w*h), blob_len = the u32 field at the end of the header (hdr_end = plen -
blob_len).  parse_btf instead scans all 4-aligned pairs and prefers a SQUARE pair, so for
w != h it can pick a spurious square (blob fits both 512x128 bpp4 and 256x256 bpp4), which
was used for export and packing.

Class per name (original backup vs current game file):
  untouched        game blob == canon blob, dims == canon
  installed-ok     game blob == 4*canon blob, dims == canon*2   (correct 2x install)
  BROKEN-dims      game blob == 4*canon blob, dims == canon     (blob upscaled, w,h not patched)
  BROKEN-other     any other combination

import os
import sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
Writes paths.scratch_dir()/broken.txt (names to redo) and full report.
"""
import os, sys, struct, zlib, datetime

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

def canon(path):
    pl = payload_of(path)
    plen = len(pl)
    pos = 4
    while pos + 8 <= plen:
        L = u32(pl, pos)
        if 3 <= L <= 60 and pos + 4 + L <= plen and all(32 <= c < 127 for c in pl[pos+4:pos+4+L]):
            pos += 4 + L
            break
        pos += 4
    w, h = u32(pl, pos), u32(pl, pos+4)
    blob_len = None
    for i in range(min(plen, 4096) - 4, 3, -1):
        v = u32(pl, i)
        if v > 0 and i + 4 == plen - v:
            blob_len = v
            break
    if not w or not h or blob_len is None or blob_len % (w*h):
        return None
    return (w, h, blob_len // (w*h), blob_len)

rows, classes, broken = [], {}, []
for f in sorted(os.listdir(ORIG)):
    if not f.endswith('.btf'):
        continue
    n = f[:-4]
    if n.endswith('_43') or n.endswith('_169'):
        classes['sidecar'] = classes.get('sidecar', 0) + 1
        continue
    c = canon(ORIG + '/' + f)
    gp = GAME + '/' + f
    if not os.path.exists(gp):
        classes['missing-in-game'] = classes.get('missing-in-game', 0) + 1
        continue
    g = canon(gp)
    mt = datetime.date.fromtimestamp(os.stat(gp).st_mtime).isoformat()
    if c is None:
        rows.append('%s\tcanon-unparsable' % n)
        classes['canon-unparsable'] = classes.get('canon-unparsable', 0) + 1
        continue
    if g is None:
        rows.append('%s\tgame-unparsable\t%s' % (n, mt))
        classes['game-unparsable'] = classes.get('game-unparsable', 0) + 1
        broken.append(n)
        continue
    w, h, bpp, blob = c
    gw, gh, gbpp, gblob = g
    if (gw, gh) == (w, h) and gblob == blob:
        cls = 'untouched'
    elif (gw, gh) == (w*2, h*2) and gblob == 4*blob and gbpp == bpp:
        cls = 'installed-ok'
    elif (gw, gh) == (w, h) and gblob == 4*blob:
        cls = 'BROKEN-dims'
        broken.append(n)
    else:
        cls = 'BROKEN-other game %dx%d blob %d bpp %d vs canon %dx%d blob %d bpp %d' % (gw, gh, gblob, gbpp, w, h, blob, bpp)
        broken.append(n)
    rows.append('%s\tcanon %dx%d bpp=%d blob=%d\tgame %dx%d bpp=%d blob=%d\t%s\t%s' % (n, w, h, bpp, blob, gw, gh, gbpp, gblob, cls, mt))
    key = cls.split(' ')[0]
    classes[key] = classes.get(key, 0) + 1

with open('paths.scratch_dir()/broken_report.txt', 'w') as f:
    f.write('\n'.join(rows) + '\n')
with open(os.path.join(paths.scratch_dir(), 'broken.txt'), 'w') as f:
    f.write('\n'.join(sorted(broken)) + '\n')
print('scanned:', len(rows))
for k, v in sorted(classes.items(), key=lambda kv: -kv[1]):
    print('  %-18s %d' % (k, v))
print('broken names:', len(broken))
for n in sorted(broken):
    print('  ', n)
