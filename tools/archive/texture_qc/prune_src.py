#!/usr/bin/env python3
"""Remove from LethalDreams_textures_src the PNGs that are already upscaled and
installed into the game (i.e. present in LethalDreams_textures_dst).

Also prunes the matching rows from MANIFEST.txt so the manifest stays consistent.
Nothing outside the src folder is touched.

Usage: python3 .tmp/prune_src.py          (delete)
       python3 .tmp/prune_src.py --dry    (list only)
"""
import os, sys


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
SRC = paths.assets_dir('LethalDreams_textures_src')
DST = paths.assets_dir('LethalDreams_textures_dst')

dry = '--dry' in sys.argv

names = sorted(f[:-4] for f in os.listdir(DST) if f.endswith('.png'))
to_del, missing = [], []
for n in names:
    p = os.path.join(SRC, n + '.png')
    if os.path.exists(p):
        to_del.append(p)
    else:
        missing.append(n)

# rows to keep in manifest (skip the deleted names, keep bmp/ rows untouched)
lines = open(os.path.join(SRC, 'MANIFEST.txt')).read().splitlines()
header, body = lines[0], lines[1:]
drop = set(names)
kept = [l for l in body if l.split('\t')[0] not in drop]

if not dry:
    for p in to_del:
        os.remove(p)
    with open(os.path.join(SRC, 'MANIFEST.txt'), 'w') as f:
        f.write(header + '\n' + '\n'.join(kept) + '\n')

print('deleting %d png; manifest rows %d -> %d; not found in src: %d %s' % (
    len(to_del), len(body), len(kept), len(missing), missing[:10]))
print('remaining png in src (btf-derived):',
      sum(1 for f in os.listdir(SRC) if f.endswith('.png')))
