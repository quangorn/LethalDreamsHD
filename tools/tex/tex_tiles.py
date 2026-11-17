#!/usr/bin/env python3
"""Нарезка почвенных `.tex` на блоки 32x32 и обратная сборка атласа.

Атлас `.tex` — сетка блоков 32x32: патчи земли, уложенные `BTexturer.dll`, плюс
незанятые слоты (раскладку задают UV меша, см. `docs/OBSERVATIONS_TEXTURE.md`,
«Почвенные текстуры карт»). Нарезка даёт по PNG на блок, имя несёт позицию блока
в атласе, поэтому сборка обратно однозначна:

    <stem>__tx<col>_ty<row>.png      # stem = имя записи без `.tex`

`col`/`row` — номер блока по X/Y (0-based); ширина полей выравнивается по сетке
(минимум 2 цифры), разбор устойчив к паддингу. Блоки пишутся RGBA PNG (для bpp 3
альфа 255) — канал не теряется в AI-апскейлере; при сборке в `.tex` альфа-плоскость
bpp4-записей всё равно берётся родная (nearest), как в `--upscayl`.

Блоки можно ресемплить любым целым множителем f: сборка читает размер блока и
требует, чтобы все блоки записи были одного размера (f = размер/32), атлас
получается в f раз больше. Отсутствующий блок = нули (пустой слот атласа); если
в родной записи он был непустой — это ошибка вызывающей стороны
(`patch_map_pattern.py --tiles`, флаг `--allow-missing`).

Функции и их потребители в репозитории:
  tile_name(stem, tx, ty, nb)              -> имя файла блока
  parse_tile_name(fn)                      -> (stem, tx, ty) | None
  scan_tiles(dirpath)                      -> {stem: {(tx, ty): path}}
  split_record(rows, w, h, bpp, ...)       -> блоки записи; (written, empty) —
                                              tools/tex/unpack_tex.py --tiles
  glue_rows(stem, w, h, tilemap)           -> (f, rows, missing) атлас RGBA —
                                              tools/tex/patch_map_pattern.py --tiles
  blob_tile_empty(blob, w, h, bpp, tx, ty) -> пуст ли блок родного blob'а
  CLI glue <tiles-dir> --maps <dir|map> -o <outdir> — сборка PNG без дерева игры.
"""
import argparse
import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
sys.path.insert(0, _HERE)
from pack_btf import png_decode  # noqa: E402
from unpack_btf import png_write  # noqa: E402
from scan_tex import load_payload, find_records  # noqa: E402

TILE = 32
_TILE_RE = re.compile(r'^(?P<stem>.+)__tx(?P<tx>\d+)_ty(?P<ty>\d+)$', re.I)


def tile_name(stem, tx, ty, nb):
    """Имя PNG блока: `<stem>__tx<col>_ty<row>.png`; `nb` — блоков по стороне."""
    pad = max(2, len(str(max(0, nb - 1))))
    return '%s__tx%0*d_ty%0*d.png' % (stem, pad, tx, pad, ty)


def parse_tile_name(fn):
    """Имя файла (с .png или без) -> (stem, tx, ty); не блок -> None."""
    m = _TILE_RE.match(fn[:-4] if fn.lower().endswith('.png') else fn)
    if not m:
        return None
    return m.group('stem'), int(m.group('tx')), int(m.group('ty'))


def scan_tiles(dirpath):
    """Каталог блоков -> {stem: {(tx, ty): path}}."""
    out = {}
    for fn in sorted(os.listdir(dirpath)):
        if not fn.lower().endswith('.png'):
            continue
        p = parse_tile_name(fn)
        if not p:
            continue
        stem, tx, ty = p
        out.setdefault(stem, {})[(tx, ty)] = os.path.join(dirpath, fn)
    return out


def _rows_empty(rows, bpp):
    """RGBA-строки блока пусты: RGB=0 и (bpp 3 или альфа=0)."""
    if bpp == 3:
        for row in rows:
            if any(row[0::4]) or any(row[1::4]) or any(row[2::4]):
                return False
        return True
    for row in rows:
        if any(row):
            return False
    return True


def split_record(rows, w, h, bpp, outdir, stem, skip_empty=False):
    """RGBA-строки атласа (top-down) -> блоки 32x32 в `outdir`.

    Возвращает (written, empty): сколько PNG записано и сколько блоков пустых
    (при `skip_empty` пустые не пишутся — при сборке они станут нулями).
    """
    if w % TILE or h % TILE:
        raise ValueError('%s: %dx%d не кратно %d' % (stem, w, h, TILE))
    nbw, nbh = w // TILE, h // TILE
    nb = max(nbw, nbh)
    written = empty = 0
    for ty in range(nbh):
        band = rows[ty * TILE:(ty + 1) * TILE]
        for tx in range(nbw):
            x0 = tx * TILE * 4
            tro = [band[y][x0:x0 + TILE * 4] for y in range(TILE)]
            is_empty = _rows_empty(tro, bpp)
            if is_empty:
                empty += 1
                if skip_empty:
                    continue
            png_write(os.path.join(outdir, tile_name(stem, tx, ty, nb)),
                      TILE, TILE, tro)
            written += 1
    return written, empty


