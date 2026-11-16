#!/usr/bin/env python3
"""edge_scan.py (tools/measure/) - сканирует канвас-нормализованный снимок по строкам канвасных Y и
печатает интервалы «ярких» (светлая рамка/текст) и «тёмных» (чёрная панель) блоков.
Снимок должен быть в канвасных координатах (1 px = 1 канвас, x=-170..1196 -> пиксель 0..1196+170).
Скриншот нормализуется tools/measure/norm_canvas.ps1, пиксель X = canvasX + 170.

  python3 tools/measure/edge_scan.py n43_step5.png 90 200 -170 1196
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402


def runs(mask):
    out = []
    i = 0
    n = len(mask)
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            out.append((i, j - 1))
            i = j
        else:
            i += 1
    return out


def main():
    path = sys.argv[1]
    ys = [int(v) for v in sys.argv[2:] if v.lstrip('-').isdigit()]
    w, h, rows = decode_png(path)
    x0 = -170
    print('%s size %dx%d (canvas x = px - 170)' % (path, w, h))
    for cy in ys:
        if cy < 0 or cy >= h:
            continue
        row = rows[cy]
        bright = []
        dark = []
        for px in range(w):
            i = px * 3
            s = row[i] + row[i + 1] + row[i + 2]
            bright.append(s > 600)
            dark.append(s < 90)
        bx = [(a - x0, b - x0) for a, b in runs(bright) if b - a >= 2]
        dx = [(a - x0, b - x0) for a, b in runs(dark) if b - a >= 8]
        print('canvasY=%d bright-runs(x): %s' % (cy, bx[:14]))
        print('           dark-runs>=8(x): %s' % dx[:10])


if __name__ == '__main__':
    main()
