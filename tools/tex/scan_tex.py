#!/usr/bin/env python3
"""Экстрактор почвенных текстур карт Lethal Dreams: записи `<map>_<N>.tex` из
`config[game.root]/data/maps/<map>.map` (фаза A плана `docs/TASK_tex_unpacker.md`).

Контейнер `.map` = тот же, что `.btf`: `LDPACKER` + u32(plen) + u32(clen) +
zlib-поток (`78 DA`) → payload. В payload'е — сетка геометрии карты и почвенные
текстуры. Запись текстуры (проверено на всех 30 картах, 44 записи):

    u32 name_len | ASCII `<map>_<N>.tex` | шапка 40 байт | пиксельный blob

Шапка (u32 little-endian, byte-packed; поля после имени не выровнены по 4,
выравнивание сдвигается длиной имени):
    [0]  w, [4] h   — квадрат, w == h == S
    [8]  alpha-флаг — 0 = 24-битный RGB-blob, 1 = 32-битный RGBA-blob
    [12],[16] = 2; [20],[24],[28] = 16777216; [32] = 119 (TLV-тип `'w'` из `.btf`)
    в интервале [8..31] встречается лимит движка 33554432 (32 MiB)
    [36] blob_len == w*h*(3 + alpha-флаг)
Пиксельный blob начинается сразу после шапки: offset = name_off + name_len + 40.
blob_len = w*h*bpp ровно (mip-цепочки, alpha-плоскости и RLE нет). Хвост payload'а
после последней записи — геометрия карты, к текстуре не относится.

Инструмент только читает игру; пишет исключительно в `scratch_dir` (raw-blob'ы и
`MANIFEST.txt`). Раскод blob'а в PNG — `tools/tex/unpack_tex.py`.

Команды:
    python3 tools/tex/scan_tex.py                     # скан всех 30 карт + гейт
    python3 tools/tex/scan_tex.py --dump [OUTDIR]     # raw-blob'ы + MANIFEST.txt
    python3 tools/tex/scan_tex.py <map>.map -o <out.bin> --index N
    python3 tools/tex/scan_tex.py <map>.map --fields  # распечатать поля шапки
"""
# Заголовок проекта (2026-11). Экстрактор записей `.tex` из `.map` (фаза A).
#   load_payload(map_path) -> payload (zlib-поток контейнера LDPACKER).
#   find_records(payload)   -> список записей {name, name_off, blob_off, blob_len, w, h, alpha}.
#   gate(records, payload)  -> сверки гейта фазы A.
#   main()                  -> CLI: скан / --dump / одиночная запись.
# Потребители: tools/tex/unpack_tex.py (раскод blob'а в PNG).
import argparse
import os
import re
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

MAGIC = b'LDPACKER'
HDR_LEN = 40                      # шапка записи .tex (байт после имени)
ZLIB_MAGICS = (b'\x78\xda', b'\x78\x9c', b'\x78\x01')
NAME_RE = re.compile(rb'[A-Za-z0-9_\-]+\.tex')


def u32(buf, i):
    return struct.unpack('<I', buf[i:i + 4])[0]


def load_payload(map_path):
    """payload = распакованный zlib-поток контейнера LDPACKER. Проверка поля plen."""
    raw = open(map_path, 'rb').read()
    if not raw.startswith(MAGIC):
        raise ValueError('%s: no LDPACKER magic' % map_path)
    zi = -1
    for m in ZLIB_MAGICS:
        i = raw.find(m, len(MAGIC))
        if i >= 0:
            zi = i
            break
    if zi < 0:
        raise ValueError('%s: no zlib stream' % map_path)
    payload = zlib.decompress(raw[zi:])
    declared = u32(raw, len(MAGIC))
    if declared != len(payload):
        raise ValueError('%s: plen field %d != payload %d' % (map_path, declared, len(payload)))
    return payload


def parse_record(payload, name_off, name_len):
    """Одна запись `.tex`. Возвращает dict; ValueError при несовпадении полей."""
    hdr_off = name_off + name_len
    hdr = payload[hdr_off:hdr_off + HDR_LEN]
    if len(hdr) < HDR_LEN:
        raise ValueError('truncated header')
    w, h = u32(hdr, 0), u32(hdr, 4)
    alpha = u32(hdr, 8)
    blob_len = u32(hdr, HDR_LEN - 4)
    if w != h:
        raise ValueError('w != h (%d,%d)' % (w, h))
    if alpha not in (0, 1):
        raise ValueError('alpha field %d not 0/1' % alpha)
    bpp = 3 + alpha
    if blob_len != w * h * bpp:
        raise ValueError('blob_len %d != w*h*bpp %d' % (blob_len, w * h * bpp))
    blob_off = hdr_off + HDR_LEN
    if blob_off + blob_len > len(payload):
        raise ValueError('blob runs past payload end')
    # поле count (число 256x256-блоков blob'а) — старшие байты blob_len; его
    # позиции в шапке сдвинуты длиной имени, каноническое смещение не найдено.
    count = blob_len // 65536 if blob_len >= 65536 else blob_len // 256
    return {
        'name': payload[name_off:hdr_off].decode('ascii'),
        'name_off': name_off,
        'name_len': name_len,
        'hdr_off': hdr_off,
        'blob_off': blob_off,
        'blob_len': blob_len,
        'w': w,
        'h': h,
        'alpha': alpha,
        'bpp': bpp,
        'count': count,
        'fields': [u32(hdr, i) for i in range(0, HDR_LEN, 4)],
        'hdr': hdr,
    }


