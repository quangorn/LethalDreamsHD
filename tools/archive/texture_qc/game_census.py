#!/usr/bin/env python3
"""Census of data/textures vs the original sizes.

Reference original sizes come from two sources:
  - MANIFEST.txt of the export (names that are still un-upscaled),
  - half the dims of the dst PNGs (the upscaled ones, whose manifest rows were pruned).
Classes: installed-2x / original-size / sidecar / unparsable / other.
"""
import os, sys, struct, datetime

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

GAME = paths.game_path('data', 'textures')
SRC = paths.assets_dir('LethalDreams_textures_src')
DST = paths.assets_dir('LethalDreams_textures_dst')

orig = {}
for line in open(SRC + '/MANIFEST.txt').read().splitlines()[1:]:
    n, w, h, b, blob = line.split('\t')
    if '/' in n:
        continue
    orig[n] = (int(w), int(h), int(b))

def ihdr(p):
    d = open(p, 'rb').read()
    return struct.unpack('>II', d[16:24])

dst_png = {}
for f in sorted(os.listdir(DST)):
    if f.endswith('.png'):
        w, h = ihdr(DST + '/' + f)
        dst_png[f[:-4]] = (w, h)
for n, (w, h) in dst_png.items():
    orig.setdefault(n, (w // 2, h // 2, 0))

rows = []
counts = {}
installed = []
for f in sorted(os.listdir(GAME)):
    if not f.endswith('.btf'):
        continue
    n = f[:-4]
    mt = datetime.date.fromtimestamp(os.stat(GAME + '/' + f).st_mtime).isoformat()
    try:
        d = U.parse_btf(open(GAME + '/' + f, 'rb').read())
        w, h, bpp, P = d['dims'][0]
        bl = d['blob_len']
    except Exception as e:
        rows.append('%s\tERR\t%s\t%s' % (n, mt, repr(e)))
        counts['unparsable'] = counts.get('unparsable', 0) + 1
        continue
    sidecar = n.endswith('_43') or n.endswith('_169')
    o = orig.get(n)
    if o is None:
        cls = 'sidecar' if sidecar else 'no-reference'
    elif (w, h) == o[:2]:
        cls = 'original-size'
    elif w == o[0] * 2 and h == o[1] * 2:
        cls = 'installed-2x'
        installed.append(n)
    else:
        cls = 'other'
    rows.append('%s\t%dx%d bpp=%d blob=%d\t%s\t%s' % (n, w, h, bpp, bl, mt, cls))
    counts[cls] = counts.get(cls, 0) + 1

with open(os.path.join(paths.scratch_dir(), 'game_census.txt'), 'w') as f:
    f.write('\n'.join(rows) + '\n')
print('total .btf:', len(rows))
for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
    print('  %-16s %d' % (k, v))
dst_names = set(dst_png)
print('dst pngs:', len(dst_names))
print('installed-2x in game:', len(installed))
print('dst & installed:', len(dst_names & set(installed)))
pending = sorted(n for n in dst_names if n not in set(installed))
print('dst pending (game at original size):', len(pending))
print('sample:', pending[:15])
with open(os.path.join(paths.scratch_dir(), 'pending.txt'), 'w') as f:
    f.write('\n'.join(pending) + '\n')
