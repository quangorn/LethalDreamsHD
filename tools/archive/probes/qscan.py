#!/usr/bin/env python3
"""qscan.py - границы help/Q-квадрата на снимке: переходы цвета по X в заданных строках.

  python3 qscan.py k4rc_step2.png 300 1080 1900
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'measure'))
from pngdiff import decode_png  # noqa: E402


def main():
    path = sys.argv[1]
    ys = [int(v) for v in sys.argv[2:]]
    w, h, rows = decode_png(path)
    print('%s size %dx%d' % (path, w, h))
    for y in ys:
        if y < 0 or y >= h:
            continue
        row = rows[y]
        def px(x):
            return (row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
        prev = px(0)
        out = [0]
        for x in range(1, w):
            c = px(x)
            d = abs(c[0] - prev[0]) + abs(c[1] - prev[1]) + abs(c[2] - prev[2])
            if d > 90:
                out.append(x)
                prev = c
        print('y=%d transitions: %s' % (y, out))


if __name__ == '__main__':
    main()
