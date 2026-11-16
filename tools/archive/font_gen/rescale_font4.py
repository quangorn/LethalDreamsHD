# rescale_font4.py - генератор 16:9 листов шрифтов Lethal Dreams (3 независимых фактора).
#
# Модель рендера (подтверждена реверсом и живыми замерами, см. SCALING_HANDOFF.md 7-8):
#   `.fdf` строка = `char x y adv`: x - левый край ячейки глифа в листе, y - верх
#   y-полосы (кратно pitch из заголовка), adv - ширина ячейки = АДВАНС глифа.
#   Движок рисует квад шириной adv (адванс берёт из таблицы) и высотой pitch
#   (`GetFontSize()` = pitch), UV = ячейка листа [x, x+adv) x [y, y+pitch).
#   Значит: адванс = экранному шагу глифа, ink = картинке внутри квада.
#
# Факторы (все относительно РОДНОго листа, независимы):
#   -kx  адванс (и расстановка ячеек в полосе). 0.75 = текущий k = sy/sx,
#        компенсирует растяжение движка per-axis (sx=w/1024, sy=h/768) при 16:9.
#   -fx  ширина ink глифа. 0.7125 = 0.75 * 0.95 ->additional сжатие по горизонтали на 5%.
#        Адванс при этом НЕ трогается -> межбуквенные просветы (горизонтальный паддинг) растут.
#   -fy  высота ink глифа. 0.85 = сузить по вертикали на 15%. Якорь - НИЖНЯЯ строка ink
#        (базовая линия и нижняя часть полосы сохраняются), сжатие вверх.
#        Pitch (межстрочный шаг) НЕ трогается -> индекс глифов в полосе не рвётся,
#        компенсация RowS в hd_scale остаётся валидной.
#   При kx=fx=fy=1 -> побитово родной лист.
#
# База - ТОЛЬКО бэкап родных листов paths.btf_backup_dir()/*_orig.*, никогда игровой файл.
#
#   python3 rescale_font4.py -kx 0.75 -fx 0.7125 -fy 0.85 [-o DIR] [--install]
import sys, os, shutil, zlib, struct, binascii, argparse

_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u
import pack_btf as p

BAK = paths.btf_backup_dir()
FONTS = ['ld_uncial12', 'ld_uncial20', 'ld_tahoma11', 'ld_numbers15']


