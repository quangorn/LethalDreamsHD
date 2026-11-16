"""Полосы текста -> bbox каждой строки в КАНВАСНЫХ единицах.
   tools/measure/textlines.py
   python3 tools/measure/textlines.py <png> x0 y0 w h sx dx sy thr
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402

f = sys.argv[1]
x0, y0, w, h = (int(v) for v in sys.argv[2:6])
sx = float(sys.argv[6]); dx = float(sys.argv[7]); sy = float(sys.argv[8])
thr = int(sys.argv[9])
W, H, rows = decode_png(f)
x1 = min(W, x0 + w); y1 = min(H, y0 + h)
prof = []
for y in range(max(0, y0), y1):
    r = rows[y]; n = 0
    for x in range(max(0, x0), x1):
        p = x * 3
        if max(r[p], r[p+1], r[p+2]) >= thr:
            n += 1
    prof.append((y, n))
on = []; run = None
for y, n in prof:
    if n >= 4:
        if run is None: run = [y, y]
        else: run[1] = y
    else:
        if run: on.append(tuple(run)); run = None
if run: on.append(tuple(run))
print('%s: %d bands' % (f.split('/')[-1], len(on)))
prev = None
for a, b in on:
    mny, mxx = 10**9, -1
    for y in range(a, b + 1):
        r = rows[y]
        for x in range(max(0, x0), x1):
            p = x * 3
            if max(r[p], r[p+1], r[p+2]) >= thr:
                if x < mny: mny = x
                if x > mxx: mxx = x
    cy = (a + b) / 2.0
    print('  y %6.1f..%6.1f (h=%4.1f)  x %6.1f..%6.1f (w=%5.1f)  cx=%6.1f  step=%s' % (
        (a - 0) / sy, (b) / sy, (b - a + 1) / sy,
        (mny - dx) / sx, (mxx - dx) / sx, (mxx - mny + 1) / sx,
        ((mny + mxx) / 2.0 - dx) / sx,
        '-' if prev is None else '%5.1f' % ((a - prev) / sy)))
    prev = a
