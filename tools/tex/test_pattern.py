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
# --circle <цвет> --bg <фон> --frac F: фон закрашен --bg, по центру круг
# диаметра F*сторона (F=0.5 => диаметр = половина стороны: для 64x64 это 32 px).
# Дробь задаёт геометрию, поэтому один и тот же паттерн точно ложится в запись
# любого размера (круг масштабируется вместе с записью).
#
#   python3 tools/tex/test_pattern.py <out.png> [--size 256] [--quad tl|tr|bl|br] [--color white|red|green|blue]
#   python3 tools/tex/test_pattern.py <out.png> --band top --solid
#   python3 tools/tex/test_pattern.py <out.png> --size 64 --circle red --bg yellow --frac 0.5
import argparse
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
from unpack_btf import png_write  # noqa: E402

COLORS = {'white': (255, 255, 255), 'red': (255, 0, 0), 'green': (0, 255, 0),
          'blue': (0, 0, 255), 'yellow': (255, 240, 0), 'black': (0, 0, 0)}
QUADS = {'tl': (0, 0), 'tr': (1, 0), 'bl': (0, 1), 'br': (1, 1)}


BANDS = {'top': 'rows', 'bottom': 'rows', 'left': 'cols', 'right': 'cols'}


def build(size, quad='tl', color='white', solid=False, band=None,
          circle=None, bg='black', frac=0.5):
    c = COLORS[color]
    qx, qy = QUADS[quad]
    x0 = (size // 2) * qx
    y0 = (size // 2) * qy
    thick = max(4, size // 8)
    bgc = COLORS[bg]
    cc = COLORS[circle] if circle else None
    r = frac * size / 2.0            # радиус круга (диаметр = frac*сторона)
    cx = cy = size / 2.0
    rows = []
    for y in range(size):
        line = bytearray(size * 4)
        line[3::4] = b'\xff' * size          # альфа непрозрачная
        for x in range(size):
            o = x * 4
            if cc is not None:
                # фон, затем круг по центру
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 <= r * r:
                    line[o] = cc[0]; line[o + 1] = cc[1]; line[o + 2] = cc[2]
                else:
                    line[o] = bgc[0]; line[o + 1] = bgc[1]; line[o + 2] = bgc[2]
                continue
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
            elif cc is None and circle is None and bgc != (0, 0, 0) and band is None:
                line[o] = bgc[0]; line[o + 1] = bgc[1]; line[o + 2] = bgc[2]
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
    ap.add_argument('--circle', default=None, choices=list(COLORS.keys()),
                    help='круг по центру заданного цвета')
    ap.add_argument('--bg', default='black', choices=list(COLORS.keys()),
                    help='цвет фона (чёрный по умолчанию)')
    ap.add_argument('--frac', type=float, default=0.5,
                    help='диаметр круга как доля стороны записи (0.5 => 32 px для 64x64)')
    ap.add_argument('--solid', action='store_true',
                    help='вся картинка одним цветом (проверка, что хук вообще применяется)')
    a = ap.parse_args()
    w, h, rows = build(a.size, a.quad, a.color, a.solid, a.band, a.circle, a.bg, a.frac)
    png_write(a.out, w, h, rows)
    if a.circle:
        print('wrote %s (%dx%d): %s background, %s circle centered, diameter %d px' % (
            a.out, w, h, a.bg, a.circle, int(a.frac * a.size)))
    elif a.solid:
        print('wrote %s (%dx%d): solid %s' % (a.out, w, h, a.color))
    elif a.band:
        print('wrote %s (%dx%d): bright %s band along %s edge (thickness %d), rest black' % (
            a.out, w, h, a.color, a.band, max(4, a.size // 8)))
    else:
        print('wrote %s (%dx%d): bright %s square in %s corner, rest black' % (
            a.out, w, h, a.color, a.quad))


if __name__ == '__main__':
    main()
