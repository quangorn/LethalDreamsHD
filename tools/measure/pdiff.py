#!/usr/bin/env python3
"""pdiff.py (tools/measure/) - попиксельная средняя |ΔRGB| двух PNG одинакового размера.

  python3 tools/measure/pdiff.py a.png b.png
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
    wa, ha, ra = decode_png(a)
    wb, hb, rb = decode_png(b)
    if (wa, ha) != (wb, hb):
        print('%s %s размеры различаются %dx%d vs %dx%d' % (a, b, wa, ha, wb, hb))
        return
    tot = 0
    n = 0
    big = 0
    for y in range(ha):
        x1 = ra[y]
        x2 = rb[y]
        i = 0
        s = 0
        for j in range(len(x1)):
            d = x1[j] - x2[j]
            if d < 0:
                d = -d
            s += d
            if d > 40:
                big += 1
        tot += s
        n += 1
    px = wa * ha
    print('%s vs %s: mean|dRGB|=%.3f  px>40=%d/%d (%.2f%%)' % (a, b, tot / (px * 3), big, px, 100.0 * big / px))


if __name__ == '__main__':
    main()
