#!/usr/bin/env python3
"""Генератор 16:9 листов глифов Lethal Dreams (компенсация горизонтального растяжения).

Модель движка (NOTES.md): canvas 1024x768 растягивается на экран sx=w/1024, sy=h/768;
глиф рисуется квадом pitch x h, UV = бокс глифа из .fdf. При 16:9 sx/sy = 1.333,
поэтому глифы шире референса в 1.333 раза. Компенсация: сжать ink глифа по
горизонтали фактором k = sy/sx = 768*w/(1024*h) (0.75 при 3840x2160 и 2560x1440,
1.0 при 1280x960 -> identity).

Таблица .fdf НЕ меняется: позиции x, y и высота h остаются родными, layout не
сдвигается. Сжимается только ink внутри бокса глифа (ширина бокса = до
следующего x в полосе), alpha по покрытию, RGB по среднему.


import os
import sys
sys.path.insert(0, _ROOT)
import paths
БАЗА читается из бэкапа родных листов (paths.btf_backup_dir()/<name>_orig.btf),
никогда из игрового файла - иначе Prepare() в 16:9 кладёт _169 на load-имя и
следующий прогон делает _43 копией сжатого листа.

  python3 rescale_font2.py -k 0.75
"""
import sys, os, zlib, struct, binascii, argparse

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u
import pack_btf as p

BAK = paths.btf_backup_dir()
FONTS = ['ld_uncial12', 'ld_uncial20', 'ld_tahoma12' and 'ld_uncial20']  # placeholder fixed below
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
    """(name, size, sheet, [(charstr, x, y, h), ...]) - header `name size sheet`."""
    lines = open(path, encoding='latin-1').read().split('\n')
    hdr = lines[0].split()
    name, size, sheet = hdr[0], int(hdr[1]), hdr[2]
    ents = []
    for ln in lines[1:]:
        ln = ln.strip()
        if not ln:
            continue
        t = ln.split()
        ents.append((t[0], int(t[1]), int(t[2]), int(t[3])))
    return name, size, sheet, ents


def resample_row(rgb, alpha, x0, x1, k):
    """Сжимает столбцы [x0, x1) по горизонтали фактором k (k<1).
    Возвращает список (r,g,b,a) длины t = max(1, round((x1-x0)*k)).
    Alpha - по покрытию (есть ink в окне -> 255), RGB - среднее по ink-пикселям окна."""
    w = x1 - x0
    t = max(1, int(round(w * k)))
    out = []
    for i in range(t):
        a = i * w / t
        b = (i + 1) * w / t
        lo = int(a)
        hi = min(x1, int(b) + 1)
        # взвешенное окно покрывает [a, b)
        rs = gs = bs = 0.0
        cov = 0.0
        for c in range(lo, hi):
            s = max(a, c)
            e = min(b, c + 1)
            wt = e - s
            if wt <= 0:
                continue
            idx = x0 + c - lo
            if alpha[c]:
                cov += wt
                rs += rgb[0][c] * wt
                gs += rgb[1][c] * wt
                bs += rgb[2][c] * wt
        if cov > 0:
            out.append((int(round(rs / cov)), int(round(gs / cov)), int(round(bs / cov)), 255))
        else:
            out.append((0, 0, 0, 0))
    return out


def gen(nm, k, outdir):
    base_btf = BAK + '/' + nm + '_orig.btf'
    base_fdf = GAME_FDF + '/' + nm + '.fdf'
    d = u.parse_btf(open(base_btf, 'rb').read())
    pw, ph, bpp, P = d['dims'][0]
    rows = u.decode32(d['blob'], pw, ph, 'BGRA', False)
    name, size, sheet, ents = parse_fdf(base_fdf)
    # полосы по y, глифы по x; ширина бокса = до следующего x (последний - до края листа)
    bands = {}
    for ch, x, y, h in ents:
        bands.setdefault(y, []).append((x, ch, h))
    changed = 0
    for y, grp in bands.items():
        grp.sort()
        for i, (x, ch, h) in enumerate(grp):
            x1 = grp[i + 1][0] if i + 1 < len(grp) else pw
            w = x1 - x
            t = max(1, int(round(w * k)))
            if t == w:
                continue
            for yy in range(y, min(y + h, ph)):
                r = rows[yy]
                rgb = [r[(x + c) * 4 + j] for c in range(w) for j in (0,)]
                rgb = [r[(x + c) * 4] for c in range(w)], [r[(x + c) * 4 + 1] for c in range(w)], [r[(x + c) * 4 + 2] for c in range(w)]
                alpha = [r[(x + c) * 4 + 3] > 0 for c in range(w)]
                new = resample_row(rgb, alpha, x, x1, k)
                for c in range(t):
                    rr, gg, bb, aa = new[c]
                    o = (x + c) * 4
                    r[o], r[o + 1], r[o + 2], r[o + 3] = rr, gg, bb, aa
                for c in range(t, w):
                    o = (x + c) * 4
                    r[o], r[o + 1], r[o + 2], r[o + 3] = 0, 0, 0, 0
            changed += 1
    png = outdir + '/font_' + nm + '_169.png'
    png_write_rgba(png, pw, ph, [bytes(r) for r in rows])
    out = outdir + '/' + nm + '_169.btf'
    p.pack_one(png, base_btf, out)
    print(nm, pw, 'x', ph, 'pitch', size, 'glyphs', len(ents), 'resampled', changed, 'k =', k, '->', out)
    return out


def inkcount(rows):
    n = 0
    for r in rows:
        for x in range(0, len(r), 4):
            if max(r[x], r[x + 1], r[x + 2]) > 60:
                n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-k', type=float, default=0.75)
    ap.add_argument('-o', default=paths.scratch_dir('ld169'))
    ap.add_argument('-i', default=paths.game_path('data', 'fonts'))
    global GAME_FDF
    GAME_FDF = ap.parse_args().i
    outdir = ap.parse_args().o
    os.makedirs(outdir, exist_ok=True)
    k = ap.parse_args().k
    outs = []
    for nm in FONTS:
        o = gen(nm, k, outdir)
        d = u.parse_btf(open(o, 'rb').read())
        w, h, bpp, P = d['dims'][0]
        rows = u.decode32(d['blob'], w, h, 'BGRA', False)
        print('   ink', inkcount(rows), 'of', w * h)
        outs.append(o)
    return outs


GAME_FDF = paths.game_path('data', 'fonts')

if __name__ == '__main__':
    main()
