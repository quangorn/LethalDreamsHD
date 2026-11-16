# fontqc.py - контроль качества 4x-апскейла листов шрифтов.
#
#   python3 .tmp/fontqc.py <dir_with_variants> [variant ...]
#
# Для каждого варианта/листа считает:
#   alpha/coverage    - сумма и площадь покрытия против оригинала (ожидание ~1.0)
#   centroid          - сдвиг центра масс альфы в пикселях ОРИГИНАЛА (ожидание ~0)
#   bbox              - bbox ink 4x vs 4*bbox оригинала (ожидание +-1..2 px @4x)
#   dA1x              - mean|A| после box-даунсемпла 4x->1x против оригинала
#   bleed             - ink ВНЕ ячейки [x,x+adv) x [y,y+pitch) (должно быть 0)
#   rgbD              - средняя |RGB| по штрафу a>200 против оригинала
import sys, os, subprocess, struct
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


def raw_rgba(path):
    """PNG -> bytes RGBA (через GraphicsMagick)."""
    out = path + '.raw'
    subprocess.check_call(['gm', 'convert', path, '-depth', '8', 'RGBA:' + out])
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


def stats(blob, W, H):
    n = W * H
    asum = 0
    cov = 0
    sx = sy = 0.0
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for i in range(n):
        a = blob[i * 4 + 3]
        if a:
            asum += a
            x, y = i % W, i // W
            sx += x * a
            sy += y * a
            if a >= 32:
                cov += 1
                if x < x0:
                    x0 = x
                if x > x1:
                    x1 = x
                if y < y0:
                    y0 = y
                if y > y1:
                    y1 = y
    return dict(asum=asum, cov=cov, cx=sx / asum if asum else 0, cy=sy / asum if asum else 0,
                bbox=(x0, y0, x1, y1))


def main():
    root = sys.argv[1]
    want = sys.argv[2:]
    vars_ = [v for v in sorted(os.listdir(root)) if os.path.isdir(os.path.join(root, v))]
    if want:
        vars_ = [v for v in vars_ if any(w in v for w in want)]
    print('%-22s %-13s %6s %6s %7s %7s %6s %7s %6s %7s' % (
        'variant', 'sheet', 'alpha', 'cover', 'dcx', 'dcy', 'dA1x', 'bleed', 'out%', 'rgbD'))
    for v in vars_:
        for name, W, H, pitch_n in FONTS:
            p = os.path.join(root, v, name + '.png')
            if not os.path.exists(p):
                print('%-22s %-13s MISSING' % (v, name))
                continue
            blob = raw_rgba(p)
            W4, H4 = W * 4, H * 4
            assert len(blob) == W4 * H4 * 4, (p, len(blob), W4 * H4 * 4)
            orig = U.parse_btf(open('%s/%s_orig.btf' % (BAK, name), 'rb').read())['blob']
            pitch, ents = parse_fdf(name)
            s1 = stats(orig, W, H)
            s4 = stats(blob, W4, H4)
            # box downsample 4x -> 1x по альфе, сравнение с оригиналом
            dA = 0
            for y in range(H):
                for x in range(W):
                    s = 0
                    for dy in range(4):
                        base = ((y * 4 + dy) * W4 + x * 4) * 4 + 3
                        s += blob[base] + blob[base + 4] + blob[base + 8] + blob[base + 12]
                    dA += abs(s // 16 - orig[(y * W + x) * 4 + 3])
            # bleed: ink вне ячеек
            bleed = 0
            for x, y, adv in ents:
                pass
            # карта ячеек: pixel принадлежит ячейке, если попадает в [4x,4x+4adv) x band
            okx = bytearray(W4 * H4)
            for x, y, adv in ents:
                for yy in range(y * 4, (y + pitch) * 4):
                    row = yy * W4
                    for xx in range(x * 4, (x + adv) * 4):
                        okx[row + xx] = 1
            for i in range(W4 * H4):
                if blob[i * 4 + 3] > 8 and not okx[i]:
                    bleed += 1
            mass_out = sum(blob[i * 4 + 3] for i in range(W4 * H4) if not okx[i]) / float(s4['asum'])
            # цвет
            dr = dg = db = 0
            cnt = 0
            for y in range(H):
                for x in range(W):
                    if orig[(y * W + x) * 4 + 3] > 200:
                        oy, ox = y * 4, x * 4
                        r = g = b = 0
                        for dy in range(4):
                            for dx in range(4):
                                i = ((oy + dy) * W4 + ox + dx) * 4
                                r += blob[i]; g += blob[i + 1]; b += blob[i + 2]
                        r //= 16; g //= 16; b //= 16
                        oi = (y * W + x) * 4
                        dr += abs(r - orig[oi]); dg += abs(g - orig[oi + 1]); db += abs(b - orig[oi + 2])
                        cnt += 1
            rgbd = (dr + dg + db) / (3.0 * cnt) if cnt else 0
            print('%-22s %-13s %6.3f %6.3f %7.2f %7.2f %6.2f %7d %7.2f %6.2f' % (
                v[15:], name,
                s4['asum'] / 16.0 / s1['asum'], s4['cov'] / 16.0 / s1['cov'],
                s4['cx'] / 4.0 - s1['cx'], s4['cy'] / 4.0 - s1['cy'],
                dA / (W * H), bleed, mass_out * 100, rgbd))


main()
