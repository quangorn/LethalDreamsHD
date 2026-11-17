# tools/tex/test_pattern.py — тестовый паттерн для known-answer проверки отображения
# почвенной текстуры: один яркий квадрат на чёрном фоне.
#
# Паттерн top-down (как хранится blob): БЕЛЫЙ квадрат занимает левую верхнюю
# четверть, всё остальное чёрное. По тому, в какой угол квадрата попадает на
# снимке оверленда, однозначно определяются ориентация (top-down / bottom-up) и
# порядок каналов (белый = R=G=B, каналы не искажаются; цветные варианты
# --color задают R/G/B-канальный тест).
#
# --band top|bottom|left|right: тонкая яркая полоса вдоль края записи
# (толщина 1/8 стороны), остальное чёрное. Полоса — край blob'а, её положение
# относительно шва тайлов на снимке читает ориентацию рядов одноозначно.
#
#   python3 tools/tex/test_pattern.py <out.png> [--size 256] [--quad tl|tr|bl|br] [--color white|red|green|blue]
#   python3 tools/tex/test_pattern.py <out.png> --band top --solid
import argparse
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
from unpack_btf import png_write  # noqa: E402

COLORS = {'white': (255, 255, 255), 'red': (255, 0, 0), 'green': (0, 255, 0),
          'blue': (0, 0, 255)}
QUADS = {'tl': (0, 0), 'tr': (1, 0), 'bl': (0, 1), 'br': (1, 1)}


BANDS = {'top': 'rows', 'bottom': 'rows', 'left': 'cols', 'right': 'cols'}


def build(size, quad='tl', color='white', solid=False, band=None):
    c = COLORS[color]
    qx, qy = QUADS[quad]
    x0 = (size // 2) * qx
    y0 = (size // 2) * qy
    thick = max(4, size // 8)
    rows = []
    for y in range(size):
        line = bytearray(size * 4)
        line[3::4] = b'\xff' * size          # альфа непрозрачная
        for x in range(size):
            o = x * 4
            lit = solid
            if not lit and band:
                if band == 'top' and y < thick:
                    lit = True
                elif band == 'bottom' and y >= size - thick:
                    lit = True
                elif band == 'left' and x < thick:
                    lit = True
                elif band == 'right' and x >= size - thick:
                    lit = True
            if not lit and (x0 <= x < x0 + size // 2 and y0 <= y < y0 + size // 2):
                lit = solid or not band
            if lit:
                line[o] = c[0]; line[o + 1] = c[1]; line[o + 2] = c[2]
        rows.append(bytes(line))
    return size, size, rows


def main():
    ap = argparse.ArgumentParser(description='ground-texture display test pattern')
    ap.add_argument('out', help='выходной PNG')
    ap.add_argument('--size', type=int, default=256)
    ap.add_argument('--quad', default='tl', choices=list(QUADS.keys()),
                    help='какой угол занимает яркий квадрат (left top по умолчанию)')
    ap.add_argument('--color', default='white', choices=list(COLORS.keys()))
    ap.add_argument('--band', default=None, choices=list(BANDS.keys()),
                    help='яркая полоса вдоль края записи (thin edge marker)')
    ap.add_argument('--solid', action='store_true',
                    help='вся картинка одним цветом (проверка, что хук вообще применяется)')
    a = ap.parse_args()
    w, h, rows = build(a.size, a.quad, a.color, a.solid, a.band)
    png_write(a.out, w, h, rows)
    if a.solid:
        print('wrote %s (%dx%d): solid %s' % (a.out, w, h, a.color))
    elif a.band:
        print('wrote %s (%dx%d): bright %s band along %s edge (thickness %d), rest black' % (
            a.out, w, h, a.color, a.band, max(4, a.size // 8)))
    else:
        print('wrote %s (%dx%d): bright %s square in %s corner, rest black' % (
            a.out, w, h, a.color, a.quad))


if __name__ == '__main__':
    main()
