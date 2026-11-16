#!/usr/bin/env python3
"""color_scan.py - поиск пикселей заданной палитры в канвас-нормализованном снимке.
Канвасные координаты: canvasX = px - 170, canvasY = py.
Маски:
  teal   : скроллбар/синие иконки (R<120, G>140, B>120)
  gold   : жёлто-золотой текст (R>150, G>100, B<130, R-B>40)
  green  : зелёный текст/кнопка (G>120, R<120, B<120)
  white  : светлая рамка/текст (min(R,G,B)>150)
  black  : чёрная панель (max(R,G,B)<60)

  python3 tools/measure/color_scan.py n4k_step5.png teal
  python3 tools/measure/color_scan.py n4k_step5.png gold rows
"""
import os, sys
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pngdiff import decode_png  # noqa: E402

MASKS = {
    'teal': lambda r, g, b: r < 120 and g > 140 and b > 120,
    'gold': lambda r, g, b: r > 150 and g > 100 and b < 130 and (r - b) > 40,
    'green': lambda r, g, b: g > 120 and r < 120 and b < 120,
    'white': lambda r, g, b: min(r, g, b) > 150,
    'black': lambda r, g, b: max(r, g, b) < 60,
    'inky': lambda r, g, b: max(r, g, b) < 25,
}


def main():
    path, name = sys.argv[1], sys.argv[2]
    rest = sys.argv[3:]
    ints = [int(v) for v in rest if v.lstrip('-').isdigit()]
    words = [v for v in rest if not v.lstrip('-').isdigit()]
    mode = words[0] if words else 'bbox'
    # окно канвасных координат (canvasX0 canvasX1 canvasY0 canvasY1), опционально
    wx0, wx1, wy0, wy1 = -10**6, 10**6, -10**6, 10**6
    thr = 25
    if len(ints) >= 4:
        wx0, wx1, wy0, wy1 = ints[:4]
        if len(ints) > 4:
            thr = ints[4]
    w, h, rows = decode_png(path)
    m = MASKS[name]
    px0, px1 = max(0, wx0 + 170), min(w, wx1 + 170)
    py0, py1 = max(0, wy0), min(h, wy1)
    xs = []
    ys = []
    pts = 0
    rowcnt = [0] * h
    for py in range(py0, py1):
        row = rows[py]
        cnt = 0
        for px in range(px0, px1):
            i = px * 3
            if m(row[i], row[i + 1], row[i + 2]):
                xs.append(px - 170)
                ys.append(py)
                pts += 1
                cnt += 1
        rowcnt[py] = cnt
    if pts == 0:
        print('%s %s: нет пикселей' % (path, name))
        return
    print('%s %s: n=%d  canvasX %d..%d  canvasY %d..%d' % (path, name, pts, min(xs), max(xs), min(ys), max(ys)))
    if mode == 'cols':
        colcnt = [0] * w
        for py in range(py0, py1):
            row = rows[py]
            for px in range(px0, px1):
                i = px * 3
                if m(row[i], row[i + 1], row[i + 2]):
                    colcnt[px] += 1
        groups = []
        x = px0
        while x < px1:
            if colcnt[x] > 25:
                x0 = x
                while x < px1 and colcnt[x] > 25:
                    x += 1
                groups.append((x0 - 170, x - 1 - 170, max(colcnt[x0:x])))
            else:
                x += 1
        for a, b, n in groups[:30]:
            print('  col canvasX %d..%d (w=%d) maxcount=%d' % (a, b, b - a + 1, n))
    if mode == 'rows':
        # строки текста: группы строк канвас Y с пикселями mask
        groups = []
        y = 0
        while y < h:
            if rowcnt[y] > 3:
                y0 = y
                while y < h and rowcnt[y] > 3:
                    y += 1
                groups.append((y0, y - 1, sum(rowcnt[y0:y])))
            else:
                y += 1
        for a, b, n in groups[:40]:
            print('  row canvasY %d..%d (h=%d) px=%d' % (a, b, b - a + 1, n))


if __name__ == '__main__':
    main()
