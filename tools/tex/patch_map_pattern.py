# tools/tex/patch_map_pattern.py — замена почвенных `.tex` тестовым паттерном в
# scratch-дереве игры (known-answer проверка раскода/ориентации `.tex`).
#
# Значение `.tex` в том, что его грузит движок по имени записи (`<map>_<N>.tex`),
# поэтому подмена пикселей записи работает БЕЗ хук-модулей: родные материалы
# (`data/materials/<map>_opaque_N.pyc`) рисуют то, что лежит в `.map`.
# Проверяно: `mm_bkg01.btf` движок при запуске пересоздаёт из источника, поэтому
# путь через `.btf`-ресурс непригоден (текстура в материале = родная 2048x2048).
#
# Паттерн — `tools/tex/test_pattern.py` в размере записи (w == h), яркая фигура в
# углу на чёрном фоне; альфа-плоскость для alpha-записей (bpp 4) непрозрачная.
# Контейнер `.map` (LDPACKER + zlib) пересобирается: payload той же длины, поля
# plen/clen правятся, как в `tools/btf/pack_btf.py`.
#
# Оригиналы `.map` сохраняются в `<tree>/data/maps_ground_backup/`.
#
#   python3 tools/tex/patch_map_pattern.py --tree <дерево> [--map <map>]  # поставить
#   python3 tools/tex/patch_map_pattern.py --tree <дерево> --restore       # вернуть
import argparse
import os
import shutil
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pack_btf import build_blob, png_decode  # noqa: E402
from scan_tex import load_payload, find_records, u32, MAGIC, ZLIB_MAGICS  # noqa: E402
from test_pattern import build as build_pattern, COLORS, QUADS, BANDS  # noqa: E402


def find_zlib(raw):
    for m in ZLIB_MAGICS:
        i = raw.find(m, len(MAGIC))
        if i >= 0:
            return i
    raise ValueError('no zlib stream')


