#!/usr/bin/env python3
"""Сравнение двух PNG по прямоугольникам (mean/max/пиксели-выше-допуска в каждой зоне).
Зоны задаются КАНВАСНЫМИ координатами (канвас 1024x768) -> пиксели снимка через sx,sy.
Нужно, чтобы отсечь анимированный фон оверленда: сравниваем только зоны панелей/UI.

  python3 tools/measure/region_diff.py a.png b.png
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402


def diff_zone(rows_a, rows_b, w, h, x0, y0, x1, y1, tol=8):
    s = 0
    mx = 0
    over = 0
    n = 0
    y0 = max(0, y0)
    y1 = min(h, y1)
    x0 = max(0, x0)
    x1 = min(w, x1)
    for y in range(y0, y1):
        ra = rows_a[y]
        rb = rows_b[y]
        for x in range(x0, x1):
            i = x * 3
            d = abs(ra[i] - rb[i])
            if abs(ra[i + 1] - rb[i + 1]) > d:
                d = abs(ra[i + 1] - rb[i + 1])
            if abs(ra[i + 2] - rb[i + 2]) > d:
                d = abs(ra[i + 2] - rb[i + 2])
            s += d
            n += 1
            if d > mx:
                mx = d
            if d > tol:
                over += 1
    if n == 0:
        return (0, 0, 0, 0)
    return (s / n, mx, over, n)


def main():
    a, b = sys.argv[1], sys.argv[2]
    wa, ha, ra = decode_png(a)
    wb, hb, rb = decode_png(b)
    if (wa, ha) != (wb, hb):
        print('размеры разные: %dx%d vs %dx%d' % (wa, ha, wb, hb))
        return
    sx, sy = wa / 1024.0, ha / 768.0
    zones = [
        ('left_panel', 0, 256, 256, 512),
        ('right_panel', 512, 256, 512, 512),
        ('top_log', 135, 0, 755, 60),
        ('center_minimap', 400, 280, 220, 200),
    ]
    if len(sys.argv) > 3 and sys.argv[3] == 'battle':
        # боевой HUD: левый BHUD-блок (XLeft/YLeft) и миникарта (canvas-rect'ы
        # ДО сдвига - сравниваем базовые зоны, в 4:3 они не должны измениться)
        zones = [
            ('left_bhud', 0, 512, 256, 256),
            ('minimap', 768, 512, 256, 256),
            ('left_btns', 1, 593, 175, 175),
            ('console_strip', 0, 90, 16, 428),
            ('center_battle', 300, 200, 400, 300),
        ]
    print('%s vs %s (%dx%d, sx=%.4f sy=%.4f)' % (a, b, wa, ha, sx, sy))
    for nm, cx, cy, cw, ch in zones:
        x0, y0, x1, y1 = int(cx * sx), int(cy * sy), int((cx + cw) * sx), int((cy + ch) * sy)
        m, mx, over, n = diff_zone(ra, rb, wa, ha, x0, y0, x1, y1)
        print('%-14s canvas(%d,%d+%dx%d) px y %d..%d x %d..%d  mean=%.4f max=%d over8=%d/%d'
              % (nm, cx, cy, cw, ch, y0, y1, x0, x1, m, mx, over, n))


if __name__ == '__main__':
    main()
