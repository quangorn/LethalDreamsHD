#!/usr/bin/env python3
"""Convert plain .bmp resources of Lethal Dreams (data/wrender/figures) to PNG.

Header bitness in these files is bogus: pixel data length decides the layout.
Channel order is plain RGB (verified on water tiles: RGB gives teal, BGR olive),
rows stored bottom-up (standard BMP) -> PNG written top-down.
Extra trailing bytes after the pixel block are ignored.
"""
import os, struct, sys

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

SRC = paths.assets_dir('LethalDreams_textures_src')
BASE = paths.game_path('data', 'wrender', 'figures')

def bmp_decode(path):
    d = open(path, 'rb').read()
    off = struct.unpack('<I', d[10:14])[0]
    w, h = struct.unpack('<ii', d[18:26])
    n = len(d) - off
    pxpp = 3 if n >= w * h * 3 else 2
    data = d[off:off + w * h * pxpp]
    rows = []
    for y in range(h):
        seg = data[y * w * pxpp:(y + 1) * w * pxpp]
        out = bytearray(w * 4)
        if pxpp == 3:
            out[0::4] = seg[0::3]; out[1::4] = seg[1::3]; out[2::4] = seg[2::3]
        else:
            out[0::4] = seg[0::2]; out[1::4] = seg[1::2]; out[2::4] = seg[1::2]
        out[3::4] = b'\xff' * w
        rows.append(bytes(out))
    rows.reverse()          # BMP bottom-up -> PNG top-down
    return w, h, rows, pxpp

made, failed = [], []
new_lines = []
for sub in ('borders', 'textures'):
    d = os.path.join(BASE, sub)
    if not os.path.isdir(d):
        continue
    outdir = os.path.join(SRC, 'bmp', sub)
    os.makedirs(outdir, exist_ok=True)
    for fn in sorted(os.listdir(d)):
        if not fn.lower().endswith('.bmp'):
            continue
        stem = fn[:-4]
        try:
            w, h, rows, pxpp = bmp_decode(os.path.join(d, fn))
            U.png_write(os.path.join(outdir, stem + '.png'), w, h, rows)
            made.append('%s/%s' % (sub, stem))
            new_lines.append('bmp/%s/%s\t%d\t%d\t%d\t%d' % (sub, stem, w, h, pxpp, w * h * pxpp))
        except Exception as e:
            failed.append((sub + '/' + fn, repr(e)))

# rebuild MANIFEST.txt: keep the .btf lines, drop old bmp lines, append the fixed ones
mp = os.path.join(SRC, 'MANIFEST.txt')
keep = [l for l in open(mp).read().splitlines() if not l.startswith('bmp/')]
with open(mp, 'w') as f:
    f.write('\n'.join(keep) + '\n' + '\n'.join(new_lines) + '\n')

print('bmp->png: %d' % len(made))
for sub in ('borders', 'textures'):
    print('  %s: %d' % (sub, sum(1 for m in made if m.startswith(sub + '/'))))
print('failed: %d' % len(failed))
for f, e in failed:
    print('  FAIL %s: %s' % (f, e))
