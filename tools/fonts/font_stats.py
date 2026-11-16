# font_stats.py - сравнение ink-метрик листов: родной / ld169 (kx=0.75) / ld169b (kx=0.75 fx=0.7125 fy=0.85).
# Считает по глифам .fdf: ink bbox, ширина/высота, адванс; агрегирует по листу и по строке текста.
#
#   python3 tools/fonts/font_stats.py [--orig DIR] [--old DIR] [--new DIR]
# По умолчанию каталоги берутся из кэша:
#   --orig = cache/btf_backup  (родные шаблоны, python3 tools/extract_pristine.py templates)
#   --old  = cache/font_qc_169,  --new = cache/font_qc_169b
# Пары old/new ищутся как <dir>/<name>_169.btf|.fdf, родная пара - <dir>/<name>_orig.btf|.fdf.
# Отсутствующий файл пары не считается: лист пропускается с ясным сообщением.
import argparse
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as u  # noqa: E402

FONTS = ('ld_uncial12', 'ld_uncial20', 'ld_tahoma11', 'ld_numbers15')


def load(p):
    d = u.parse_btf(open(p, 'rb').read())
    w, h, bpp, _ = d['dims'][0]
    return w, h, u.decode32(d['blob'], w, h, 'RGBA', False)


def fdf(p):
    raw = open(p, 'rb').read().decode('latin-1')
    L = raw.replace('\r\n', '\n').split('\n')
    pitch = int(L[0].split()[1])
    ents = []
    for l in L[1:]:
        if not l.strip():
            continue
        t = l.split()
        tok = t[0]
        code = int(tok[1:]) if tok.startswith('%') else ord(tok)
        ents.append((code, int(t[1]), int(t[2]), int(t[3])))
    return pitch, ents


def bbox(rows, pw, pitch, x, y, adv):
    ix0 = ix1 = iy0 = iy1 = -1
    for yy in range(y, min(y + pitch, len(rows))):
        r = rows[yy]
        for xx in range(x, min(x + adv, pw)):
            if r[xx * 4 + 3]:
                if ix0 < 0:
                    ix0 = ix1 = xx
                    iy0 = iy1 = yy
                else:
                    ix0 = min(ix0, xx); ix1 = max(ix1, xx)
                    iy0 = min(iy0, yy); iy1 = max(iy1, yy)
    if ix0 < 0:
        return None
    return ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1


def stats(name, orig, old, new):
    """orig/old/new = (btf, fdf) пары."""
    (ob, of), (ol, odf), (nw, nf) = orig, old, new
    w1, h1, r1 = load(ob); p1, e1 = fdf(of)
    w2, h2, r2 = load(ol); p2, e2 = fdf(odf)
    w3, h3, r3 = load(nw); p3, e3 = fdf(nf)
    iw1 = ih1 = iw2 = ih2 = iw3 = ih3 = 0
    ad1 = ad2 = ad3 = 0
    n = 0
    for i in range(len(e1)):
        c, x, y, adv = e1[i]
        b = bbox(r1, w1, p1, x, y, adv)
        if not b:
            continue
        _, _, w, h = b
        iw1 += w; ih1 += h; ad1 += adv
        c2, x2, y2, a2 = e2[i]
        b2 = bbox(r2, w2, p2, x2, y2, a2)
        c3, x3, y3, a3 = e3[i]
        b3 = bbox(r3, w3, p3, x3, y3, a3)
        if b2:
            iw2 += b2[2]; ih2 += b2[3]; ad2 += a2
        if b3:
            iw3 += b3[2]; ih3 += b3[3]; ad3 += a3
        n += 1
    print('%-12s glyphs=%d' % (name, n))
    print('   ink width   orig->old %.4f   old->new %.4f   orig->new %.4f' %
          (iw2 / float(iw1), iw3 / float(iw2), iw3 / float(iw1)))
    print('   ink height  orig->old %.4f   old->new %.4f   orig->new %.4f' %
          (ih2 / float(ih1), ih3 / float(ih2), ih3 / float(ih1)))
    print('   advance     orig->old %.4f   old->new %.4f   orig->new %.4f' %
          (ad2 / float(ad1), ad3 / float(ad2), ad3 / float(ad1)))
    # сумма адвансов = ширина строки текста на экране (в пикселях листа * s)
    print('   pitch %d/%d/%d   межглифный паддинг (адванс-ширина ink) orig %.1f old %.1f new %.1f' %
          (p1, p2, p3, (ad1 - iw1) / float(n), (ad2 - iw2) / float(n), (ad3 - iw3) / float(n)))


def main():
    ap = argparse.ArgumentParser(prog='font_stats.py')
    ap.add_argument('--orig', default=None)
    ap.add_argument('--old', default=None)
    ap.add_argument('--new', default=None)
    ap.add_argument('fonts', nargs='*', default=list(FONTS))
    a = ap.parse_args()
    import paths  # локально: только для дефолтов кэш-каталогов
    orig = paths.norm(a.orig) if a.orig else paths.btf_backup_dir()
    old = paths.norm(a.old) if a.old else paths.cache_dir('font_qc_169')
    new = paths.norm(a.new) if a.new else paths.cache_dir('font_qc_169b')
    for nm in a.fonts:
        pairs = [(orig + '/%s_orig.btf' % nm, orig + '/%s_orig.fdf' % nm),
                 (old + '/%s_169.btf' % nm, old + '/%s_169.fdf' % nm),
                 (new + '/%s_169.btf' % nm, new + '/%s_169.fdf' % nm)]
        miss = [p for pr in pairs for p in pr if not os.path.isfile(p)]
        if miss:
            print('%-12s SKIP: missing %s' % (nm, ', '.join(miss)))
            print('   build the cache: python3 tools/extract_pristine.py templates '
                   '(orig), and pass --old/--new to the dirs with the 4x sidecars')
            continue
        stats(nm, pairs[0], pairs[1], pairs[2])
        print()


if __name__ == '__main__':
    main()
