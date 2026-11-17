# tools/tex/upscale_tex.py — Lanczos-3 апскейл почвенных `.tex` PNG в 2 раза.
#
# Вход — PNG из `scratch_dir('ld')/tex_src` (раскод `tools/tex/unpack_tex.py`),
# выход — PNG в `scratch_dir('ld')/tex_up2x` + MANIFEST.txt. Альфа-плоскость
# (bpp4) интерполируется вместе с каналом (Lanczos по всем 4 каналам), поэтому
# blend-пасс движка получает сглаженную альфа-плоскость.
#
# Ядро Lanczos-3: L(x) = sinc(x)*sinc(x/3) при |x|<3; для scale=2 центр
# выходного пикселя c = (x+0.5)/2 - 0.5, отводы i в [c-3, c+3] с краевым
# зажимом, веса нормализованы (сумма = 1 => постоянный цвет сохраняется).
# Раздельно по горизонтали, затем по вертикали; округление int(v+0.5).
# Только stdlib (без PIL); round-half-up, clip 0..255.
#
#   python3 tools/tex/upscale_tex.py                      # tex_src -> tex_up2x
#   python3 tools/tex/upscale_tex.py --in <dir> --out <dir> --name <stem>
import argparse
import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import paths  # noqa: E402
from pack_btf import png_decode  # noqa: E402
from unpack_btf import png_write  # noqa: E402

A = 3  # Lanczos-3


def _sinc(x):
    if x == 0.0:
        return 1.0
    px = math.pi * x
    return math.sin(px) / px


def _kernel(x):
    if x == 0.0:
        return 1.0
    if abs(x) >= A:
        return 0.0
    return _sinc(x) * _sinc(x / A)


def taps(n_out, n_in, scale=2.0):
    """Отводы lanczos для каждого выходного координатного индекса."""
    tab = []
    for x in range(n_out):
        c = (x + 0.5) / scale - 0.5
        lo = math.ceil(c - A)
        hi = math.floor(c + A)
        pairs = []
        s = 0.0
        for i in range(lo, hi + 1):
            w = _kernel(i - c)
            j = i if 0 <= i < n_in else (0 if i < 0 else n_in - 1)
            pairs.append((j, w))
            s += w
        if s > 0.0:
            pairs = [(j, w / s) for j, w in pairs]
        tab.append(pairs)
    return tab


def upscale2(w, h, bpp, data):
    """data — bytes строк (top-down, interleaved RGBA); возвращает (2w,2h,rows)."""
    ow, oh = 2 * w, 2 * h
    xtab = taps(ow, w)
    # горизонтальный проход: каждая входная строка -> выходная строка
    tmp = []
    for y in range(h):
        row = data[y * w * bpp:(y + 1) * w * bpp]
        out_row = bytearray(ow * bpp)
        for x, pairs in enumerate(xtab):
            acc = [0.0] * bpp
            for i, wt in pairs:
                o = i * bpp
                for ch in range(bpp):
                    acc[ch] += row[o + ch] * wt
            base = x * bpp
            for ch in range(bpp):
                v = int(acc[ch] + 0.5)
                out_row[base + ch] = 0 if v < 0 else (255 if v > 255 else v)
        tmp.append(out_row)
    # вертикальный проход
    ytab = taps(oh, h)
    rows = []
    for yo in range(oh):
        accs = [[0.0] * bpp for _ in range(ow)]
        for i, wt in ytab[yo]:
            src = tmp[i]
            for x in range(ow):
                o = x * bpp
                a = accs[x]
                for ch in range(bpp):
                    a[ch] += src[o + ch] * wt
        line = bytearray(ow * bpp)
        for x in range(ow):
            a = accs[x]
            base = x * bpp
            for ch in range(bpp):
                v = int(a[ch] + 0.5)
                line[base + ch] = 0 if v < 0 else (255 if v > 255 else v)
        rows.append(bytes(line))
    return ow, oh, rows


def main():
    ap = argparse.ArgumentParser(description='Lanczos-3 2x upscale of ground .tex PNGs')
    ap.add_argument('--in', dest='ind', default=os.path.join(paths.scratch_dir('ld'), 'tex_src'))
    ap.add_argument('--name', default=None, help='обработать один PNG по имени stem')
    ap.add_argument('--out', dest='outd', default=os.path.join(paths.scratch_dir('ld'), 'tex_up2x'))
    a = ap.parse_args()
    os.makedirs(a.outd, exist_ok=True)
    names = [a.name] if a.name else sorted(f[:-4] for f in os.listdir(a.ind) if f.endswith('.png'))
    manifest = []
    for stem in names:
        src = os.path.join(a.ind, stem + '.png')
        w, h, rows = png_decode(src)
        ow, oh, ur = upscale2(w, h, 4, b''.join(rows))
        dst = os.path.join(a.outd, stem + '.png')
        png_write(dst, ow, oh, ur)
        manifest.append('%s\t%d x %d -> %d x %d' % (stem, w, h, ow, oh))
        print('%s: %dx%d -> %dx%d' % (stem, w, h, ow, oh))
    with open(os.path.join(a.outd, 'MANIFEST.txt'), 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(manifest) + '\n')
    print('upscaled %d PNG -> %s' % (len(names), a.outd))


if __name__ == '__main__':
    main()
