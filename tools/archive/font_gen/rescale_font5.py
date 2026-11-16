# rescale_font5.py - генератор 16:9 листов шрифтов с полным масштабом k (adv, ink, pitch).
#
# Отличие от rescale_font4: pitch тоже масштабируется (new_pitch = floor(pitch*k)),
# полосы листа перекладываются с шагом new_pitch, ink сжимается по обеим осям (fx=fy=k)
# с якорем по нижней строке ink. UV = cell [x,x+adv) x [y,y+pitch) - консистентен
# (ink <= cell), quad = adv x pitch - тот же размер: рисуем preshrunk-ink без кропа.
#
# База - ТОЛЬКО бэкап родных листов paths.btf_backup_dir()/*_orig.*.
#   python3 rescale_font5.py -k 0.75 [-o DIR] [--install]
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


def gen(name, k, outdir):
    btf_in = os.path.join(BAK, name + '_orig.btf')
    fdf_in = os.path.join(BAK, name + '_orig.fdf')
    d = u.parse_btf(open(btf_in, 'rb').read())
    pw, ph, bpp, _ = d['dims'][0]
    assert bpp == 4, 'ожидался 32-bit лист'
    rows = u.decode32(d['blob'], pw, ph, 'RGBA', False)
    fname, pitch, sheet, ents, crlf = parse_fdf(fdf_in)
    new_pitch = int(k * pitch)  # floor

    # полосы по y; внутри полосы - по x
    bands = {}
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        bands.setdefault(y, []).append((x, idx, adv))

    newpos = {}      # idx -> (nx, nadv, ny)
    cellmap = {}     # idx -> (nx, nadv, ny, lb, top, nw, nh)
    ys = sorted(bands.keys())
    ymap = {y: int(k * y) for y in ys}  # нижняя граница: новый шаг полос
    for y, grp in bands.items():
        grp.sort()
        cx = 0
        ny = ymap[y]
        for i, (x, idx, adv) in enumerate(grp):
            t = max(1, int(k * adv))
            newpos[idx] = (cx, t, ny)
            bb = ink_bbox(rows, x, y, adv, ph, pitch)
            if bb is None:
                cellmap[idx] = (cx, t, ny, 0, 0, 0, 0)
            else:
                ix0, iy0, iw, ih = bb
                nw = max(1, int(k * iw))
                nh = max(1, int(k * ih))
                lb = int(round(ix0 * k))
                if lb + nw > t:
                    lb = max(0, t - nw)
                # якорь: строка ink iy1 (низ ink) -> та же доля полосы: round(iy1*k); ink вверх
                iy1 = iy0 + ih - 1
                top = int(round(iy1 * k)) - ny - nh + 1
                if top < 0:
                    top = 0
                if top + nh > new_pitch:
                    top = new_pitch - nh
                cellmap[idx] = (cx, t, ny, lb, top, nw, nh)
            gap = (grp[i + 1][0] - (x + adv)) if i + 1 < len(grp) else 0
            cx += t + (int(round(gap * k)) if gap > 0 else 0)
        if cx > pw:
            raise SystemExit('%s: полоса y=%d не влезает: %d > %d' % (name, y, cx, pw))

    empty = b'\x00' * (pw * 4)
    out = [bytearray(empty) for _ in range(ph)]
    for y, grp in bands.items():
        for x, idx, adv in grp:
            nx, nadv, ny, lb, top, nw, nh = cellmap[idx]
            if nw == 0:
                continue
            bb = ink_bbox(rows, x, y, adv, ph, pitch)
            ix0, iy0, iw, ih = bb
            cell = resample(rows, x, y, ix0, iy0, iw, ih, nw, nh)
            for j, line in enumerate(cell):
                yy = ny + top + j
                if yy >= ph:
                    break
                out[yy][nx * 4:(nx + nw) * 4] = line

    os.makedirs(outdir, exist_ok=True)
    png = os.path.join(outdir, 'font_' + name + '_169.png')
    png_write_rgba(png, pw, ph, [bytes(r) for r in out])
    btf_out = os.path.join(outdir, name + '_169.btf')
    p.pack_one(png, btf_in, btf_out)

    # fdf: заголовок pitch = new_pitch, строки: x' y' adv'
    nl = '\r\n' if crlf else '\n'
    lines = ['%s %d %s' % (fname, new_pitch, sheet), '']
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv, ny = newpos[idx]
        lines.append('%s%5d%4d%3d' % (tok, nx, ny, nadv))
    fdf_out = os.path.join(outdir, name + '_169.fdf')
    open(fdf_out, 'wb').write((nl.join(lines) + nl).encode('latin-1'))

    # контроль: индексы полос, ink внутри ячеек, bleed, суммарный ink
    d2 = u.parse_btf(open(btf_out, 'rb').read())
    w2, h2, b2, _ = d2['dims'][0]
    assert (w2, h2) == (pw, ph) and b2 == 4
    rows2 = u.decode32(d2['blob'], w2, h2, 'RGBA', False)
    bad = 0
    ink = 0
    bandviol = 0
    for idx, (tok, ch, x, y, adv) in enumerate(ents):
        nx, nadv, ny, lb, top, nw, nh = cellmap[idx]
        lo = [None, None]
        for yy in range(ny, min(ny + new_pitch, ph)):
            r = rows2[yy]
            for xx in range(nx, min(nx + nadv, pw)):
                if r[xx * 4 + 3]:
                    ink += 1
                    lo[0] = lo[0] if lo[0] is not None else yy
                    lo[1] = yy
        if nx + nadv < pw:
            for yy in range(ny, min(ny + new_pitch, ph)):
                if rows2[yy][(nx + nadv) * 4 + 3]:
                    bad += 1
                    break
        if lo[0] is not None:
            if lo[0] < ny or lo[1] > ny + new_pitch - 1:
                bandviol += 1
    adv_orig = sum(e[4] for e in ents)
    adv_new = sum(newpos[i][1] for i in range(len(ents)))
    ink_orig = sum(1 for yy in range(ph) for xx in range(pw)
                   if rows[yy][xx * 4 + 3])
    print('%-12s %dx%d pitch %d->%d glyphs=%-3d bands=%-2d k=%.3f  '
          'adv %d->%d (%.4f)  ink %d->%d (%.4f)  bleed=%d bandviol=%d'
          % (name, pw, ph, pitch, new_pitch, len(ents), len(bands), k,
             adv_orig, adv_new, adv_new / float(adv_orig), ink_orig, ink,
             ink / float(ink_orig), bad, bandviol))
    return btf_out, fdf_out


