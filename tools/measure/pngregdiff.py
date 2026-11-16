"""Region diff of two PNGs: mean/max delta + heat map of where changes are.

File: tools/measure/pngregdiff.py
Usage: python3 tools/measure/pngregdiff.py <a.png> <b.png> [tol] [x0 y0 w h]
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402

def main():
    a, b = sys.argv[1], sys.argv[2]
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    wa, ha, ra = decode_png(a)
    wb, hb, rb = decode_png(b)
    if (wa, ha) != (wb, hb):
        print('размеры разные'); return
    x0, y0, w, h = (int(v) for v in sys.argv[4:8]) if len(sys.argv) > 7 else (0, 0, wa, ha)
    w = min(w, wa - x0); h = min(h, ha - y0)
    rows, cols = 16, 16
    ch = max(1, h // rows); cw = max(1, w // cols)
    grid = [[0] * cols for _ in range(rows)]
    sumd = 0; over = 0; mx = 0
    for y in range(y0, y0 + h):
        A = ra[y]; B = rb[y]
        ry = min(rows - 1, (y - y0) // ch)
        for x in range(x0, x0 + w):
            p = x * 3
            dd = max(abs(A[p] - B[p]), abs(A[p+1] - B[p+1]), abs(A[p+2] - B[p+2]))
            sumd += abs(A[p] - B[p]) + abs(A[p+1] - B[p+1]) + abs(A[p+2] - B[p+2])
            if dd > mx: mx = dd
            if dd > tol:
                over += 1
                grid[ry][min(cols - 1, (x - x0) // cw)] += 1
    print('region %dx%d+%d+%d  meanDelta=%.4f  maxDelta=%d  over_tol=%d' %
          (w, h, x0, y0, sumd / float(w * h * 3), mx, over))
    for r in range(rows):
        print(' '.join('%4d' % grid[r][c] for c in range(cols)))

if __name__ == '__main__':
    main()
