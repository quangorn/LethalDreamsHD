# maskqc.py - QC апскейла МАСОК покрытия (альфы) листов шрифтов.
#
#   python3 .tmp/maskqc.py <dir_variants> [variant ...]
#
# Метрики на лист (оригинал = alpha из paths.btf_backup_dir()/*_orig.btf):
#   mass   - сумма альфы / (16 * сумма в оригинале)      (1.000 = точная масса штриха)
#   cov    - площадь a>=128 / 16 / площадь в оригинале   (>1 = штрих «распух»)
#   dcx/dcy- сдвиг центроида в пикселях ОРИГИНАЛА
#   soft   - доля пикселей 0<a<255 среди штриха (мягкость края; у оригинала своя)
#   bleed  - доля массы альфы вне ячеек [x,x+adv)x[y,y+pitch) (кроп при рендере)
#   dA1x   - mean|A| после box-даунсемпла 4x->1x против оригинала
import sys, os, subprocess

# Архивное семейство: unpack_btf лежит в tools/btf репозитория (было: scratch-каталог).
_HERE = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, _ROOT)
import paths
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import unpack_btf as U

BAK = paths.btf_backup_dir()
FONTS = [('ld_uncial20', 512, 256, 38), ('ld_uncial12', 256, 256, 25),
         ('ld_tahoma11', 256, 256, 19), ('ld_numbers15', 128, 32, 16)]


def gray_raw(path):
    out = path + '.raw'
    subprocess.check_call(['gm', 'convert', path, '-colorspace', 'gray', '-depth', '8', 'gray:' + out])
    d = open(out, 'rb').read()
    os.remove(out)
    return d


def parse_fdf(name):
    raw = open('%s/%s_orig.fdf' % (BAK, name), 'rb').read().decode('latin-1')
    lines = raw.replace('\r\n', '\n').split('\n')
    pitch = int(lines[0].split()[1])
    ents = []
    for ln in lines[1:]:
        t = ln.split()
        if t:
            ents.append((int(t[1]), int(t[2]), int(t[3])))
    return pitch, ents


def stats(px, W, H):
    s = 0
    cov = 0
    soft = 0
    sx = sy = 0.0
    for i in range(W * H):
        a = px[i]
        if a:
            s += a
            if a >= 128:
                cov += 1
            else:
                soft += 1
            if 0 < a < 255:
                pass
            y, x = divmod(i, W)
            sx += x * a
            sy += y * a
    return s, cov, soft, (sx / s if s else 0), (sy / s if s else 0)


def softfrac(px, W, H):
    n = p = 0
    for i in range(W * H):
        a = px[i]
        if a:
            n += 1
            if a < 255:
                p += 1
    return p / float(n) if n else 0.0


def main():
    root = sys.argv[1]
    want = sys.argv[2:]
    vars_ = [v for v in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, v))]
    if want:
        vars_ = [v for v in vars_ if any(w in v for w in want)]
    print('%-22s %-13s %6s %6s %7s %7s %6s %6s %6s %6s' % (
        'variant', 'sheet', 'mass', 'cov', 'dcx', 'dcy', 'soft', 'bleed%', 'dA1x', 'inkpx'))
    for v in vars_:
        for name, W, H, pitch_n in FONTS:
            p = os.path.join(root, v, name + '_mask.png')
            if not os.path.exists(p):
                print('%-22s %-13s MISSING' % (v[:22], name))
                continue
            px = gray_raw(p)
            W4, H4 = W * 4, H * 4
            assert len(px) == W4 * H4, (p, len(px), W4 * H4)
            orig = U.parse_btf(open('%s/%s_orig.btf' % (BAK, name), 'rb').read())['blob']
            oa = orig[3::4]
            pitch, ents = parse_fdf(name)
            s4, cov4, _, cx4, cy4 = stats(px, W4, H4)
            s1, cov1, _, cx1, cy1 = stats(oa, W, H)
            # bleed: маска ячеек
            ok = bytearray(W4 * H4)
            for x, y, adv in ents:
                for yy in range(y * 4, (y + pitch) * 4):
                    base = yy * W4
                    for xx in range(x * 4, (x + adv) * 4):
                        ok[base + xx] = 1
            out = 0
            for i in range(W4 * H4):
                if not ok[i]:
                    out += px[i]
            # dA1x
            dA = 0
            for y in range(H):
                for x in range(W):
                    s = 0
                    for dy in range(4):
                        b = (y * 4 + dy) * W4 + x * 4
                        s += px[b] + px[b + 1] + px[b + 2] + px[b + 3]
                    dA += abs(s // 16 - oa[y * W + x])
            inks = sum(1 for a in px if a)
            print('%-22s %-13s %6.3f %6.3f %7.2f %7.2f %6.3f %6.2f %6.2f %6d' % (
                v[15:][:22], name, s4 / 16.0 / s1, cov4 / 16.0 / cov1,
                cx4 / 4.0 - cx1, cy4 / 4.0 - cy1, softfrac(px, W4, H4),
                out * 100.0 / s4, dA / (W * H), inks))


main()
