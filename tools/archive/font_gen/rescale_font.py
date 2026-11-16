#!/usr/bin/env python3
"""Генератор 16:9 шрифтов Lethal Dreams (лист глифов + таблица .fdf).

Движок грузит шрифт из data/fonts/<name>.fdf (таблица глифов `char x y h` в
пикселях листа) и лист data/textures/<name>.btf. Аргумент SetFont(size) на
отрисовку глифа не влияет (FML-эксперименты); глиф рисуется по таблице.

Генератор: для каждого глифа таблицы (x, y, h; ширина = до следующего x в
полосе, y-полоса = pitch size) масштабает box фактором f (x'=x*f, y'=y*f,
h'=h*f, w'=w*f) и сжимает пиксели листа по этому box; пишет:
  data/fonts/<name>_169.fdf   - масштабированная таблица
  data/textures/<name>_169.btf - масштабированный лист
  data/fonts/<name>_43.fdf, data/textures/<name>_43.btf - оригиналы (копии)
hd_res.Prepare() кладёт выбранный вариант в load-имена по aspect ratio.

  python3 rescale_font.py -f 0.71
"""
import sys, os, zlib, struct, binascii, argparse

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u
import pack_btf as p

GAME = paths.install_dir()
FONTS = ['ld_uncial12', 'ld_uncial20', 'ld_tahoma11', 'ld_numbers15']


def png_write_rgba(path, w, h, rows):
    raw = b''.join(b'\x00' + r for r in rows)
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def parse_fdf(path):
    """Возвращает (name, size, sheet, [(charstr, x, y, h), ...])."""
    lines = open(path, encoding='latin-1').read().split('\n')
    hdr = lines[0].split()
    name, size, sheet = hdr[0], int(hdr[1]), hdr[2]
    ents = []
    for ln in lines[1:]:
        ln = ln.strip()
        if not ln:
            continue
        p = ln.split()
        ch = p[0]
        if ch.startswith('%'):
            ch = chr(int(ch[1:]))
        ents.append((ch, int(p[1]), int(p[2]), int(p[3])))
    return name, size, sheet, ents


def scale_entries(ents, f):
    """x'=round(x*f), y'=round(y*f), h'=max(1,round(h*f)); ширина box = до
    следующего x в той же y-полосе (для последнего - до правого края)."""
    # группы по полосам y
    bands = {}
    for ch, x, y, h in ents:
        bands.setdefault(y, []).append((x, ch, h))
    out = []
    for y, grp in bands.items():
        grp.sort()
        for i, (x, ch, h) in enumerate(grp):
            w = grp[i + 1][0] - x if i + 1 < len(grp) else None
            out.append((ch, x, y, h, w))
    res = []
    for ch, x, y, h, w in out:
        nh = max(1, int(h * f + 0.5))
        ny = y + (h - nh)      # box сжат снизу вверх: base глифа остаётся на прежной линии
        res.append((ch, x, y, h, w, x, ny, nh, None))
    return res, bands


def scale_sheet(rows, w, h, res, f):
    """Сжать box каждого глифа и вписать в scaled-позицию; лист того же размера."""
    out = [bytearray(r) for r in rows]
    # все ячейки очищаем по альфе (лист был прозрачным фоном)
    for y in range(h):
        for x in range(w):
            out[y][x * 4 + 3] = 0
    for ch, x, y0, gh, gw, nx, ny, nh, nw in res:
        if gw is None:
            gw = w - x
        # ink сжимается внутри оригинального box (x, y0, w, gh) в (x, ny, w*f, nh)
        nw = max(1, int(gw * f + 0.5))
        # sampling: target pixel (tx,ty) -> source (x + tx/f, y0 + ty/f)
        for ty in range(nh):
            sy = int(y0 + ty / f + 0.5)
            sy = min(max(sy, y0), y0 + gh - 1)
            for tx in range(nw):
                sx = int(x + tx / f + 0.5)
                sx = min(max(sx, x), x + gw - 1)
                src = rows[sy][sx * 4:sx * 4 + 4]
                out[ny + ty][(nx + tx) * 4:(nx + tx) * 4 + 4] = bytes(src)
    return [bytes(r) for r in out]


def fdf_text(name, size, sheet, res):
    """Формат оригинала: header, пустая строка, группы entries по полосам."""
    out = [name + ' ' + str(size) + ' ' + sheet, '']
    # порядок оригинала = порядок таблицы (сохраняем как было)
    for ch, x, y, gh, gw, nx, ny, nh, nw in res:
        if ch == ' ':
            c = '%32'
        elif ord(ch) == 37:
            c = '%37'
        elif ord(ch) > 127:
            c = ch  # latin-1 символ пишется как есть
        else:
            c = ch
        out.append('%s%5d%4d%3d' % (c, nx, ny, nh))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-f', type=float, default=0.71)
    ap.add_argument('-o', default=paths.scratch_dir())
    a = ap.parse_args()
    for name in FONTS:
        fdf = GAME + '/data/fonts/' + name + '.fdf'
        btf = GAME + '/data/textures/' + name + '.btf'
        nm, size, sheet, ents = parse_fdf(fdf)
        res, bands = scale_entries(ents, a.f)
        d = u.parse_btf(open(btf, 'rb').read())
        pw, ph, bpp, P = d['dims'][0]
        rows = u.decode32(d['blob'], pw, ph, 'BGRA', False)
        rows2 = scale_sheet(rows, pw, ph, res, a.f)
        png = a.o + '/font_' + name + '_169.png'
        png_write_rgba(png, pw, ph, rows2)
        p.pack_one(png, btf, GAME + '/data/textures/' + name + '_169.btf')
        # оригиналы для 4:3 - точные копии
        open(GAME + '/data/textures/' + name + '_43.btf', 'wb').write(open(btf, 'rb').read())
        open(GAME + '/data/fonts/' + name + '_43.fdf', 'wb').write(open(fdf, 'rb').read())
        # 16:9: fdf тот же (геометрия box оригинальна), лист со сжатым ink
        open(GAME + '/data/fonts/' + name + '_169.fdf', 'wb').write(open(fdf, 'rb').read())
        print(name, pw, 'x', ph, 'size', size, 'glyphs', len(res), 'f=', a.f)

if __name__ == '__main__':
    main()
