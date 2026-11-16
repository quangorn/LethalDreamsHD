# rescale_font3.py - корректный генератор 16:9 листов шрифтов Lethal Dreams.
#
# Модель (проверено офлайн по родным листам и .fdf):
#   .fdf: header `name pitch sheet`, затем `char x y adv` где
#       x   - левый край ячейки глифа в листе (пиксели листа)
#       y   - верх y-полосы (кратно pitch)
#       adv - ширина/адванс глифа в пикселях листа (ровно ширина ячейки: ink
#             всегда лежит внутри [x, x+adv), проверено для всех 552 глифов)
#   Лист 32-bit (blob = w*h*4), top-down, ink = белое с чёрной обводкой, каналы
#   серые -> порядок RGB не важен, alpha работает как маска.
#
# Компенсация: движок рисует канвас 1024x768 на окно по осям (sx=w/1024,
# sy=h/768), поэтому при 16:9 глифы шире в sx/sy = 1.333. Сжимаем ячейку
# ЦЕЛИКОМ по горизонтали: и ink, и адванс на k = sy/sx = 768*w/(1024*h)
# (0.75 при 3840x2160 и 2560x1440, 1.0 при 1280x960 -> identity).
# Вертикаль не трогаем: доля высоты текста 0.557 уже равна референсной 0.56.
#
# База - ТОЛЬКО бэкап родных листов paths.btf_backup_dir()/*_orig.btf, никогда
# игровой файл (иначе повторный прогон сожмёт уже сжатое).
#
#   python3 rescale_font3.py -k 0.75 [-o paths.scratch_dir('ld169')]
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
    """(name, pitch, sheet, [(raw_char_token, ch, x, y, adv), ...]) - сохраняем
    исходный токен символа (%32 и т.п.), чтобы fdf был побайтово предсказуем."""
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


def render_band_cell(rows, x, y, adv, pitch, ph, k):
    """Сжать ячейку [x, x+adv) x [y, y+pitch) по горизонтали в t = round(adv*k)
    столбцов. Alpha - по покрытию (>=0.5 -> 255), RGB - среднее по ink.
    Возвращает (t, list of rows(bytes len t*4))."""
    t = max(1, int(round(adv * k)))
    out = []
    for yy in range(y, min(y + pitch, ph)):
        r = rows[yy]
        line = bytearray(t * 4)
        for i in range(t):
            a0 = i * adv / float(t)
            a1 = (i + 1) * adv / float(t)
            cov = 0.0
            rs = gs = bs = 0.0
            wsum = 0.0
            c0 = int(a0)
            c1 = min(adv, int(a1) + 1)
            for c in range(c0, c1):
                s = max(a0, c)
                e = min(a1, c + 1)
                wt = e - s
                if wt <= 0:
                    continue
                o = (x + c) * 4
                al = r[o + 3]
                if al:
                    cov += wt
                    wsum += wt * al
                    rs += r[o] * wt * al
                    gs += r[o + 1] * wt * al
                    bs += r[o + 2] * wt * al
            q = i * 4
            span = a1 - a0
            if wsum > 0 and span > 0:
                line[q] = int(round(rs / wsum))
                line[q + 1] = int(round(gs / wsum))
                line[q + 2] = int(round(bs / wsum))
                line[q + 3] = min(255, int(round(wsum / span)))
        out.append(bytes(line))
    return t, out


def gen(name, k, outdir):
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
    newpos = {}          # idx -> (nx, nadv)
    for y, grp in bands.items():
        grp.sort()
        cx = 0
        for i, (x, idx, adv) in enumerate(grp):
            t = max(1, int(round(adv * k)))
            newpos[idx] = (cx, t)
            gap = (grp[i + 1][0] - (x + adv)) if i + 1 < len(grp) else 0
            cx += t + (int(round(gap * k)) if gap > 0 else 0)
        if cx > pw:
            raise SystemExit('%s: полоса y=%d не влезает: %d > %d' % (name, y, cx, pw))

    # новый лист: размер тот же, ink переносим в новые ячейки
    empty = b'\x00' * (pw * 4)
    out = [bytearray(empty) for _ in range(ph)]
    for y, grp in bands.items():
        for x, idx, adv in grp:
            nx, nadv = newpos[idx]
            t, cell = render_band_cell(rows, x, y, adv, pitch, ph, k)
            assert t == nadv
            for j, line in enumerate(cell):
                yy = y + j
                if yy >= ph:
                    break
                out[yy][nx * 4:(nx + nadv) * 4] = line

    os.makedirs(outdir, exist_ok=True)
    png = os.path.join(outdir, 'font_' + name + '_169.png')
    png_write_rgba(png, pw, ph, [bytes(r) for r in out])
    btf_out = os.path.join(outdir, name + '_169.btf')
    p.pack_one(png, btf_in, btf_out)

    # fdf: тот же заголовок, тот же порядок строк, новые x и adv
    nl = '\r\n' if crlf else '\n'
    lines = ['%s %d %s' % (fname, pitch, sheet), '']
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv = newpos[idx]
        lines.append('%s%5d%4d%3d' % (tok, nx, y, nadv))
    fdf_out = os.path.join(outdir, name + '_169.fdf')
    open(fdf_out, 'wb').write((nl.join(lines) + nl).encode('latin-1'))

    # контроль: ink внутри новых ячеек, сумма адвансов
    d2 = u.parse_btf(open(btf_out, 'rb').read())
    w2, h2, b2, _ = d2['dims'][0]
    assert (w2, h2) == (pw, ph) and b2 == 4
    rows2 = u.decode32(d2['blob'], w2, h2, 'RGBA', False)
    bad = 0
    ink = 0
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv = newpos[idx]
        for yy in range(y, min(y + pitch, ph)):
            r = rows2[yy]
            for xx in range(nx, nx + nadv):
                if r[xx * 4 + 3]:
                    ink += 1
        # нет ли ink сразу за правой границей ячейки
        if nx + nadv < pw:
            for yy in range(y, min(y + pitch, ph)):
                if rows2[yy][(nx + nadv) * 4 + 3]:
                    bad += 1
                    break
    adv_orig = sum(e[4] for e in ents)
    adv_new = sum(newpos[i][1] for i in range(len(ents)))
    print('%-13s %dx%d pitch=%d glyphs=%d bars=%d k=%.3f adv %d->%d (%.3f) ink=%d bleed=%d'
          % (name, pw, ph, pitch, len(ents), len(bands), k, adv_orig, adv_new,
             adv_new / float(adv_orig), ink, bad))
    return btf_out, fdf_out


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
    ap.add_argument('-k', type=float, default=0.75)
    ap.add_argument('-o', default=paths.scratch_dir('ld169'))
    ap.add_argument('--install', action='store_true')
    a = ap.parse_args()
    for nm in FONTS:
        btf_out, fdf_out = gen(nm, a.k, a.o)
        if a.install:
            install(nm, btf_out, fdf_out, a.o)
            print('   installed sidecars %s_169 + %s_43' % (nm, nm))


if __name__ == '__main__':
    main()
