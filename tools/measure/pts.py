#!/usr/bin/env python3
"""pts.py (tools/measure/) - цвета пикселей снимка по экранным координатам (x y x y ...).

  python3 tools/measure/pts.py k4rc_step2.png 840 1000 835 1000
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402

def main():
    path = sys.argv[1]
    pts = [int(v) for v in sys.argv[2:]]
    w, h, rows = decode_png(path)
    print('%s size %dx%d' % (path, w, h))
    for i in range(0, len(pts), 2):
        x, y = pts[i], pts[i + 1]
        if x < 0 or x >= w or y < 0 or y >= h:
            print('  (%d,%d) вне' % (x, y))
            continue
        r = rows[y]
        print('  (%4d,%4d) = %3d %3d %3d' % (x, y, r[x*3], r[x*3+1], r[x*3+2]))

if __name__ == '__main__':
    main()
