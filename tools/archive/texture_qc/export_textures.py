#!/usr/bin/env python3
"""Export original Lethal Dreams .btf textures to PNG for upscale.

- source: config[game.install_dir]/data/textures/*.btf (read-only)
- skips sidecar files (*_43.btf, *_169.btf) and base textures that have sidecars
- canonical decode per NOTES: top-down, RGB/RGBA channel order (not BGRA)
- writes <name>.png + MANIFEST.txt (name, w, h, bpp, blob bytes)
"""
import os, sys


sys.path.insert(0, _ROOT)
import paths
SRC = paths.game_path('data', 'textures')
OUT = paths.assets_dir('LethalDreams_textures_src')
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

def stem(fn):
    return fn[:-4]

files = sorted(f for f in os.listdir(SRC) if f.endswith('.btf'))

# sidecar files present in the game dir
sidecars = set(f for f in files if f.endswith('_43.btf') or f.endswith('_169.btf'))
bases_with_sidecar = set(stem(f)[:-3] for f in sidecars)  # strip _43/_169

made, skipped, failed = [], [], []
os.makedirs(OUT, exist_ok=True)
manifest = []

for fn in files:
    base = stem(fn)
    if fn.endswith('_43.btf') or fn.endswith('_169.btf'):
        skipped.append((fn, 'sidecar file'))
        continue
    if base in bases_with_sidecar:
        skipped.append((fn, 'has sidecar variants (%s_43/_169)' % base))
        continue
    path = os.path.join(SRC, fn)
    try:
        d = U.parse_btf(open(path, 'rb').read())
        w, h, bpp, P = d['dims'][0]
        blob = d['blob']
        if bpp == 4:
            rows = U.decode32(blob, w, h, 'RGBA', False)
        elif bpp == 3:
            rows = U.decode24(blob, w, h, 'RGB', False)
        elif bpp == 1:
            # indexed with embedded palettes -> pal 0
            rows = U.decode_indexed(blob, w, h, 0, False)
        else:
            raise ValueError('no matching dims for blob %d' % d['blob_len'])
        outp = os.path.join(OUT, base + '.png')
        U.png_write(outp, w, h, rows)
        made.append(base)
        manifest.append('%s\t%d\t%d\t%d\t%d' % (base, w, h, bpp, d['blob_len']))
    except Exception as e:
        failed.append((fn, repr(e)))

with open(os.path.join(OUT, 'MANIFEST.txt'), 'w') as f:
    f.write('name\twidth\theight\tbpp\tblob_bytes\n')
    f.write('\n'.join(manifest) + '\n')

print('exported: %d' % len(made))
print('skipped (sidecar): %d' % len(skipped))
for fn, why in skipped:
    print('  skip %s: %s' % (fn, why))
print('failed: %d' % len(failed))
for fn, why in failed:
    print('  FAIL %s: %s' % (fn, why))
print('out: %s' % OUT)
