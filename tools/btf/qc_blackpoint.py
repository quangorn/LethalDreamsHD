# tools/btf/qc_blackpoint.py — QC и правка «чёрной точки» аддитивных UI-текстур.
#
# Симптом: при движении курсора в меню виден светлый квадрат (например `mm_eye_lit`,
# `mm_curlite`, `mm_eyeglow01`). Причина: 2x-апскейлы hd_pack несут равномерный
# RGB-смещённый пол (~2/255) во всём кадре, включая края; оригиналы (2002) там 0.
# Текстурные элементы рисуются `globalscript.BBlend_Add` (аддитивно), поэтому пол
# складывается с фоном и квадрат виден на тёмных участках фоне.
#
# Правка: по каждой цветовой каналу вычитается минимум полосы по краю кадра
# (`bandmin`), клип в 0. Альфа не трогается (апскейлер ресемплит альфу отдельно).
# Каналы с bandmin==0 не меняются, контент с bandmin > MAXFLOOR (art-полотна)
# не трогается.
#
#   python3 tools/btf/qc_blackpoint.py scan                 # offenders по установке
#   python3 tools/btf/qc_blackpoint.py extract-orig         # оригиналы offenders из pristine
#   python3 tools/btf/qc_blackpoint.py fix --in DIR --out DIR
#
# Только чтение установки и архивов; запись — в `paths.scratch_dir(...)`.
import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from unpack_btf import parse_btf, u32  # noqa: E402

BORDER = 2       # ширина краевой полосы, по которой считается чёрная точка
MAXFLOOR = 8     # смещения больше этого — art-контент, не пол; такие файлы не трогать


def md5(path):
    return hashlib.md5(open(path, 'rb').read()).hexdigest()


def offenders(names, indir):
    """[(name, w, h, bpp, bandmin)] по аддитивным текстурам `names` из каталога `indir`.
    Файлы, которые текущий парсер `.btf` не раскодировывает (мелкие индексированные
    глифы с внешней палитрой), пропускаются."""
    out = []
    for n in names:
        p = os.path.join(indir, n + '.btf')
        if not os.path.isfile(p):
            continue
        try:
            d = parse_btf(open(p, 'rb').read())
            w, h, bpp, P = d['dims'][0]
        except Exception:
            continue
        st = band_stats(channels(d['blob'], w, h, bpp), band_index(w, h))
        out.append((n, w, h, bpp, min(s[0] for s in st[:3])))
    return out


def additive_names(*dirs):
    """Имена базовых текстур элементов `ui.BActionElement` с `BBlend_Add`.
    Строки пайплайна (decompiled_fixed, patches) содержат элемент целиком,
    имя текстуры — строковый токен `.bmp/.tga/.png`."""
    names = set()
    for d in dirs:
        for root, _, files in os.walk(os.path.join(_ROOT, d)):
            for f in files:
                if not f.endswith('.py'):
                    continue
                for line in open(os.path.join(root, f), encoding='utf-8', errors='replace'):
                    if 'BBlend_Add' in line:
                        for m in re.finditer(r"'([^']+\.(?:bmp|tga|png))'", line):
                            names.add(m.group(1).rsplit('.', 1)[0])
    return names


def band_index(w, h, border=BORDER):
    idx = []
    for x in range(w):
        for y in list(range(border)) + list(range(h - border, h)):
            idx.append(y * w + x)
    for y in range(h):
        for x in list(range(border)) + list(range(w - border, w)):
            idx.append(y * w + x)
    return idx


def channels(blob, w, h, bpp):
    n = w * h
    return [[blob[i] for i in range(c, n * bpp, bpp)] for c in range(bpp)]


def band_stats(chs, idx):
    """[(bandmin, bandmean)] по каждому каналу."""
    return [(min(c[i] for i in idx), sum(c[i] for i in idx) / len(idx)) for c in chs]