def resample_nearest(rows, w, h, nw, nh):
    """Ближайшая передискретизация RGBA-строк (w,h) -> (nw,nh). Топ-даун канон `.tex`;
    центрирование при уменьшении даёт кроп центра."""
    xs = [min(w - 1, (x * w) // nw) for x in range(nw)]
    out = []
    for y in range(nh):
        sy = min(h - 1, (y * h) // nh)
        src = rows[sy]
        r = bytearray(nw * 4)
        r[0::4] = bytes([src[4 * i] for i in xs])
        r[1::4] = bytes([src[4 * i + 1] for i in xs])
        r[2::4] = bytes([src[4 * i + 2] for i in xs])
        r[3::4] = bytes([src[4 * i + 3] for i in xs])
        out.append(bytes(r))
    return out


def chain_headers(payload, start=8):
    """Цепочка TLV-заголовков payload'а (как её ходит FUN_0049bf70):
    заголовок = (type u32, size u32) по pos, содержимое pos+8..pos+8+size,
    следующий заголовок — pos+8+size. Возвращает [(pos, type, size)]."""
    out = []
    pos = start
    n = len(payload)
    while pos + 8 <= n:
        typ, size = struct.unpack_from('<2I', payload, pos)
        out.append((pos, typ, size))
        nxt = pos + 8 + size
        if nxt > n - 8:
            break
        pos = nxt
    return out


def fix_chain(payload, recs, deltas):
    """Правит размеры всех TLV-заголовков, накрывающих изменённые записи, после
    вставок/вырезок blob'ов. `deltas` — {name_off: дельта длины blob'а}.
    Обход цепочки — по ЧИСТЫМ координатам payload'а (размеры ancestors ещё старые):
    позиция clean-заголовка в новом payload = pos + сумма дельт записей, целиком
    лежащих перед pos; размер заголовка растёт на сумму дельт записей, чьи внешние
    заголовки накрыты его содержимым (`pos <= name_off-32 < pos+8+size`).
    ADV (`FUN_0049bf70`) сверяет `ftell == pos+8`, поэтому рассинхрон любого
    накрывающего размера гасит флаг потока и FACT (0x495720) даёт NULL → краш."""
    for r in recs:
        r['delta'] = deltas.get(r['name_off'], 0)
    # внешний заголовок самой записи (type по name_off-32, size по name_off-28)
    for r in recs:
        if r['delta']:
            pos = r['name_off'] - 28
            shift = sum(rr['delta'] for rr in recs
                        if rr['blob_off'] + rr['blob_len'] <= pos)
            old = struct.unpack_from('<I', payload, pos + shift)[0]
            struct.pack_into('<I', payload, pos + shift, old + r['delta'])
    for pos, typ, size in chain_headers(bytes(payload)):
        # заголовок цепочки в clean-координатах -> смещение в новом payload
        shift = sum(r['delta'] for r in recs
                    if r['blob_off'] + r['blob_len'] <= pos)
        add = 0
        for r in recs:
            rec_hdr = r['name_off'] - 32
            if pos <= rec_hdr < pos + 8 + size:
                add += r['delta']
        if add:
            struct.pack_into('<I', payload, pos + shift + 4, size + add)


def blob_rows(payload, r):
    """Строки RGBA (top-down) из blob'а записи: bpp=3 -> RGB-триплеты, 4 -> RGBA."""
    w, h, bpp = r['w'], r['h'], r['bpp']
    blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
    out = []
    for y in range(h):
        row = blob[y * w * bpp:(y + 1) * w * bpp]
        if bpp == 3:
            rgb = bytearray(w * 4)
            rgb[0::4] = row[0::3]
            rgb[1::4] = row[1::3]
            rgb[2::4] = row[2::3]
            rgb[3::4] = b'\xff' * w
            out.append(bytes(rgb))
        else:
            out.append(bytes(row))
    return out


def patch_rescale(map_path, srcdir, num, den, record=None, maxsrc=None, self_src=False):
    """payload: .tex-записи пересемплированы множителем num/den (2 = апскейл,
    1/2 = даунскейл кропом центра, произвольные целые стороны тоже годятся).

    Источник PNG — `srcdir/<stem>.png` (родный размер записи); при `self_src`
    источником служит сам blob записи (`--self`). Запись пересобирается целиком:
    имя то же, `w,h` = родные*num//den, `blob_len = w*h*bpp` новый,
    payload удлинняется/укорачивается, поля plen/clen контейнера правятся.
    Round-trip: перечитанный blob побитово == ожидаемый, w/h = новые.
    `record` — имя одной записи (`portal_domain_1`); `maxsrc` — только записи со
    стороной <= maxsrc.
    """
    raw = open(map_path, 'rb').read()
    payload_orig = load_payload(map_path)
    recs = find_records(payload_orig)
    if not recs:
        return 0
    pieces = []
    prev_end = 0
    expected = {}
    deltas = {}
    n = 0
    for r in recs:
        stem = r['name'][:-4]
        if record is not None and stem != record:
            pieces.append(payload_orig[prev_end:r['blob_off'] + r['blob_len']])
            prev_end = r['blob_off'] + r['blob_len']
            continue
        if maxsrc is not None and r['w'] > maxsrc:
            pieces.append(payload_orig[prev_end:r['blob_off'] + r['blob_len']])
            prev_end = r['blob_off'] + r['blob_len']
            continue
        if (r['w'] * num) % den or (r['h'] * num) % den:
            raise ValueError('%s: w*h*num/den не целые (%d*%d/%d)' %
                             (stem, r['w'], num, den))
        w2, h2, bpp = r['w'] * num // den, r['h'] * num // den, r['bpp']
        if self_src:
            pw, ph, rows = r['w'], r['h'], blob_rows(payload_orig, r)
        else:
            pw, ph, rows = png_decode(os.path.join(srcdir, stem + '.png'))
        if (pw, ph) != (r['w'], r['h']):
            raise ValueError('%s: PNG %dx%d != родная запись %dx%d' %
                             (stem, pw, ph, r['w'], r['h']))
        blob = build_blob(resample_nearest(rows, pw, ph, w2, h2), w2, h2, bpp)
        if len(blob) != w2 * h2 * bpp:
            raise ValueError('%s: blob %d != w*h*bpp' % (stem, len(blob)))
        if len(blob) > 33554432:
            raise ValueError('%s: blob %d превышает лимит движка 33554432' % (stem, len(blob)))
        hdr = bytearray(r['hdr'])
        struct.pack_into('<I', hdr, 0, w2)
        struct.pack_into('<I', hdr, 4, h2)
        struct.pack_into('<I', hdr, 36, len(blob))
        # name_len u32 записи живёт в gap'е перед именем (scan_tex: name_off =
        # строка имени); rec_bin = имя+шапка+blob, лишнего префикса нет.
        rec_bin = r['name'].encode('ascii') + bytes(hdr) + blob
        deltas[r['name_off']] = len(blob) - r['blob_len']
        pieces.append(payload_orig[prev_end:r['name_off']])
        pieces.append(rec_bin)
        prev_end = r['blob_off'] + r['blob_len']
        expected[r['name']] = (w2, h2, blob)
        n += 1
    pieces.append(payload_orig[prev_end:])
    payload = bytearray(b''.join(pieces))
    # размеры заголовков цепочки FUN_0049bf70, накрывающих изменённые записи
    fix_chain(payload, recs, deltas)
    payload = bytes(payload)
    zi = find_zlib(raw)
    old_plen = len(payload_orig)
    new_plen = len(payload)
    comp = zlib.compress(payload, 9)
    head = bytearray(raw[:zi])
    old_clen = len(raw) - zi
    for i in range(8, len(head) - 3):
        if u32(head, i) == old_plen:
            struct.pack_into('<I', head, i, new_plen)
    for i in range(8, len(head) - 3):
        if u32(head, i) == old_clen:
            struct.pack_into('<I', head, i, len(comp))
    out = bytes(head) + comp
    open(map_path, 'wb').write(out)
    # round-trip: перечитали payload, сверили w/h и blob побитово
    chk = load_payload(map_path)
    recs2 = {r['name']: r for r in find_records(chk)}
    for r in recs:
        stem = r['name'][:-4]
        if stem not in expected:
            continue
        r2 = recs2[r['name']]
        if (r2['w'], r2['h']) != expected[r['name']][:2]:
            raise ValueError('%s: перечитанный w/h не равен ожидаемому' % r['name'])
        blob = chk[r2['blob_off']:r2['blob_off'] + r2['blob_len']]
        if blob != expected[r['name']][2]:
            raise ValueError('%s: перечитанный blob не совпадает' % r['name'])
    return n


def patch_up2x(map_path, updir, maxsrc=None):
    """payload: .tex-записи заменены 2x-апскейлом из `updir/<stem>.png`.

    Запись пересобирается целиком: имя то же, в шапке `w,h` удвоены и
    `blob_len = w*h*bpp` новый (поле [20..28] — лимит движка, не трогается);
    payload удлинняется, поля plen/clen контейнера правятся. Round-trip
    сверка: перечитанный blob побитово равен апскейлу, w/h вдвое больше.
    `maxsrc` — апскейл только записей со стороной <= maxsrc (движок не грузит
    2048²: проверка на дереве, записи 1024² остаются родными).
    """
    raw = open(map_path, 'rb').read()
    payload_orig = load_payload(map_path)
    recs = find_records(payload_orig)
    if not recs:
        return 0
    pieces = []
    prev_end = 0
    expected = {}
    deltas = {}
    for r in recs:
        stem = r['name'][:-4]
        if maxsrc is not None and r['w'] > maxsrc:
            pieces.append(payload_orig[prev_end:r['blob_off'] + r['blob_len']])
            prev_end = r['blob_off'] + r['blob_len']
            continue
        png_path = os.path.join(updir, stem + '.png')
        pw, ph, rows = png_decode(png_path)
        w2, h2, bpp = 2 * r['w'], 2 * r['h'], r['bpp']
        if (pw, ph) != (w2, h2):
            raise ValueError('%s: upscaled PNG %dx%d != 2x record %dx%d' %
                             (stem, pw, ph, w2, h2))
        blob = build_blob(rows, w2, h2, bpp)
        if len(blob) != w2 * h2 * bpp:
            raise ValueError('%s: blob %d != w*h*bpp' % (stem, len(blob)))
        if len(blob) > 33554432:
            raise ValueError('%s: blob %d exceeds engine cap 33554432' % (stem, len(blob)))
        hdr = bytearray(r['hdr'])
        struct.pack_into('<I', hdr, 0, w2)
        struct.pack_into('<I', hdr, 4, h2)
        struct.pack_into('<I', hdr, 36, len(blob))
        # name_len u32 записи живёт в gap'е перед именем (scan_tex: name_off =
        # строка имени); rec_bin = имя+шапка+blob, лишнего префикса нет.
        rec_bin = r['name'].encode('ascii') + bytes(hdr) + blob
        deltas[r['name_off']] = len(blob) - r['blob_len']
        pieces.append(payload_orig[prev_end:r['name_off']])
        pieces.append(rec_bin)
        prev_end = r['blob_off'] + r['blob_len']
        expected[r['name']] = blob
    pieces.append(payload_orig[prev_end:])
    payload = bytearray(b''.join(pieces))
    # размеры заголовков цепочки FUN_0049bf70, накрывающих изменённые записи
    fix_chain(payload, recs, deltas)
    payload = bytes(payload)
    # пересборка контейнера: plen и clen правятся (payload удлиннился)
    zi = find_zlib(raw)
    old_plen = len(payload_orig)
    new_plen = len(payload)
    comp = zlib.compress(payload, 9)
    head = bytearray(raw[:zi])
    old_clen = len(raw) - zi
    for i in range(8, len(head) - 3):
        if u32(head, i) == old_plen:
            struct.pack_into('<I', head, i, new_plen)
    for i in range(8, len(head) - 3):
        if u32(head, i) == old_clen:
            struct.pack_into('<I', head, i, len(comp))
    out = bytes(head) + comp
    open(map_path, 'wb').write(out)
    # round-trip: перечитали payload, сверили w/h/blob побитово
    chk = load_payload(map_path)
    recs2 = {r['name']: r for r in find_records(chk)}
    for r in recs:
        if maxsrc is not None and r['w'] > maxsrc:
            continue
        r2 = recs2[r['name']]
        if (r2['w'], r2['h']) != (2 * r['w'], 2 * r['h']):
            raise ValueError('%s: re-read w/h not doubled' % r['name'])
        blob = chk[r2['blob_off']:r2['blob_off'] + r2['blob_len']]
        if blob != expected[r['name']]:
            raise ValueError('%s: re-read blob mismatch' % r['name'])
    return len(recs)


def patch_record(map_path, quad='tl', color='white', solid=False, band=None,
                circle=None, bg='black', frac=0.5, alpha_mode='keep', dry=False):
    """payload: blob'ы всех .tex-записей -> паттерн; возвращает число записей.

    Для alpha-записей (bpp 4) альфа-плоскость берётся из режима `alpha_mode`:
    `keep` — родная альфа записи (движковый blend-пасс сохраняет поведение),
    `opaque` — альфа 255 из паттерна, `zero` — альфа 0 (пасс прозрачен).
    """
    raw = open(map_path, 'rb').read()
    payload_orig = load_payload(map_path)
    payload = bytearray(payload_orig)
    recs = find_records(payload)
    if not recs:
        return 0
    expected = {}
    for r in recs:
        w, h, bpp = r['w'], r['h'], r['bpp']
        S = w
        _, _, rows = build_pattern(S, quad, color, solid, band, circle, bg, frac)
        blob = bytearray(build_blob(rows, w, h, bpp))
        if bpp == 4:
            ob = payload_orig[r['blob_off']:r['blob_off'] + r['blob_len']]
            if alpha_mode == 'keep':
                blob[3::4] = ob[3::4]
            elif alpha_mode == 'zero':
                blob[3::4] = bytes(len(blob) // 4)
        blob = bytes(blob)
        if len(blob) != r['blob_len']:
            raise ValueError('%s: pattern blob %d != record blob_len %d' %
                             (r['name'], len(blob), r['blob_len']))
        payload[r['blob_off']:r['blob_off'] + r['blob_len']] = blob
        expected[r['blob_off']] = blob
    if dry:
        return len(recs)
    # пересборка контейнера: plen тот же, clen = длина нового zlib-поток
    zi = find_zlib(raw)
    old_plen = len(payload)
    comp = zlib.compress(bytes(payload), 9)
    head = bytearray(raw[:zi])
    old_clen = len(raw) - zi
    for i in range(8, len(head) - 3):
        v = u32(head, i)
        if v == old_clen:
            struct.pack_into('<I', head, i, len(comp))
    out = bytes(head) + comp
    open(map_path, 'wb').write(out)
    # проверка round-trip: перечитали payload, сверили с ожидаемым
    chk = load_payload(map_path)
    for r in find_records(chk):
        if chk[r['blob_off']:r['blob_off'] + r['blob_len']] != expected[r['blob_off']]:
            raise ValueError('%s: re-read blob mismatch' % r['name'])
    return len(recs)


def main():
    ap = argparse.ArgumentParser(description='patch .tex ground records with test pattern (scratch tree only)')
    ap.add_argument('--tree', required=True, help='scratch-дерево игры')
    ap.add_argument('--map', default=None, help='имя .map (по умолчанию — все карты)')
    ap.add_argument('--restore', action='store_true', help='вернуть оригиналы из backup')
    ap.add_argument('--quad', default='tl', choices=list(QUADS.keys()))
    ap.add_argument('--color', default='white', choices=list(COLORS.keys()))
    ap.add_argument('--band', default=None, choices=list(BANDS.keys()),
                    help='яркая полоса вдоль края записи (thin edge marker)')
    ap.add_argument('--circle', default=None, choices=list(COLORS.keys()),
                    help='круг по центру записи заданного цвета')
    ap.add_argument('--bg', default='black', choices=list(COLORS.keys()),
                    help='цвет фона записи')
    ap.add_argument('--frac', type=float, default=0.5,
                    help='диаметр круга как доля стороны записи (0.5 => 32 px для 64x64)')
    ap.add_argument('--alpha', default='keep', choices=('keep', 'opaque', 'zero'),
                    help='альфа-плоскость alpha-записей: родная / 255 / 0')
    ap.add_argument('--solid', action='store_true', help='вся запись одним цветом')
    ap.add_argument('--up2x', default=None, metavar='DIR',
                    help='заменить записи 2x-апскейлом из DIR (PNG по имени записи)')
    ap.add_argument('--rescale', default=None, metavar='DIR',
                    help='пересемплировать записи множителем --num/--den из DIR '
                         '(PNG родного размера по имени записи)')
    ap.add_argument('--num', type=int, default=1, help='числитель множителя стороны (--rescale)')
    ap.add_argument('--den', type=int, default=1, help='знаменатель множителя стороны (--rescale)')
    ap.add_argument('--record', default=None,
                    help='именовать одну запись (stem без .tex, напр. portal_domain_1)')
    ap.add_argument('--maxsrc', type=int, default=None,
                    help='апскейл только записей со стороной <= maxsrc')
    ap.add_argument('--dry', action='store_true', help='только проверка, без записи')
    a = ap.parse_args()

    tree = paths.norm(a.tree)
    if tree == paths.install_dir():
        raise SystemExit('refusing to touch the installed game: %s' % tree)
    maps = os.path.join(tree, 'data', 'maps')
    backup = os.path.join(tree, 'data', 'maps_ground_backup')
    if not os.path.isdir(maps):
        raise SystemExit('no data/maps in tree %s' % tree)

    if a.restore:
        if not os.path.isdir(backup):
            raise SystemExit('no backup dir %s' % backup)
        n = 0
        for fn in sorted(os.listdir(backup)):
            shutil.copyfile(os.path.join(backup, fn), os.path.join(maps, fn))
            n += 1
        print('restored %d .map files in %s' % (n, maps))
        return

    targets = [a.map] if a.map else sorted(f for f in os.listdir(maps) if f.endswith('.map'))
    os.makedirs(backup, exist_ok=True)
    total = 0
    for fn in targets:
        tgt = os.path.join(maps, fn)
        bp = os.path.join(backup, fn)
        if not os.path.isfile(bp):
            shutil.copyfile(tgt, bp)
        if a.rescale:
            src = None if a.rescale == 'self' else paths.norm(a.rescale)
            n = patch_rescale(tgt, src, a.num, a.den, a.record, a.maxsrc,
                              self_src=(a.rescale == 'self'))
        elif a.up2x:
            n = patch_up2x(tgt, paths.norm(a.up2x), a.maxsrc)
        else:
            n = patch_record(tgt, a.quad, a.color, a.solid, a.band,
                             a.circle, a.bg, a.frac, a.alpha, a.dry)
        total += n
        print('%s: %d records patched%s' % (fn, n, ' (dry)' if a.dry else ''))
    print('patched %d .tex records in %d maps' % (total, len(targets)))
    if a.dry:
        return
    print('съёмка оверленда дерева:')
    print("  LD_GAME='<tree>' LD_REPO='<repo>' LD_SCRATCH='<scratch>' powershell "
          "-NoProfile -ExecutionPolicy Bypass -File tools/archive/probes/probe_loadshot.ps1 "
          "3840 2160 1 <Tag> -Mode new -SkipClicks 2 -CharX 349 -CharY 658 -EnterCount 2 -LoadWait 5")
    print('откат: python3 tools/tex/patch_map_pattern.py --tree <tree> --restore')


if __name__ == '__main__':
    main()