def blob_tile_empty(blob, w, h, bpp, tx, ty):
    """Пуст ли блок (tx,ty) в родном blob'е записи (без декодирования в RGBA)."""
    for y in range(TILE):
        off = ((ty * TILE + y) * w + tx * TILE) * bpp
        row = blob[off:off + TILE * bpp]
        if bpp == 3:
            if any(row):
                return False
        elif any(row[i:i + 4] != b'\x00\x00\x00\x00' for i in range(0, len(row), 4)):
            return False
    return True


def glue_rows(stem, w, h, tilemap, bpp=3):
    """{ (tx,ty): path } -> (f, RGBA-строки атласа (f*h), missing).

    Все блоки записи обязаны быть одного квадратного размера, кратного 32
    (f = сторона/32). Позиции вне сетки записи — ошибка. Отсутствующие блоки
    остаются пустыми, как в родной записи: bpp 3 — чёрный с альфой 255 (канон
    `decode24`), bpp 4 — нули (чёрный, альфа 0). Список отсутствующих
    возвращается для сверки с родной записью.
    """
    if w % TILE or h % TILE:
        raise ValueError('%s: %dx%d не кратно %d' % (stem, w, h, TILE))
    nbw, nbh = w // TILE, h // TILE
    bad = sorted(p for p in tilemap if not (0 <= p[0] < nbw and 0 <= p[1] < nbh))
    if bad:
        raise ValueError('%s: блоки вне сетки %dx%d: %s' % (stem, nbw, nbh, bad[:6]))
    dec = {}
    f = None
    for pos in sorted(tilemap):
        tw, th, trows = png_decode(tilemap[pos])
        if tw != th or tw % TILE:
            raise ValueError('%s: блок %s = %dx%d (нужен квадрат, кратный %d)' %
                             (stem, tilemap[pos], tw, th, TILE))
        if f is None:
            f = tw // TILE
        elif tw != f * TILE:
            raise ValueError('%s: блоки разного размера (%d и %d)' % (stem, f * TILE, tw))
        dec[pos] = trows
    if f is None:
        raise ValueError('%s: в каталоге нет блоков' % stem)
    fw, fh, ts = f * w, f * h, f * TILE
    blank = b'\x00\x00\x00\xff' if bpp == 3 else b'\x00\x00\x00\x00'
    canvas = [bytearray(blank * fw) for _ in range(fh)]
    for (tx, ty), trows in dec.items():
        x0 = tx * ts * 4
        for y in range(ts):
            canvas[ty * ts + y][x0:x0 + ts * 4] = trows[y]
    missing = sorted((tx, ty) for ty in range(nbh) for tx in range(nbw)
                     if (tx, ty) not in dec)
    return f, [bytes(r) for r in canvas], missing


def glue_dir(tiledir, maps, outdir, allow_missing=False):
    """Сборка блоков обратно в PNG по размерам записей `.map` (без дерева игры).

    `maps` — каталог с `.map` или один `.map`. Имя PNG — stem записи.
    """
    index = scan_tiles(tiledir)
    os.makedirs(outdir, exist_ok=True)
    if os.path.isdir(maps):
        files = [os.path.join(maps, f) for f in sorted(os.listdir(maps))
                 if f.endswith('.map')]
    else:
        files = [maps]
    recs = {}
    for mp in files:
        payload = load_payload(mp)
        for r in find_records(payload):
            recs[r['name'][:-4]] = (payload, r)
    done = 0
    for stem in sorted(recs):
        tilemap = index.get(stem)
        if not tilemap:
            continue
        payload, r = recs[stem]
        f, rows, missing = glue_rows(stem, r['w'], r['h'], tilemap, r['bpp'])
        blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
        lost = [p for p in missing
                if not blob_tile_empty(blob, r['w'], r['h'], r['bpp'], p[0], p[1])]
        if lost and not allow_missing:
            raise ValueError('%s: нет непустых блоков %s (--allow-missing, чтобы залить '
                             'их нулями)' % (stem, lost[:6]))
        png_write(os.path.join(outdir, stem + '.png'), f * r['w'], f * r['h'], rows)
        print('%s: %dx%d -> %dx%d (f=%d, missing=%d, из них непустых=%d)' %
              (stem, r['w'], r['h'], f * r['w'], f * r['h'], f, len(missing), len(lost)))
        done += 1
    print('glued %d records -> %s' % (done, outdir))
    return done


def main():
    ap = argparse.ArgumentParser(description='glue 32x32 .tex tiles back into atlas PNGs')
    ap.add_argument('cmd', choices=('glue',))
    ap.add_argument('tiledir')
    ap.add_argument('--maps', required=True, help='каталог с .map или один .map')
    ap.add_argument('-o', dest='out', required=True, help='каталог для PNG-атласов')
    ap.add_argument('--allow-missing', action='store_true',
                    help='разрешить отсутствие непустых блоков (заполнятся нулями)')
    a = ap.parse_args()
    glue_dir(a.tiledir, a.maps, a.out, a.allow_missing)


if __name__ == '__main__':
    main()
