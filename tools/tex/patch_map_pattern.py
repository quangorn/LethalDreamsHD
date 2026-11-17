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
from pack_btf import build_blob  # noqa: E402
from scan_tex import load_payload, find_records, u32, MAGIC, ZLIB_MAGICS  # noqa: E402
from test_pattern import build as build_pattern, COLORS, QUADS, BANDS  # noqa: E402


def find_zlib(raw):
    for m in ZLIB_MAGICS:
        i = raw.find(m, len(MAGIC))
        if i >= 0:
            return i
    raise ValueError('no zlib stream')


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
