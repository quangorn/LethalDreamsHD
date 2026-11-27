#!/usr/bin/env python3
"""QC апскейлов `.btf`: альфа-корреляция записи hd_pack с родной текстурой.

Симптом: значки юнитов на миникарте (`minimap_pt01.tga`, `MM_*_TEX` в
`ui/gui_templates.py`) рисуются размытыми прямоугольниками. Причина: запись
hd_pack 16x16 несёт шумный blob (средняя альфа ~20/255) — апскейл сделан из
испорченного раскода, а не из родного 8x8.

Метрика: по паре pristine/hd_pack с точным множителем размеров (2/4) считается
корреляция Пирсона альфы hd_pack (nearest-даунскейл к размеру оригинала) и
родной альфы. Легитимный апскейл >= 0.98, испорченный blob < 0.5. Альфу
апскейлеры ресемплируют отдельно и не меняют, поэтому она и есть признак порчи.
Для 24-bit пар альфы нет: в отчёт идёт минимальная корреляция каналов как
advisory (на градиентах и аддитивных эффектах метрика занижена, автофикса нет).

    python3 tools/btf/qc_upscale_alpha.py extract-orig            # originals из pristine
    python3 tools/btf/qc_upscale_alpha.py scan [--all] [--max-dim 256]
    python3 tools/btf/qc_upscale_alpha.py fix                     # flagged -> .btf
    python3 tools/btf/qc_upscale_alpha.py install --install       # hd_pack + установка

Чтение — установка и архивы; запись — `paths.scratch_dir('qc_upscale')`; hd_pack-архив
и файлы установки — только с явным `--install`.
"""
import argparse
import math
import os
import shutil
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
from unpack_btf import parse_btf, decode32, png_write  # noqa: E402
from pack_btf import pack_one  # noqa: E402

sys.path.insert(0, os.path.join(_ROOT, 'tools', 'tex'))
from upscale_tex import upscale2  # noqa: E402

FLAG = 0.5    # альфа-корреляция ниже этого — испорченный blob (легитимные апскейлы >= 0.88)
MAXDIM = 256  # «небольшие текстуры»: стороны родного оригинала не больше этой
ADVISORY = 0.5


def work_dir(subdir=None):
    return paths.scratch_dir('qc_upscale' + ('/' + subdir if subdir else ''))


def extract_orig():
    """Оригиналы (2002) всех hd_pack `.btf` из pristine_archive в work_dir('orig')."""
    names = sorted(e['name'] for e in paths.archive_list_of(paths.hd_pack())
                   if e['name'].endswith('.btf'))
    out = os.path.join(work_dir(), 'orig')
    os.makedirs(out, exist_ok=True)
    proc = paths.run7z(['x', '-y', '-scsUTF-8', '-o' + out], paths.pristine_archive(), names)
    if proc.returncode != 0:
        raise RuntimeError('7z extract failed\n%s' % (proc.stdout + proc.stderr))
    got = [n for n in names if os.path.isfile(os.path.join(out, n))]
    print('extracted %d originals (of %d hd_pack .btf) into %s' % (len(got), len(names), out))
    return len(got)


def pairs(max_dim=MAXDIM):
    """[(name, w_orig, h_orig, bpp, k)] — пары pristine/hd_pack, где установленный
    файл парсится, совпадает по bpp и ровно в k (2/4) раз больше оригинала."""
    hd = paths.archive_list_of(paths.hd_pack())
    names = {e['name'] for e in paths.archive_list_of(paths.pristine_archive())}
    origdir = os.path.join(work_dir(), 'orig')
    out = []
    for e in hd:
        n = e['name']
        if not n.endswith('.btf') or n not in names:
            continue
        op = os.path.join(origdir, n)
        ip = paths.game_path(*n.split('/'))
        if not (os.path.isfile(op) and os.path.isfile(ip)):
            continue
        try:
            i = parse_btf(open(ip, 'rb').read())['canon']
            o = parse_btf(open(op, 'rb').read())['canon']
        except Exception:
            continue
        wi, hi, bi = i[0:3]
        wo, ho, bo = o[0:3]
        if bi != bo or bo not in (3, 4) or not (wo and ho):
            continue
        if wi % wo or hi % ho:
            continue
        k = wi // wo
        if hi // ho != k or k not in (2, 4):
            continue
        if max_dim and (wo > max_dim or ho > max_dim):
            continue
        out.append((n, wo, ho, bo, k))
    return out


def channels(blob, bpp):
    """Канальные плоскости interleaved-blob'а (R,G,B[,A]) как bytes."""
    return [blob[c::bpp] for c in range(bpp)]


