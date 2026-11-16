"""Полосы текста в регионе (tools/measure/lines.py): строки (в пикселях)
  python3 tools/measure/lines.py <png> x0 y0 w h thr
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
thr = int(sys.argv[6]) if len(sys.argv) > 6 else 150
W, H, rows = decode_png(f)
y1 = min(H, y0 + h); x1 = min(W, x0 + w)
prof = []
for y in range(max(0, y0), y1):
    r = rows[y]; n = 0
    for x in range(max(0, x0), x1):
        p = x * 3
        if max(r[p], r[p+1], r[p+2]) >= thr:
            n += 1
    prof.append((y, n))
on = []
run = None
for y, n in prof:
    if n >= 3:
        if run is None: run = [y, y]
        else: run[1] = y
    else:
        if run: on.append(tuple(run)); run = None
if run: on.append(tuple(run))
print('%s: %d bands' % (f.split('/')[-1], len(on)))
prev = None
for a, b in on:
    d = '' if prev is None else ' step=%d' % (a - prev)
    print('  y %d..%d h=%d%s' % (a, b, b - a + 1, d))
    prev = a
