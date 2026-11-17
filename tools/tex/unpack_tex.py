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
    python3 tools/tex/unpack_tex.py --batch data/maps <out> --tiles <dir>  # блоки 32x32

Режим `--tiles DIR` раскладывает записи на блоки 32x32 (`tools/tex/tex_tiles.py`):
`<stem>__tx<col>_ty<row>.png`, сборка обратно — `patch_map_pattern.py --tiles DIR`
(или `tex_tiles.py glue`).
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
import tex_tiles  # noqa: E402

LAYOUTS24 = ('RGB', 'BGR')
LAYOUTS32 = ('RGBA', 'BGRA', 'ABGR', 'ARGB')
TILES_HEADER = 'name\tmap\tw\th\tbpp\ttile\ttiles\ttiles_written\tempty\n'


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


def split_map(map_path, outdir, index=None, skip_empty=False):
    """Блоки 32x32 записей `.map` (или одной по `index`) в outdir.

    Возвращает (written, manifest-строки) для MANIFEST.txt каталога блоков.
    Имена: `<stem>__tx<col>_ty<row>.png` (`tools/tex/tex_tiles.py`).
    """
    os.makedirs(outdir, exist_ok=True)
    payload = load_payload(map_path)
    recs = find_records(payload)
    if not recs:
        raise ValueError('%s: no .tex records' % map_path)
    if index is not None:
        if index >= len(recs):
            raise ValueError('--index %d out of range (%d records)' % (index, len(recs)))
        recs = [recs[index]]
    map_name = os.path.basename(map_path)
    written = 0
    manifest = []
    for r in recs:
        blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
        lay = 'RGBA' if r['bpp'] == 4 else 'RGB'
        rows = decode_blob(blob, r['w'], r['h'], r['bpp'], lay, False)
        stem = r['name'][:-4]
        n, empty = tex_tiles.split_record(rows, r['w'], r['h'], r['bpp'],
                                          outdir, stem, skip_empty)
        written += n
        manifest.append('%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d' % (
            stem, map_name, r['w'], r['h'], r['bpp'], tex_tiles.TILE,
            (r['w'] // tex_tiles.TILE) * (r['h'] // tex_tiles.TILE), n, empty))
    return written, manifest


def split_batch(maps_dir, outdir, skip_empty=False):
    """Все `.map` каталога -> блоки 32x32 в outdir + MANIFEST.txt."""
    os.makedirs(outdir, exist_ok=True)
    manifest = []
    made = 0
    for fn in sorted(os.listdir(maps_dir)):
        if not fn.endswith('.map'):
            continue
        n, rows = split_map(os.path.join(maps_dir, fn), outdir, None, skip_empty)
        made += n
        manifest += rows
    write_tiles_manifest(outdir, manifest)
    print('split %d tiles -> %s' % (made, outdir))
    return made


def write_tiles_manifest(outdir, manifest):
    """MANIFEST.txt каталога блоков: по строке на запись (размеры/число блоков)."""
    with open(os.path.join(outdir, 'MANIFEST.txt'), 'w', encoding='utf-8') as fh:
        fh.write(TILES_HEADER)
        if manifest:
            fh.write('\n'.join(manifest) + '\n')


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
    ap.add_argument('--index', type=int, default=None,
                    help='номер записи .tex в .map (по умолчанию 0 для -o, все для --tiles)')
    ap.add_argument('--batch', nargs=2, metavar=('DIR', 'OUTDIR'),
                    help='распаковать все .map каталога DIR в OUTDIR (+MANIFEST.txt)')
    ap.add_argument('--tiles', metavar='DIR', default=None,
                    help='вместо целого PNG писать блоки 32x32 в DIR '
                         '(<stem>__tx<col>_ty<row>.png); работает и с --batch')
    ap.add_argument('--skip-empty', action='store_true',
                    help='с --tiles: не писать пустые блоки (при сборке станут нулями)')
    ap.add_argument('--probe', action='store_true', help='писать все варианты раскода')
    ap.add_argument('--outdir', default=None, help='каталог для --probe')
    ap.add_argument('--layout', default=None, choices=list(LAYOUTS24 + LAYOUTS32),
                    help='порядок каналов (по умолчанию канон RGB/RGBA)')
    ap.add_argument('--flip', action='store_true', help='зеркально по горизонтали')
    ap.add_argument('--vflip', action='store_true', help='вертикальный флип')
    a = ap.parse_args()

    if a.batch:
        d, out = a.batch
        if a.tiles:
            n = split_batch(d, a.tiles, a.skip_empty)
        else:
            n = batch(d, out)
            if n != 44:
                print('WARNING: %d records (expected 44)' % n)
        return
    if not a.map:
        ap.error('need a .map file (or --batch DIR OUTDIR)')
    if a.probe:
        probe_one(a.map, a.outdir or os.path.join(paths.scratch_dir('ld'), 'tex_probe'))
        return
    if a.tiles:
        written, manifest = split_map(a.map, a.tiles, a.index, a.skip_empty)
        write_tiles_manifest(a.tiles, manifest)
        print('split %d tiles -> %s' % (written, a.tiles))
        return
    if not a.out:
        ap.error('-o <out.png> is required (or --probe / --batch / --tiles)')
    unpack_map(a.map, a.out, a.index if a.index is not None else 0, a.layout,
               a.vflip, a.flip)


if __name__ == '__main__':
    main()