def nearest_down(ch, w, h, k):
    """Nearest-даунскейл канальной плоскости в k раз (каждый k-й пиксель)."""
    out = bytearray()
    for y in range(h // k):
        out += ch[y * k * w:(y * k + 1) * w][0::k]
    return bytes(out)


def corr(a, b):
    """Корреляция Пирсона двух плоскостей; None, если одна постоянна."""
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 1e-9 or vb <= 1e-9:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def measure(name, wo, ho, bpp, k):
    """(alpha_corr, min_rgb_corr) пары; alpha_corr None для 24-bit."""
    ip = paths.game_path(*name.split('/'))
    op = os.path.join(work_dir('orig'), name)
    i = parse_btf(open(ip, 'rb').read())
    o = parse_btf(open(op, 'rb').read())
    wi, hi = i['canon'][0], i['canon'][1]
    downs = [nearest_down(c, wi, hi, k) for c in channels(i['blob'], bpp)]
    cs = [corr(a, b) for a, b in zip(channels(o['blob'], bpp), downs)]
    rgb = [c for c in cs[:3] if c is not None]
    return (cs[3] if bpp == 4 else None), (min(rgb) if rgb else None)


def scan(max_dim=MAXDIM):
    """[(name, wo, ho, bpp, k, alpha_corr, min_ch_corr, flag)]; flag: BAD (RGBA,
    альфа < FLAG), advisory (24-bit, каналы < ADVISORY)."""
    rows = []
    for n, wo, ho, bpp, k in pairs(max_dim):
        try:
            al, mn = measure(n, wo, ho, bpp, k)
        except Exception as e:
            rows.append((n, wo, ho, bpp, k, None, None, 'ERR:%s' % e))
            continue
        if bpp == 4:
            if al is None:
                flag = 'flat'          # альфа постоянна: корреляция не определена
            elif al < FLAG:
                flag = 'BAD'
            elif al < 0.9:
                flag = 'weak'          # перерисованный лист (шрифты) — только просмотр
            else:
                flag = ''
        else:
            flag = 'advisory' if mn is not None and mn < ADVISORY else ''
        rows.append((n, wo, ho, bpp, k, al, mn, flag))
    return rows


REPORT = lambda: os.path.join(work_dir(), 'report.tsv')  # noqa: E731


def write_report(rows, path):
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('name\tw_orig\th_orig\tbpp\tfactor\talpha_corr\tmin_ch_corr\tflag\n')
        for r in rows:
            fh.write('%s\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n' % (
                r[0], r[1], r[2], r[3], r[4],
                '' if r[5] is None else '%.4f' % r[5],
                '' if r[6] is None else '%.4f' % r[6], r[7]))


def read_report(path):
    rows = []
    for line in open(path, encoding='utf-8').read().splitlines()[1:]:
        p = line.split('\t')
        rows.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]), p[7]))
    return rows


def regen(name, wo, ho, k, outd):
    """Родной раскод -> Lanczos-3 x k -> .btf по шаблону родного файла; пишет
    `outd/<путь в архиве>` (структура каталогов та же, что у hd_pack)."""
    op = os.path.join(work_dir('orig'), name)
    o = parse_btf(open(op, 'rb').read())
    rows = decode32(o['blob'], wo, ho, 'RGBA', False)
    ow, oh, ur = upscale2(wo, ho, 4, b''.join(rows), k)
    png = os.path.join(outd, name.rsplit('/', 1)[1] + '.png')
    png_write(png, ow, oh, ur)
    dst = os.path.join(outd, name)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    pack_one(png, op, dst)
    os.remove(png)
    return dst


def install(outd, names):
    """Обновляет записи hd_pack-архива и те же файлы установки (явный --install)."""
    proc = subprocess.run(['7z', 'u', '-y', paths.cygwin_path(paths.hd_pack())] + names,
                          cwd=outd, capture_output=True)
    if proc.returncode != 0:
        raise RuntimeError('7z update failed\n%s' % (proc.stdout + proc.stderr))
    for n in names:
        shutil.copyfile(os.path.join(outd, n), paths.game_path(*n.split('/')))


def main():
    ap = argparse.ArgumentParser(description='.btf upscale QC (alpha correlation vs pristine)')
    ap.add_argument('cmd', choices=['extract-orig', 'scan', 'fix', 'install'])
    ap.add_argument('--all', action='store_true', help='не ограничивать размер оригинала')
    ap.add_argument('--max-dim', type=int, default=MAXDIM)
    ap.add_argument('--out', dest='outd', help='каталог результатов (fix/install)')
    ap.add_argument('--install', action='store_true', help='запись в hd_pack и установку')
    a = ap.parse_args()
    outd = a.outd or os.path.join(work_dir(), 'fixed')

    if a.cmd == 'extract-orig':
        extract_orig()
        return

    if a.cmd == 'scan':
        rows = scan(None if a.all else a.max_dim)
        write_report(rows, REPORT())
        bad = [r for r in rows if r[7] == 'BAD']
        adv = [r for r in rows if r[7] == 'advisory']
        weak = [r for r in rows if r[7] in ('weak', 'flat')]
        print('%-46s %-8s %2s  %-10s  %-12s  %s' % ('texture', 'orig', 'k', 'alpha_corr', 'min_ch', 'flag'))
        for r in sorted(rows, key=lambda r: (r[5] if r[5] is not None else 9,
                                             r[6] if r[6] is not None else 9)):
            if r[7] == '' or r[7].startswith('ERR'):
                continue
            print('%-46s %3dx%-3d x%d  %-10s  %-12s  %s' % (
                r[0], r[1], r[2], r[4],
                '-' if r[5] is None else '%.4f' % r[5],
                '-' if r[6] is None else '%.4f' % r[6], r[7]))
        for r in rows:
            if r[7].startswith('ERR'):
                print('ERR %s: %s' % (r[0], r[7][4:]))
        print('пар: %d; BAD: %d; weak/flat: %d; advisory: %d; отчёт: %s' % (
            len(rows), len(bad), len(weak), len(adv), REPORT()))
        return

    todo = [r for r in read_report(REPORT()) if r[5] == 'BAD']
    if a.cmd == 'fix':
        if not todo:
            print('flagged нет: %s' % REPORT())
            return
        os.makedirs(outd, exist_ok=True)
        for n, wo, ho, bpp, k, _f in todo:
            regen(n, wo, ho, k, outd)
        print('результат: %s; установка: qc_upscale_alpha.py install --install' % outd)
        return

    if a.cmd == 'install':
        names = [r[0] for r in todo]
        if not a.install:
            print('dry-run: %d файла; запись только с --install' % len(names))
            return
        install(outd, names)
        for n, wo, ho, bpp, k, _f in todo:
            al, _mn = measure(n, wo, ho, bpp, k)
            print('%s: alpha_corr=%s' % (n, 'None' if al is None else '%.4f' % al))
        return


if __name__ == '__main__':
    main()
