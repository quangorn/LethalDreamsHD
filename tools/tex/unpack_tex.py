#!/usr/bin/env python3
"""Раскод почвенных текстур карт Lethal Dreams: `.tex` из `.map` -> PNG.

Формат записи и поля шапки — `tools/tex/scan_tex.py`. Пиксельный blob = raw
interleaved raster (24-бит RGB при alpha=0, 32-бит RGBA при alpha=1), строки
top-down, порядок каналов RGB/RGBA — тот же канон, что в `.btf`
(`docs/OBSERVATIONS_TEXTURE.md`); подтверждено visual-known-answer:
снимок оверленда новой игры совпадает по цветам с декодом той же карты.

Инструмент только читает игру; пишет PNG и MANIFEST.txt в scratch-каталоги.

Команды:
    python3 tools/tex/unpack_tex.py <map>.map -o <out.png> --index N
    python3 tools/tex/unpack_tex.py --batch data/maps <outdir>      # все 44 + MANIFEST
    python3 tools/tex/unpack_tex.py <map>.map --probe              # все варианты раскода
    python3 tools/tex/unpack_tex.py <map>.map -o <out.png> --flip --vflip
"""
# Заголовок проекта (2026-11). Раскод `.tex` blob'а в PNG; канон top-down, RGB/RGBA.
#   decode_blob(blob, w, h, bpp, layout, bottomup) -> RGBA-строки top-down.
#   unpack_map(map_path, outdir, index)            -> PNG одной записи.
#   batch(maps_dir, outdir)                        -> 44 PNG + MANIFEST.txt.
#   probe_one(map_path, outdir)                    -> все варианты раскода для отбора.
# Потребители: CLI; tools/tex/qc_tex.py (round-trip сверка).
import argparse
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from unpack_btf import png_write, decode24, decode32, hflip_rows  # noqa: E402
from scan_tex import load_payload, find_records  # noqa: E402

LAYOUTS24 = ('RGB', 'BGR')
LAYOUTS32 = ('RGBA', 'BGRA', 'ABGR', 'ARGB')


def decode_blob(blob, w, h, bpp, layout, bottomup=False):
    """blob -> список RGBA-строк top-down (24-бит канон RGB, 32-бит канон RGBA)."""
    if bpp == 3:
        return decode24(blob, w, h, layout, bottomup)
    return decode32(blob, w, h, layout, bottomup)


def unpack_map(map_path, outpath, index=0, layout=None, vflip=False, flip=False):
    """Одна запись `.map` -> PNG. Канон: layout RGB/RGBA, top-down."""
    payload = load_payload(map_path)
    recs = find_records(payload)
    if not recs:
        raise ValueError('%s: no .tex records' % map_path)
    if index >= len(recs):
        raise ValueError('--index %d out of range (%d records)' % (index, len(recs)))
    r = recs[index]
    blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
    canon = 'RGBA' if r['bpp'] == 4 else 'RGB'
    lay = layout or canon
    rows = decode_blob(blob, r['w'], r['h'], r['bpp'], lay, False)
    if vflip:
        rows = rows[::-1]
    if flip:
        rows = hflip_rows(rows, 4)
    png_write(outpath, r['w'], r['h'], rows)
    print('wrote %s (%s %dx%d bpp=%d blob=%d layout=%s%s%s)' % (
        outpath, r['name'], r['w'], r['h'], r['bpp'], r['blob_len'], lay,
        ' vflip' if vflip else '', ' flip' if flip else ''))
    return r


def batch(maps_dir, outdir):
    """Все `.map` каталога -> PNG в outdir + MANIFEST.txt. Пропусков быть не должно."""
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    made = 0
    for fn in sorted(os.listdir(maps_dir)):
        if not fn.endswith('.map'):
            continue
        payload = load_payload(os.path.join(maps_dir, fn))
        for r in find_records(payload):
            blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
            stem = r['name'][:-4]
            lay = 'RGBA' if r['bpp'] == 4 else 'RGB'
            rows = decode_blob(blob, r['w'], r['h'], r['bpp'], lay, False)
            outp = os.path.join(outdir, stem + '.png')
            png_write(outp, r['w'], r['h'], rows)
            manifest.append('%s\t%s\t%d\t%d\t%d\t%d\t%d' % (
                stem, fn, r['w'], r['h'], r['bpp'], r['blob_len'], r['blob_off']))
            made += 1
    with open(os.path.join(outdir, 'MANIFEST.txt'), 'w', encoding='utf-8') as fh:
        fh.write('name\tmap\tw\th\tbpp\tblob_len\tblob_off\n')
        fh.write('\n'.join(manifest) + '\n')
    print('unpacked %d .tex -> %s' % (made, outdir))
    return made


def probe_one(map_path, outdir):
    """Все варианты раскода каждой записи `.map` для визуального отбора."""
    os.makedirs(outdir, exist_ok=True)
    payload = load_payload(map_path)
    made = []
    for r in find_records(payload):
        blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
        stem = r['name'][:-4]
        lays = LAYOUTS32 if r['bpp'] == 4 else LAYOUTS24
        for lay in lays:
            for bottomup in (False, True):
                name = '%s__%s_%s.png' % (stem, lay, 'BU' if bottomup else 'TD')
                rows = decode_blob(blob, r['w'], r['h'], r['bpp'], lay, bottomup)
                png_write(os.path.join(outdir, name), r['w'], r['h'], rows)
                made.append(name)
        print('%s %dx%d bpp=%d blob=%d -> %d variants' % (r['name'], r['w'], r['h'],
                                                          r['bpp'], r['blob_len'], len(made)))
    return made


def main():
    ap = argparse.ArgumentParser(description='Lethal Dreams .tex (ground texture) unpacker')
    ap.add_argument('map', nargs='?', help='.map-файл карты (или каталог для --batch)')
    ap.add_argument('-o', dest='out', help='выходной PNG')
    ap.add_argument('--index', type=int, default=0, help='номер записи .tex в .map')
    ap.add_argument('--batch', nargs=2, metavar=('DIR', 'OUTDIR'),
                    help='распаковать все .map каталога DIR в OUTDIR (+MANIFEST.txt)')
    ap.add_argument('--probe', action='store_true', help='писать все варианты раскода')
    ap.add_argument('--outdir', default=None, help='каталог для --probe')
    ap.add_argument('--layout', default=None, choices=list(LAYOUTS24 + LAYOUTS32),
                    help='порядок каналов (по умолчанию канон RGB/RGBA)')
    ap.add_argument('--flip', action='store_true', help='зеркально по горизонтали')
    ap.add_argument('--vflip', action='store_true', help='вертикальный флип')
    a = ap.parse_args()

    if a.batch:
        d, out = a.batch
        n = batch(d, out)
        if n != 44:
            print('WARNING: %d records (expected 44)' % n)
        return
    if not a.map:
        ap.error('need a .map file (or --batch DIR OUTDIR)')
    if a.probe:
        probe_one(a.map, a.outdir or os.path.join(paths.scratch_dir('ld'), 'tex_probe'))
        return
    if not a.out:
        ap.error('-o <out.png> is required (or --probe / --batch)')
    unpack_map(a.map, a.out, a.index, a.layout, a.vflip, a.flip)


if __name__ == '__main__':
    main()