def install(name, btf_out, fdf_out, outdir):
    """*_169 (масштаб) + *_43 (побайтово родные); активные имена = _169."""
    GAME = paths.install_dir()
    shutil.copyfile(btf_out, os.path.join(GAME, 'data', 'textures', name + '_169.btf'))
    shutil.copyfile(fdf_out, os.path.join(GAME, 'data', 'fonts', name + '_169.fdf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.btf'),
                    os.path.join(GAME, 'data', 'textures', name + '_43.btf'))
    shutil.copyfile(os.path.join(BAK, name + '_orig.fdf'),
                    os.path.join(GAME, 'data', 'fonts', name + '_43.fdf'))
    shutil.copyfile(btf_out, os.path.join(GAME, 'data', 'textures', name + '.btf'))
    shutil.copyfile(fdf_out, os.path.join(GAME, 'data', 'fonts', name + '.fdf'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-k', type=float, default=0.75)
    ap.add_argument('-o', default=paths.scratch_dir('ld169c'))
    ap.add_argument('--install', action='store_true')
    a = ap.parse_args()
    for nm in FONTS:
        btf_out, fdf_out = gen(nm, a.k, a.o)
        if a.install:
            install(nm, btf_out, fdf_out, a.o)
            print('   installed sidecars %s_169 + %s_43' % (nm, nm))


if __name__ == '__main__':
    main()
