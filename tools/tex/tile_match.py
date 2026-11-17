# tools/tex/tile_match.py — known-answer сверка декода `.tex` с тайлами генератора (фаза B)
#
# Почва карты генерируется из тайлов `data/wrender/figures/textures/*.bmp` (32x32,
# 24-bit RGB) множителем цвета из `GROUPS` (id|group|class|имя|путь|цвет). Декод
# `.tex` = сетка блоков `grid`; блок = тайл, масштабированный до размера блока и
# перемноженный с цветом GROUPS. Script для каждого блока сетки перебирает все
# (тайл, цвет) пары и печатат лучший поштучный остаток |блок - тайл*цвет/255|.
# Малый остаток = блок собран из этого тайла с этим множителем (known-answer).
# Только чтение игры; пишет в stdout.
#
#   python3 tools/tex/tile_match.py <decoded.png> --grid 128
import argparse
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import paths  # noqa: E402
from pack_btf import png_decode  # noqa: E402

WR = paths.game_path('data', 'wrender')
TSIZE = 32                       # тайлы figures/textures — 32x32


def read_groups():
    """GROUPS: id|group|class|name|path|color -> {id: (name, path, color_int)}."""
    out = {}
    with open(os.path.join(WR, 'GROUPS'), encoding='utf-8', errors='replace') as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            parts = line.split('|')
            if len(parts) < 6:
                continue
            try:
                out[parts[0]] = (parts[3], parts[4], int(parts[5]))
            except ValueError:
                continue
    return out


def rgb_of(color):
    """GROUPS color int -> (r,g,b) as 0xRRGGBB decimal."""
    return ((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)


def read_bmp_pixels(path):
    """24-bit BMP с обманным biBitCount: пиксельный блок по длине w*h*3, строки
    bottom-up, порядок каналов RGB. Возвращает top-down rows."""
    d = open(path, 'rb').read()
    w, h = struct.unpack('<ii', d[18:26])
    w, h = abs(w), abs(h)
    off = struct.unpack('<I', d[10:14])[0]
    data = d[off:off + w * h * 3]
    if len(data) < w * h * 3:
        raise ValueError('%s: pixel block truncated' % path)
    rows = []
    for y in range(h):
        s = (h - 1 - y) * w * 3
        rows.append(data[s:s + w * 3])
    return w, h, rows


def box_down(rows, w, h, f):
    """Box-average downscale by integer factor f on RGBA rows -> RGB triplets."""
    ow, oh = w // f, h // f
    out = []
    for y in range(oh):
        line = []
        for x in range(ow):
            s = 0
            c0 = c1 = c2 = 0
            for dy in range(f):
                r = rows[y * f + dy]
                for dx in range(f):
                    o = (x * f + dx) * 4
                    c0 += r[o]; c1 += r[o + 1]; c2 += r[o + 2]
            n = f * f
            line.append((c0 // n, c1 // n, c2 // n))
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description='known-answer: .tex blocks vs wrender tiles')
    ap.add_argument('png', help='decoded PNG of the .tex blob')
    ap.add_argument('--grid', type=int, default=128, help='tile grid to test (32/64/128)')
    ap.add_argument('--blocks', type=int, default=12, help='how many blocks to report')
    ap.add_argument('--top', type=int, default=3, help='best matches per block')
    a = ap.parse_args()

    groups = read_groups()
    tdir = os.path.join(WR, 'figures', 'textures')
    tiles = []
    for fn in sorted(os.listdir(tdir)):
        if not fn.lower().endswith('.bmp'):
            continue
        try:
            tw, th, rows = read_bmp_pixels(os.path.join(tdir, fn))
        except ValueError:
            continue
        if (tw, th) != (TSIZE, TSIZE):
            continue
        flat = b''.join(rows)
        cid = name = None
        tint = None
        base = fn.lower()
        for cid, (name, path, color) in groups.items():
            if path.replace('\\', '/').lower().endswith('/' + base):
                tint = rgb_of(color)
                break
        tiles.append((cid, name, fn, flat, tint))
    matched = [t for t in tiles if t[4] is not None]
    print('tiles=%d (with GROUPS color %d), groups=%d' % (len(tiles), len(matched), len(groups)))

    w, h, rows, bpp = png_decode(a.png)
    g = a.grid
    f = g // TSIZE
    if f < 1:
        sys.exit('grid %d smaller than tile 32' % g)
    print('%s %dx%d grid %d -> %d blocks (tile downscale f=%d)' % (
        os.path.basename(a.png), w, h, g, (w // g) * (h // g), f))

    total = 0
    nrep = 0
    nblocks = 0
    for by in range(0, h, g):
        for bx in range(0, w, g):
            blk = box_down(rows[by:by + g], g, g, f)
            scored = []
            for cid, name, fn, flat, tint in matched:
                s = 0
                for y in range(TSIZE):
                    for x in range(TSIZE):
                        o = (y * TSIZE + x) * 3
                        b = blk[y][x]
                        s += abs(flat[o] * tint[0] // 255 - b[0]) \
                           + abs(flat[o + 1] * tint[1] // 255 - b[1]) \
                           + abs(flat[o + 2] * tint[2] // 255 - b[2])
                scored.append((s / (TSIZE * TSIZE), cid, name, fn, tint))
            scored.sort(key=lambda t: t[0])
            best = scored[0]
            total += best[0]
            nblocks += 1
            if nrep < a.blocks:
                nrep += 1
                print('block (%d,%d): best %s id=%s tint=%s resid=%.1f; next %s resid=%.1f' % (
                    bx, by, best[3], best[1], best[4], best[0],
                    scored[1][3], scored[1][0]))
    print('mean per-pixel residual over %d blocks = %.1f' % (nblocks, total / nblocks))


if __name__ == '__main__':
    main()