def find_records(payload):
    """Все записи `.tex` payload'а, в порядке offset'ов. Кандидаты, где имя не
    подтверждено полем name_len и полями шапки, отбрасываются."""
    recs = []
    for m in NAME_RE.finditer(payload):
        o = m.start()
        if o < 4:
            continue
        n = len(m.group())
        if u32(payload, o - 4) != n:
            continue          # имя не подтверждено полем длины
        try:
            recs.append(parse_record(payload, o, n))
        except ValueError:
            continue
    recs.sort(key=lambda r: r['name_off'])
    return recs


def gate(records, payload, map_name):
    """Гейт фазы A: записи не перекрываются, blob'ы влезли, имени-дупликата нет."""
    problems = []
    names = [r['name'] for r in records]
    if len(set(names)) != len(names):
        problems.append('%s: duplicate texture names %s' % (map_name, names))
    for i, r in enumerate(records):
        nxt = records[i + 1]['name_off'] - 4 if i + 1 < len(records) else len(payload)
        if r['blob_off'] + r['blob_len'] > nxt:
            problems.append('%s: %s blob overlaps next record' % (map_name, r['name']))
    return problems


def scan_maps(maps_dir):
    """Скан всех `.map` каталога: список (файл, payload, записи)."""
    out = []
    for fn in sorted(os.listdir(maps_dir)):
        if not fn.endswith('.map'):
            continue
        p = load_payload(os.path.join(maps_dir, fn))
        out.append((fn, p, find_records(p)))
    return out


def main():
    ap = argparse.ArgumentParser(description='Lethal Dreams .map ground-texture extractor')
    ap.add_argument('map', nargs='?', help='одиночный .map (по умолчанию — весь каталог карт игры)')
    ap.add_argument('-o', dest='out', help='файл для raw-blob\'а одиночной записи')
    ap.add_argument('--index', type=int, default=0, help='номер записи в .map (0..N-1)')
    ap.add_argument('--fields', action='store_true', help='распечатать u32-поля шапки')
    ap.add_argument('--dump', nargs='?', const=None, metavar='OUTDIR',
                    help='писать raw-blob\'ы и MANIFEST.txt (по умолчанию scratch_dir(\'ld\')/tex_raw)')
    a = ap.parse_args()

    if a.map:
        payload = load_payload(a.map)
        recs = find_records(payload)
        for i, r in enumerate(recs):
            print('%s  name_off=%d blob_off=%d blob_len=%d w=%d h=%d alpha=%d bpp=%d count=%d'
                  % (r['name'], r['name_off'], r['blob_off'], r['blob_len'],
                     r['w'], r['h'], r['alpha'], r['bpp'], r['count']))
            if a.fields:
                print('   fields[0..9] = %s' % r['fields'])
        if a.out:
            if not recs:
                sys.exit('no .tex records in %s' % a.map)
            if a.index >= len(recs):
                sys.exit('--index %d out of range (%d records)' % (a.index, len(recs)))
            r = recs[a.index]
            out = a.out
            os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
            open(out, 'wb').write(payload[r['blob_off']:r['blob_off'] + r['blob_len']])
            print('wrote %s (%d bytes, %dx%d bpp=%d)' % (out, r['blob_len'], r['w'], r['h'], r['bpp']))
        return

    maps_dir = paths.game_path('data', 'maps')
    scanned = scan_maps(maps_dir)
    all_recs = [r for _, p, rs in scanned for r in rs]
    problems = [p for _, p, rs in scanned for p in gate(rs, p, os.path.basename(_))]

    outdir = None
    if a.dump is not None:
        outdir = a.dump if a.dump else os.path.join(paths.scratch_dir('ld'), 'tex_raw')
        os.makedirs(outdir, exist_ok=True)
        manifest = []
        for fn, p, rs in scanned:
            for r in rs:
                stem = r['name'][:-4]
                blob = p[r['blob_off']:r['blob_off'] + r['blob_len']]
                binpath = os.path.join(outdir, stem + '.bin')
                open(binpath, 'wb').write(blob)
                manifest.append('%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d' % (
                    stem, fn, r['w'], r['h'], r['bpp'], r['blob_len'],
                    r['name_off'], r['blob_off'], r['count']))
        with open(os.path.join(outdir, 'MANIFEST.txt'), 'w', encoding='utf-8') as fh:
            fh.write('name\tmap\tw\th\tbpp\tblob_len\tname_off\tblob_off\tcount\n')
            fh.write('\n'.join(manifest) + '\n')
        print('dumped %d blobs -> %s' % (len(all_recs), outdir))

    for fn, p, rs in scanned:
        print('%-28s payload=%9d records=%d %s' % (fn, len(p), len(rs),
                                                   ', '.join(r['name'] for r in rs) if rs else 'none'))
    print('maps=%d records=%d names_unique=%d' % (len(scanned), len(all_recs), len(set(r['name'] for r in all_recs))))
    total = sum(r['blob_len'] for r in all_recs)
    print('sum blob bytes = %d; sum(headers) = %d' % (total, len(all_recs) * HDR_LEN))
    for pr in problems:
        print('GATE FAIL:', pr)
    if not problems and len(all_recs) == 44:
        print('GATE OK: 44 records, coverage consistent, no duplicates')
    elif not problems:
        print('GATE: coverage consistent, %d records (expected 44)' % len(all_recs))
    else:
        sys.exit(1)


if __name__ == '__main__':
    main()
