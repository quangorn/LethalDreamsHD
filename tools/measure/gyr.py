"""gyr.py (tools/measure/) - Радиус инерции (std) чернил строки в canvas-единицах — устойчив к AA и порогу.
  python3 tools/measure/gyr.py <png> sx dx sy y0 y1
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
f = sys.argv[1]; sx = float(sys.argv[2]); dx = float(sys.argv[3]); sy = float(sys.argv[4])
y0, y1 = int(sys.argv[5]), int(sys.argv[6])
W, H, rows = decode_png(f)
sw = sx_ = 0.0
sx_ = 0.0; sxx = 0.0; s2 = 0.0
for py in range(y0, min(y1, H)):
    r = rows[py]
    for px in range(W):
        p = px * 3
        v = max(r[p], r[p+1], r[p+2])
        w = v - 40 if v > 40 else 0
        if not w: continue
        x = (px - dx) / sx
        sx_ += w; sxx += w * x; s2 += w * x * x
if sx_ <= 0:
    print('no ink'); raise SystemExit
m = sxx / sx_
var = s2 / sx_ - m * m
print('cx=%.2f sd=%.2f' % (m, var ** 0.5))
