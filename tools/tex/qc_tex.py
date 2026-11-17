# tools/tex/qc_tex.py — QC почвенных текстур (фаза D плана docs/TASK_tex_unpacker.md):
#   1. round-trip lossless: blob -> PNG -> blob побитово (как делалось для .btf);
#   2. статистика по всем 44: размеры, bpp, уникальные цвета, доля gray;
#   3. доля пикселей, попадающих в предсказанную палитру генератора
#      (пиксель тайла `figures/textures/*.bmp` x цвет `GROUPS`) — known-answer.
#
# Только чтение игры; сверка идёт по декоду `tools/tex/unpack_tex.py`.
#   python3 tools/tex/qc_tex.py            # round-trip + статистика по всем 44
#   python3 tools/tex/qc_tex.py --tiles    # добавить сверку палитры генератора
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from pack_btf import png_decode, build_blob  # noqa: E402
from unpack_btf import decode24, decode32  # noqa: E402
from scan_tex import load_payload, find_records  # noqa: E402
from tile_match import read_groups, rgb_of, read_bmp_pixels  # noqa: E402

WR = paths.game_path('data', 'wrender')


def roundtrip(blob, w, h, bpp):
    """blob -> PNG (temp) -> blob; возвращает True, если побитово совпало."""
    import tempfile
    tmp = os.path.join(tempfile.mkdtemp(prefix='ldtex_'), 'rt.png')
    rows = decode24(blob, w, h, 'RGB', False) if bpp == 3 else decode32(blob, w, h, 'RGBA', False)
    from unpack_btf import png_write
    png_write(tmp, w, h, rows)
    pw, ph, rrows = png_decode(tmp)
    if (pw, ph) != (w, h):
        return False
    return build_blob(rrows, w, h, bpp) == blob


def stats(blob, bpp):
    uniq = set()
    gray = 0
    n = 0
    for i in range(0, len(blob), bpp):
        c = blob[i:i + 3]
        uniq.add(c)
        if c[0] == c[1] == c[2]:
            gray += 1
        n += 1
    return len(uniq), gray, n


def predicted_palette():
    """Множество предсказанных цветов генератора: пиксель тайла * цвет GROUPS."""
    groups = read_groups()
    tdir = os.path.join(WR, 'figures', 'textures')
    pal = set()
    ntiles = 0
    for fn in sorted(os.listdir(tdir)):
        if not fn.lower().endswith('.bmp'):
            continue
        try:
            w, h, rows = read_bmp_pixels(os.path.join(tdir, fn))
        except ValueError:
            continue
        base = fn.lower()
        color = None
        for cid, (name, path, col) in groups.items():
            if path.replace('\\', '/').lower().endswith('/' + base):
                color = rgb_of(col)
                break
        if color is None:
            continue
        ntiles += 1
        flat = b''.join(rows)
        for i in range(0, len(flat), 3):
            pal.add((flat[i] * color[0] // 255, flat[i + 1] * color[1] // 255,
                     flat[i + 2] * color[2] // 255))
    return pal, ntiles


def main():
    import argparse
    ap = argparse.ArgumentParser(description='ground-texture QC (round-trip + statistics)')
    ap.add_argument('--tiles', action='store_true', help='сверка палитры генератора')
    ap.add_argument('--sample', type=int, default=8, help='каждый N-й пиксель для сверки палитры')
    a = ap.parse_args()

    maps_dir = paths.game_path('data', 'maps')
    pal = None
    if a.tiles:
        pal, nt = predicted_palette()
        print('predicted palette colors=%d from %d tiles x GROUPS colors' % (len(pal), nt))

    total = 0
    rt_ok = 0
    rows = []
    for fn in sorted(os.listdir(maps_dir)):
        if not fn.endswith('.map'):
            continue
        payload = load_payload(os.path.join(maps_dir, fn))
        for r in find_records(payload):
            blob = payload[r['blob_off']:r['blob_off'] + r['blob_len']]
            ok = roundtrip(blob, r['w'], r['h'], r['bpp'])
            uniq, gray, npx = stats(blob, r['bpp'])
            share = ''
            if pal is not None:
                hit = 0
                cnt = 0
                step = r['bpp'] * a.sample
                for i in range(0, len(blob), step):
                    c = (blob[i], blob[i + 1], blob[i + 2])
                    cnt += 1
                    if c in pal:
                        hit += 1
                share = ' palhit=%.1f%%' % (100.0 * hit / cnt)
            rows.append((r['name'], r['w'], r['h'], r['bpp'], r['blob_len'], uniq,
                         100.0 * gray / npx, ok, share))
            total += 1
            rt_ok += 1 if ok else 0

    print('%-26s %5s %3s %9s %8s %6s %6s %s' % ('name', 'size', 'bpp', 'blob_len',
                                                'uniq', 'gray%', 'rt', 'share'))
    for nm, w, h, bpp, bl, uniq, gp, ok, share in rows:
        print('%-26s %5d %3d %9d %8d %6.1f %6s%s' % (nm, w, bpp, bl, uniq, gp,
                                                      'OK' if ok else 'FAIL', share))
    print('records=%d round-trip lossless=%d/%d' % (total, rt_ok, total))
    if rt_ok != total:
        sys.exit(1)


if __name__ == '__main__':
    main()
