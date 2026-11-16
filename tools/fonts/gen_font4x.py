# gen_font4x.py - сборка 4x-сайдкаров листов шрифтов (16:9) из апскейленных PNG.
#
# Вход: каталог с PNG строго x4 (ld_uncial20.png, ld_uncial12.png, ld_tahoma11.png,
# ld_numbers15.png), RGBA 8 бит, сетка ячеек ровно x4. Каталог берётся из
# config-ключа assets.upscayl_dir (или флага -s).
# База/шаблон: ТОЛЬКО кэш-шаблоны cache/btf_backup/<name>_orig.btf|.fdf
# (восстанавливаются: python3 tools/extract_pristine.py templates).
#
# Выход (cache/fonts4x_out/ — в репозитории сайдкары не хранятся):
#   <name>_169.btf  - лист 4x (шаблон = родной .btf)
#   <name>_169.fdf  - та же таблица, все x,y,adv и pitch умножены на 4
#   <name>_43.btf/.fdf - побайтово родные (для 4:3)
#
#   python3 tools/fonts/gen_font4x.py -s <srcdir> [--install]
import argparse
import os
import re
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
import paths  # noqa: E402
import pack_btf as P  # noqa: E402

BAK = paths.btf_backup_dir()
OUT = paths.cache_dir('fonts4x_out')
FONTS = ['ld_uncial20', 'ld_uncial12', 'ld_tahoma11', 'ld_numbers15']
SCALE = 4
TEMPLATES_CMD = 'python3 tools/extract_pristine.py templates'


def template(nm, ext):
    """cache/btf_backup/<name>_orig<ext>; names the rebuild command when missing."""
    p = os.path.join(BAK, nm + '_orig' + ext)
    if not os.path.isfile(p):
        sys.exit("missing template %s\nrebuild the cache: %s" % (p, TEMPLATES_CMD))
    return p


def src_dir(cli_value):
    """The 4x PNG source dir: -s wins, otherwise the config key assets.upscayl_dir.
    Missing key or missing dir fails with a message naming the key."""
    d = paths.norm(cli_value) if cli_value else None
    if d is None:
        sec = paths.load().get('assets')
        v = sec.get('upscayl_dir') if isinstance(sec, dict) else None
        if not v:
            sys.exit("no 4x source dir: set assets.upscayl_dir in config.toml "
                     "(see config.example.toml) or pass -s <srcdir>")
        d = paths.norm(v)
    if not os.path.isdir(d):
        sys.exit("4x source dir does not exist: %s\nset assets.upscayl_dir to the dir "
                 "with the 4x PNGs, or pass -s <srcdir>" % d)
    return d


def scale_fdf(src, dst, n):
    raw = open(src, 'rb').read().decode('latin-1')
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    out = []
    # header: <name> <pitch> <sheet.tga>
    m0 = re.match(r'^(\s*)(\S+)(\s+)(\d+)(\s+)(\S+)(\s*)$', lines[0])
    assert m0, lines[0]
    out.append(m0.group(1) + m0.group(2) + m0.group(3) + str(int(m0.group(4)) * n) +
               m0.group(5) + m0.group(6) + m0.group(7))
    for ln in lines[1:]:
        if not ln.strip():
            out.append(ln)
            continue
        m = re.match(r'^(\s*)(\S+)(.*)$', ln)
        head, rest = m.group(1) + m.group(2), m.group(3)
        nums = re.findall(r'\d+', rest)
        assert len(nums) == 3, ln
        it = iter(nums)
        out.append(head + re.sub(r'\d+', lambda _m: str(int(next(it)) * n), rest))
    open(dst, 'wb').write(nl.join(out).encode('latin-1'))


def main():
    ap = argparse.ArgumentParser(prog='gen_font4x.py')
    ap.add_argument('-s', default=None)
    ap.add_argument('--install', action='store_true')
    a = ap.parse_args()
    src = src_dir(a.s)
    for nm in FONTS:
        png = os.path.join(src, nm + '.png')
        if not os.path.isfile(png):
            sys.exit("missing 4x PNG %s\nassets.upscayl_dir (or -s) must point at the "
                     "dir with the x4 sheets" % png)
    os.makedirs(OUT, exist_ok=True)
    for nm in FONTS:
        png = os.path.join(src, nm + '.png')
        P.pack_one(png, template(nm, '.btf'), os.path.join(OUT, nm + '_169.btf'))
        scale_fdf(template(nm, '.fdf'), os.path.join(OUT, nm + '_169.fdf'), SCALE)
        shutil.copyfile(template(nm, '.btf'), os.path.join(OUT, nm + '_43.btf'))
        shutil.copyfile(template(nm, '.fdf'), os.path.join(OUT, nm + '_43.fdf'))
    if a.install:
        for sub in ('textures', 'fonts'):
            os.makedirs(paths.game_path('data', sub), exist_ok=True)
        for nm in FONTS:
            for sfx, sub, fname in (('.btf', 'textures', nm), ('.fdf', 'fonts', nm)):
                for tag in ('_169', '_43'):
                    srcf = os.path.join(OUT, nm + tag + sfx)
                    dst = paths.game_path('data', sub) + '/' + nm + tag + sfx
                    shutil.copyfile(srcf, dst)
                    print('installed', dst)
    print('done ->', OUT)


main()
