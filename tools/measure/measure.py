"""measure.py (tools/measure/) - Ink bbox / metrics of a region.
  python3 tools/measure/measure.py <png> x0 y0 w h [thr]
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
thr = int(sys.argv[6]) if len(sys.argv) > 6 else 110
W, H, rows = decode_png(f)
x1 = x0 + w; y1 = y0 + h
minx, maxx, miny, maxy = 10**9, -1, 10**9, -1
for y in range(max(0, y0), min(H, y1)):
    r = rows[y]
    for x in range(max(0, x0), min(W, x1)):
        p = x * 3
        if max(r[p], r[p + 1], r[p + 2]) >= thr:
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y
print("%s: x %d..%d (w=%d)  y %d..%d (h=%d)" % (f.split('\\')[-1], minx, maxx,
      maxx - minx + 1 if maxx >= 0 else 0, miny, maxy, maxy - miny + 1 if maxy >= 0 else 0))
