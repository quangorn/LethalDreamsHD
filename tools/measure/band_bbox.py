#!/usr/bin/env python3
"""band_bbox.py (tools/measure/) - bbox белых пикселей (текст) в полосе строк y0..y1 снимка.

  python3 tools/measure/band_bbox.py k4rc_step2.png 55 75
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
    y0, y1 = int(sys.argv[2]), int(sys.argv[3])
    w, h, rows = decode_png(path)
    minx, maxx, n = w, -1, 0
    for y in range(y0, min(y1, h)):
        r = rows[y]
        for x in range(w):
            if min(r[x * 3], r[x * 3 + 1], r[x * 3 + 2]) > 150:
                n += 1
                if x < minx:
                    minx = x
                if x > maxx:
                    maxx = x
    print('%s band y %d..%d: white n=%d  x %d..%d  center %.1f' % (path, y0, y1, n, minx, maxx, (minx + maxx) / 2.0))


if __name__ == '__main__':
    main()