def clamp_blob(blob, w, h, bpp):
    """Вычитает bandmin каждого канала (RGB) из всех пикселей того же канала."""
    chs = channels(blob, w, h, bpp)
    idx = band_index(w, h)
    out = []
    floors = []
    for c in range(bpp):
        f = min(chs[c][i] for i in idx) if c < 3 else 0
        floors.append(f)
        out.append([v - f if v >= f else v for v in chs[c]] if f else chs[c])
    n = w * h
    new = bytearray(n * bpp)
    for c in range(bpp):
        for i in range(c, n * bpp, bpp):
            new[i] = out[c][(i - c) // bpp]
    return bytes(new), floors


def repack_blob(template_path, new_blob, outpath):
    """Тот же контейнер и заголовок, что у шаблона; меняется только пиксельный blob
    (размеры не меняются, поэтому поля w,h и blob_len остаются)."""
    data = open(template_path, 'rb').read()
    d = parse_btf(data)
    payload = d['payload']
    blob_len = d['blob_len']
    if len(new_blob) != blob_len:
        raise ValueError('blob size changed: %d vs %d' % (len(new_blob), blob_len))
    new_payload = payload[:len(payload) - blob_len] + new_blob
    if data.startswith(b'LDPACKER'):
        zi = -1
        for m in (b'\x78\xda', b'\x78\x9c', b'\x78\x01'):
            i = data.find(m, 8)
            if i >= 0:
                zi = i
                break
        comp = zlib.compress(new_payload, 9)
        head = bytearray(data[:zi])
        for i in range(8, zi - 3):
            if u32(head, i) == len(data) - zi:
                struct.pack_into('<I', head, i, len(comp))
        open(outpath, 'wb').write(bytes(head) + comp)
    else:
        open(outpath, 'wb').write(new_payload)


def box_down(chs, w, h, f):
    """Box-даунскейл каналов в f раз (для сверки апскейла с оригиналом)."""
    out = []
    for c in chs:
        rows = []
        for y in range(h // f):
            for x in range(w // f):
                s = 0
                for dy in range(f):
                    base = (y * f + dy) * w + x * f
                    for dx in range(f):
                        s += c[base + dx]
                rows.append(s / (f * f))
        out.append(rows)
    return out


def compare(chs_new, w, h, chs_orig, wo):
    """Средняя и максимальная |Δ| по RGB после box-даунскейла к размеру оригинала."""
    if w != wo:
        d = box_down(chs_new[:3], w, h, w // wo)
    else:
        d = chs_new[:3]
    mean = 0.0
    mx = 0
    n = 0
    for c in range(3):
        diff = [abs(a - b) for a, b in zip(d[c], chs_orig[c])]
        mean += sum(diff)
        mx = max(mx, max(diff))
        n += len(diff)
    return mean / n, mx


def extract_orig(names, outdir):
    """Оригиналы (2002) offender-текстур из pristine_archive в outdir/data/textures."""
    entries = ['data/textures/%s.btf' % n for n in sorted(names)]
    proc = paths.run7z(['x', '-y', '-scsUTF-8', '-o' + outdir], paths.pristine_archive(), entries)
    if proc.returncode != 0:
        raise RuntimeError('7z extract failed for %s\n%s' % (paths.pristine_archive(),
                                                             proc.stdout + proc.stderr))
    got = []
    for n in sorted(names):
        f = os.path.join(outdir, 'data', 'textures', n + '.btf')
        if os.path.isfile(f):
            got.append(n)
        else:
            print('MISSING original %s' % n)
    return got


def main():
    ap = argparse.ArgumentParser(description='.btf black-point QC / clamp')
    ap.add_argument('cmd', choices=['scan', 'extract-orig', 'fix'])
    ap.add_argument('--in', dest='ind', help='каталог исходных .btf (файлы data/textures/*.btf)')
    ap.add_argument('--out', dest='outd', help='каталог результатов')
    ap.add_argument('--orig', help='каталог оригиналов для сверки')
    a = ap.parse_args()

    names = sorted(additive_names('decompiled_fixed', 'patches'))
    ind = a.ind or paths.game_path('data', 'textures')
    outd = a.outd or paths.scratch_dir('btf_clamp')
    origd = a.orig or paths.scratch_dir('btf_clamp') + '/orig'

    off = [r for r in offenders(names, ind) if 0 < r[4] <= MAXFLOOR]
    if a.cmd == 'extract-orig':
        got = extract_orig([r[0] for r in off], origd)
        print('offenders %d, originals extracted %d into %s' % (len(off), len(got), origd))
        return

    rows = []
    for n, w, h, bpp, floor in offenders(names, ind):
        src = os.path.join(ind, n + '.btf')
        if floor == 0 or floor > MAXFLOOR:
            rows.append((n, w, h, bpp, floor, floor, 0.0, 0.0, None))
            continue
        blob = parse_btf(open(src, 'rb').read())['blob']
        chs = channels(blob, w, h, bpp)
        idx = band_index(w, h)
        st = band_stats(chs, idx)
        new, _floors = clamp_blob(blob, w, h, bpp)
        nchs = channels(new, w, h, bpp)
        nst = band_stats(nchs, idx)
        d_ = None
        opath = os.path.join(origd, 'data', 'textures', n + '.btf')
        if os.path.isfile(opath):
            od = parse_btf(open(opath, 'rb').read())
            ow, oh, obpp, oP = od['dims'][0]
            ochs = channels(od['blob'], ow, oh, obpp)
            d_ = compare(nchs, w, h, ochs, ow)
        if a.cmd == 'fix':
            td = os.path.join(outd, 'data', 'textures')
            os.makedirs(td, exist_ok=True)
            repack_blob(src, new, os.path.join(td, n + '.btf'))
        rows.append((n, w, h, bpp, floor, min(s[0] for s in nst[:3]),
                     sum(s[1] for s in st[:3]) / 3, sum(s[1] for s in nst[:3]) / 3, d_))

    print('%-22s %9s %s  floor->bandmin  bandmean->bandmean  vs-orig(mean,max)'
          % ('texture', 'dims', 'bpp'))
    off = 0
    rep = []
    for r in rows:
        n, w, h, bpp, f0, f1, bm0, bm1, d_ = r
        if f0 == 0:
            continue
        off += 1
        cmp = 'n/a' if d_ is None else '%.2f, %d' % (d_[0], d_[1])
        line = '%-22s %5dx%-5s %d  %5.1f -> %.1f      %6.2f -> %6.2f      %s' % (
            n, w, h, bpp, f0, f1, bm0, bm1, cmp)
        print(line)
        rep.append(line)
    print('offenders %d из %d аддитивных; выход: %s/data/textures' % (off, len(rows), outd))

    if a.cmd == 'fix':
        bad = []
        for n, w, h, bpp, floor, _f, _b, _n, _d in rows:
            if floor == 0 or floor > MAXFLOOR:
                continue
            p = os.path.join(outd, 'data', 'textures', n + '.btf')
            nd = parse_btf(open(p, 'rb').read())
            nw, nh, nbpp, _P = nd['dims'][0]
            nchs = channels(nd['blob'], nw, nh, nbpp)
            idx = band_index(nw, nh)
            if (nw, nh, nbpp) != (w, h, bpp):
                bad.append('%s dims %s vs %s' % (n, (nw, nh, nbpp), (w, h, bpp)))
            elif min(s[0] for s in band_stats(nchs, idx)[:3]) != 0:
                bad.append('%s bandmin != 0' % n)
            elif bpp == 4 and channels(parse_btf(open(os.path.join(ind, n + '.btf'), 'rb').read())['blob'], w, h, bpp)[3] != nchs[3]:
                bad.append('%s alpha changed' % n)
            rep.append('%s md5=%s size=%d' % (n, md5(p), os.path.getsize(p)))
        print('гейт: %d файлов, проблем %d' % (off, len(bad)))
        for b in bad:
            print('  PROBLEM', b)
        written = len([f for f in os.listdir(os.path.join(outd, 'data', 'textures'))
                       if f.endswith('.btf')])
        rep.append('гейт: %d offenders, записано %d файлов, проблем %d' % (off, written, len(bad)))
        print('записано %d файлов (offenders %d; floor > MAXFLOOR=%d не трогается)' % (written, off, MAXFLOOR))
        rep.append('гейт: %d файлов, проблем %d' % (off, len(bad)))
        for b in bad:
            rep.append('PROBLEM ' + b)
        open(os.path.join(outd, 'qc_report.txt'), 'w', encoding='utf-8').write('\n'.join(rep) + '\n')
        print('отчёт: %s/qc_report.txt' % outd)


if __name__ == '__main__':
    main()
