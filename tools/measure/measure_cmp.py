#!/usr/bin/env python3
"""measure_cmp.py (tools/measure/) - мерит ключевые метрики каждого окна HUD в 4:3 и 16:9
по канвас-нормализованным снимкам (canvasX = px-170, 1px = 1 канвас).
Печатает для каждой пары: золотой текст (bbox, первые строки и их высота/шаг),
светлые вертикальные линии (рамка/скроллбар), зелёные/teal колонки.
  python3 tools/measure/measure_cmp.py
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402
from color_scan import MASKS  # noqa: E402

DIR = paths.scratch_dir() + '/'
STEPS = ['overland', 'step1', 'step2', 'step3', 'step4', 'step5', 'step6', 'step7']


def stats(path, mask):
    w, h, rows = decode_png(path)
    m = MASKS[mask]
    xs, ys, rowcnt = [], [], [0] * h
    for py in range(h):
        row = rows[py]
        c = 0
        for px in range(w):
            i = px * 3
            if m(row[i], row[i + 1], row[i + 2]):
                xs.append(px - 170)
                ys.append(py)
                c += 1
        rowcnt[py] = c
    if not xs:
        return None
    lines = []
    y = 0
    while y < h:
        if rowcnt[y] > 3:
            y0 = y
            while y < h and rowcnt[y] > 3:
                y += 1
            lines.append((y0, y - 1))
        else:
            y += 1
    return (min(xs), max(xs), min(ys), max(ys), len(xs), lines[:8])


def fmt(tag, s):
    if s is None:
        print('  %-10s %s: нет' % (tag, ''))
        return
    x0, x1, y0, y1, n, lines = s
    ls = ' '.join('%d-%d' % (a, b) for a, b in lines)
    print('  %-6s canvasX %4d..%4d  canvasY %4d..%4d  n=%5d  lines: %s' % (tag, x0, x1, y0, y1, n, ls))


def main():
    for st in STEPS:
        print('=== %s ===' % st)
        for tag, mask in (('43-gold', 'gold'), ('4k-gold', 'gold'), ('43-white', 'white'), ('4k-white', 'white')):
            path = DIR + ('n43_' if tag.startswith('43') else 'n4k_') + st + '.png'
            fmt(tag, stats(path, mask))


if __name__ == '__main__':
    main()