def png_write_rgba(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', binascii.crc32(c) & 0xffffffff)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def parse_fdf(path):
    """(name, pitch, sheet, [(tok, ch, x, y, adv), ...], crlf) - токен символа
    ('%32' и т.п.) сохраняем, чтобы fdf был побайтово предсказуем."""
    raw = open(path, 'rb').read().decode('latin-1')
    crlf = raw.startswith('\xef') or '\r\n' in raw
    lines = raw.replace('\r\n', '\n').split('\n')
    hdr = lines[0].split()
    name, pitch, sheet = hdr[0], int(hdr[1]), hdr[2]
    ents = []
    for ln in lines[1:]:
        ln = ln.strip()
        if not ln:
            continue
        t = ln.split()
        tok = t[0]
        ch = chr(int(tok[1:])) if tok.startswith('%') else tok
        ents.append((tok, ch, int(t[1]), int(t[2]), int(t[3])))
    return name, pitch, sheet, ents, crlf


def ink_bbox(rows, x, y, adv, ph, pitch):
    """bbox пикселей с alpha>0 внутри ячейки [x,x+adv) x [y,y+pitch).
    Возвращает (ix0, iy0, iw, ih) в относительных координатах полосы, None если пусто."""
    ix0 = ix1 = iy0 = iy1 = -1
    lim = min(y + pitch, ph)
    for yy in range(y, lim):
        r = rows[yy]
        for xx in range(x, min(x + adv, len(r) // 4)):
            if r[xx * 4 + 3]:
                relx = xx - x
                if ix0 < 0:
                    ix0 = ix1 = relx
                    iy0 = iy1 = yy - y
                else:
                    if relx < ix0:
                        ix0 = relx
                    if relx > ix1:
                        ix1 = relx
                    if yy - y < iy0:
                        iy0 = yy - y
                    if yy - y > iy1:
                        iy1 = yy - y
    if ix0 < 0:
        return None
    return ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1


def resample(rows, x, y, ix0, iy0, iw, ih, nw, nh):
    """Сжать ink-область (ix0,iy0) sized iw x ih в nw x nh (площадное среднее
    RGB по альфе, alpha = среднее покрытия). Возвращает список строк nw*4 байт."""
    out = []
    for j in range(nh):
        line = bytearray(nw * 4)
        sy0 = j * ih / float(nh)
        sy1 = (j + 1) * ih / float(nh)
        r0 = int(sy0)
        r1 = min(ih, int(sy1) + 1)
        for i in range(nw):
            sx0 = i * iw / float(nw)
            sx1 = (i + 1) * iw / float(nw)
            c0 = int(sx0)
            c1 = min(iw, int(sx1) + 1)
            wsum = 0.0
            rs = gs = bs = 0.0
            for rr in range(r0, r1):
                src = rows[y + iy0 + rr]
                vy = min(sy1, rr + 1) - max(sy0, rr)
                for cc in range(c0, c1):
                    vx = min(sx1, cc + 1) - max(sx0, cc)
                    wt = vx * vy
                    if wt <= 0:
                        continue
                    o = (x + ix0 + cc) * 4
                    al = src[o + 3]
                    if al:
                        wsum += wt * al
                        rs += src[o] * wt * al
                        gs += src[o + 1] * wt * al
                        bs += src[o + 2] * wt * al
            q = i * 4
            area = (sx1 - sx0) * (sy1 - sy0)
            if wsum > 0 and area > 0:
                line[q] = int(round(rs / wsum))
                line[q + 1] = int(round(gs / wsum))
                line[q + 2] = int(round(bs / wsum))
                line[q + 3] = min(255, int(round(wsum / area)))
        out.append(bytes(line))
    return out


def gen(name, kx, fx, fy, outdir):
    btf_in = os.path.join(BAK, name + '_orig.btf')
    fdf_in = os.path.join(BAK, name + '_orig.fdf')
    d = u.parse_btf(open(btf_in, 'rb').read())
    pw, ph, bpp, _ = d['dims'][0]
    assert bpp == 4, 'ожидался 32-bit лист'
    rows = u.decode32(d['blob'], pw, ph, 'RGBA', False)
    fname, pitch, sheet, ents, crlf = parse_fdf(fdf_in)

    # полосы по y; внутри полосы - по x
    bands = {}
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        bands.setdefault(y, []).append((x, idx, adv))

    newpos = {}      # idx -> (nx, nadv)
    cellmap = {}     # idx -> (nx, nadv, lb, top, nw, nh)
    for y, grp in bands.items():
        grp.sort()
        cx = 0
        for i, (x, idx, adv) in enumerate(grp):
            t = max(1, int(round(adv * kx)))
            newpos[idx] = (cx, t)
            bb = ink_bbox(rows, x, y, adv, ph, pitch)
            if bb is None:
                cellmap[idx] = (cx, t, 0, 0, 0, 0)
            else:
                ix0, iy0, iw, ih = bb
                nw = max(1, int(round(iw * fx)))
                nh = max(1, int(round(ih * fy)))
                lb = int(round(ix0 * fx))
                if lb + nw > t:
                    lb = max(0, t - nw)
                top = iy1_anchor(iy0, ih, nh)
                cellmap[idx] = (cx, t, lb, top, nw, nh)
            gap = (grp[i + 1][0] - (x + adv)) if i + 1 < len(grp) else 0
            cx += t + (int(round(gap * kx)) if gap > 0 else 0)
        if cx > pw:
            raise SystemExit('%s: полоса y=%d не влезает: %d > %d' % (name, y, cx, pw))

    # новый лист: тот же размер, ink в новые ячейки; нижняя строка ink на той же
    # строке полосы (якорь - базовая линия)
    empty = b'\x00' * (pw * 4)
    out = [bytearray(empty) for _ in range(ph)]
    for y, grp in bands.items():
        for x, idx, adv in grp:
            nx, nadv, lb, top, nw, nh = cellmap[idx]
            if nw == 0:
                continue
            bb = ink_bbox(rows, x, y, adv, ph, pitch)
            ix0, iy0, iw, ih = bb
            cell = resample(rows, x, y, ix0, iy0, iw, ih, nw, nh)
            for j, line in enumerate(cell):
                yy = y + top + j
                if yy >= ph:
                    break
                out[yy][nx * 4:(nx + nw) * 4] = line

    os.makedirs(outdir, exist_ok=True)
    png = os.path.join(outdir, 'font_' + name + '_169.png')
    png_write_rgba(png, pw, ph, [bytes(r) for r in out])
    btf_out = os.path.join(outdir, name + '_169.btf')
    p.pack_one(png, btf_in, btf_out)

    # fdf: заголовок (pitch) не тронут, порядок строк тот же, новые x и adv
    nl = '\r\n' if crlf else '\n'
    lines = ['%s %d %s' % (fname, pitch, sheet), '']
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv = newpos[idx]
        lines.append('%s%5d%4d%3d' % (tok, nx, y, nadv))
    fdf_out = os.path.join(outdir, name + '_169.fdf')
    open(fdf_out, 'wb').write((nl.join(lines) + nl).encode('latin-1'))

    # контроль: индексы полос, ink внутри ячеек, bleed за правой границей, суммарный ink
    d2 = u.parse_btf(open(btf_out, 'rb').read())
    w2, h2, b2, _ = d2['dims'][0]
    assert (w2, h2) == (pw, ph) and b2 == 4
    rows2 = u.decode32(d2['blob'], w2, h2, 'RGBA', False)
    bad = 0
    ink = 0
    over_band = 0
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv, lb, top, nw, nh = cellmap[idx]
        lo = [None, None]
        for yy in range(y, min(y + pitch, ph)):
            r = rows2[yy]
            for xx in range(nx, min(nx + nadv, pw)):
                if r[xx * 4 + 3]:
                    ink += 1
                    if lo[0] is None:
                        lo[0] = yy
                    lo[1] = yy
        # bleed сразу за правым краем ячейки
        if nx + nadv < pw:
            for yy in range(y, min(y + pitch, ph)):
                if rows2[yy][(nx + nadv) * 4 + 3]:
                    bad += 1
                    break
        # ink должен остаться в полосе и касаться якорной строки (низ сохранён)
        if lo[0] is not None:
            if lo[0] < y or lo[1] > y + pitch - 1:
                over_band += 1
            if lo[1] != y + top + nh - 1:
                over_band += 1
    adv_orig = sum(e[4] for e in ents)
    adv_new = sum(newpos[i][1] for i in range(len(ents)))
    ink_orig = sum(1 for yy in range(ph) for xx in range(pw // 4 * 4)
                   if rows[yy][xx * 4 + 3] and xx < pw)
    print('%-12s %dx%d pitch=%-2d glyphs=%-3d bands=%-2d kx=%.4f fx=%.4f fy=%.4f  '
          'adv %d->%d (%.4f)  ink %d->%d (%.4f)  bleed=%d bandviol=%d'
          % (name, pw, ph, pitch, len(ents), len(bands), kx, fx, fy,
             adv_orig, adv_new, adv_new / float(adv_orig), ink_orig, ink,
             ink / float(ink_orig), bad, over_band))
    return btf_out, fdf_out


def iy1_anchor(iy0, ih, nh):
    """Якорь: нижняя строка ink остаётся на той же относительной строке полосы
    (iy0+ih-1), сжатие идёт вверх. top = iy1 - nh + 1."""
    return (iy0 + ih - 1) - nh + 1


def install(name, btf_out, fdf_out, outdir):
    """Разложить сайдкары в игру: *_169 (сжатые) и *_43 (побайтово родные).
    load-имена возвращаем в родное состояние - выбор делает hd_res.Prepare()."""
    GAME = paths.install_dir()
    shutil.copyfile(btf_out, os.path.join(GAME, 'data', 'textures', name + '_169.btf'))
    shutil.copyfile(fdf_out, os.path.join(GAME, 'data', 'fonts', name + '_169.fdf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.btf'),
                    os.path.join(GAME, 'data', 'textures', name + '_43.btf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.fdf'),
                    os.path.join(GAME, 'data', 'fonts', name + '_43.fdf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.btf'),
                    os.path.join(GAME, 'data', 'textures', name + '.btf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.fdf'),
                    os.path.join(GAME, 'data', 'fonts', name + '.fdf'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-kx', type=float, default=0.75)
    ap.add_argument('-fx', type=float, default=0.7125)
    ap.add_argument('-fy', type=float, default=0.85)
    ap.add_argument('-o', default=paths.scratch_dir('ld169b'))
    ap.add_argument('--install', action='store_true')
    a = ap.parse_args()
    for nm in FONTS:
        btf_out, fdf_out = gen(nm, a.kx, a.fx, a.fy, a.o)
        if a.install:
            install(nm, btf_out, fdf_out, a.o)
            print('   installed sidecars %s_169 + %s_43' % (nm, nm))


if __name__ == '__main__':
    main()
