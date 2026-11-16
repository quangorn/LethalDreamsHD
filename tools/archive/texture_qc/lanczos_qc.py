#!/usr/bin/env python3
"""QC of Lanczos-2x upscales for the textures that Upscayl could not handle.

For each pair (original in src/tmp, upscale in dst):
  - dims must be exactly 2x
  - alpha mass ratio: sum(alpha up) / 4 vs sum(alpha orig)
  - coverage (alpha > 16) ratio up vs orig*4
  - box-downscale the upscale and compare to original: mean |dRGB| over ink
    (alpha>16) pixels, mean |dAlpha|
"""
import os, sys

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
from pack_btf import png_decode

SRC = os.path.join(paths.assets_dir('LethalDreams_textures_src'), 'tmp')
DST = paths.assets_dir('LethalDreams_textures_dst')

names = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith('.png'))
print('%-22s %-10s %-8s %-8s %-8s %-8s %-8s' % ('name', 'orig->up', 'mass', 'cov', 'dRGB', 'dA', 'flatOK'))
for n in names:
    ow, oh, orows = png_decode(SRC + '/' + n + '.png')
    p = DST + '/' + n + '.png'
    if not os.path.exists(p):
        print('%-22s missing in dst' % n); continue
    uw, uh, urows = png_decode(p)
    dims = '%dx%d->%dx%d' % (ow, oh, uw, uh)
    if (uw, uh) != (ow * 2, oh * 2):
        print('%-22s %s  BAD DIMS' % (n, dims)); continue
    oa = [r[3::4] for r in orows]
    ua = [r[3::4] for r in urows]
    mass_u = sum(sum(r) for r in ua) / 4.0
    mass_o = sum(sum(r) for r in oa)
    cov_u = sum(1 for r in ua for a in r if a > 16)
    cov_o = sum(1 for r in oa for a in r if a > 16)
    # box downscale upscale (2x2 average)
    drgb = 0.0; da = 0.0; ink = 0
    for y in range(oh):
        for x in range(ow):
            block = [urows[2 * y + dy][4 * (2 * x + dx):4 * (2 * x + dx) + 4] for dy in (0, 1) for dx in (0, 1)]
            rb = sum(b[0] for b in block) / 4.0
            gb = sum(b[1] for b in block) / 4.0
            bb = sum(b[2] for b in block) / 4.0
            ab = sum(b[3] for b in block) / 4.0
            pr = orows[y][4 * x:4 * x + 4]
            if pr[3] > 16:
                drgb += abs(rb - pr[0]) + abs(gb - pr[1]) + abs(bb - pr[2])
                ink += 1
            da += abs(ab - pr[3])
    drgb = drgb / max(ink, 1)
    da = da / (ow * oh)
    # flat check for constant originals (white.png): variance of upscale
    flat = '-'
    if len(set(orows[0])) == 1 and all(r == orows[0] for r in orows):
        uniq = set()
        for r in ua:
            uniq.update(r)
        flat = 'flat' if len(uniq) == 1 else 'ring %d vals, spread %d' % (len(uniq), max(uniq) - min(uniq))
    print('%-22s %-12s %.4f  %.4f  %5.2f  %5.2f  %s' % (n, dims, mass_u / mass_o, cov_u / (cov_o * 4), drgb, da, flat))
