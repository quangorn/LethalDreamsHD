# tools/tex/hook_ground.py — подмена почвенных текстур тестовым паттерном в
# scratch-дереве игры (known-answer проверка раскода/ориентации `.tex`).
#
# Что делает:
#   1. строит тестовый паттерн (`tools/tex/test_pattern.py`) и пакует его в
#      `<tree>/data/textures/mm_bkg01.btf` шаблоном родного `mm_bkg01.btf`
#      этого дерева (`tools/btf/pack_btf.py`);
#   2. компилирует хук-материалы `patches/tex_hook_opaque.py` /
#      `patches/tex_hook_alpha.py` интерпретатором 2.2.3;
#   3. кладёт собранные .pyc на ВСЕ почвенные материалы
#      `<tree>/data/materials/<map>_opaque_N.pyc` и `<map>_alpha_N.pyc`,
#      оригиналы сохраняет в `<tree>/data/materials_ground_backup/`.
#
# Игра — только scratch-дерево: путь установки отвергается с ошибкой.
#
#   python3 tools/tex/hook_ground.py --tree <scratch-дерево>          # поставить
#   python3 tools/tex/hook_ground.py --tree <scratch-дерево> --restore # вернуть оригиналы
#   python3 tools/tex/tex_hook_probe.ps1 ...                          # съёмка оверленда
import argparse
import os
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'btf'))
sys.path.insert(0, os.path.join(_ROOT, 'tools', 'pyc'))
import paths  # noqa: E402
import pycbuild  # noqa: E402
import pack_btf as P  # noqa: E402
sys.path.insert(0, _HERE)
from test_pattern import build as build_pattern  # noqa: E402

TEST_TEX_NAME = 'mm_bkg01.btf'      # имя ресурса, которое уже есть в .tt-манифестах
HOOKS = ('tex_hook_opaque', 'tex_hook_alpha')
PDIR = os.path.join(_ROOT, 'patches')


def ground_materials(mats_dir):
    """Все почвенные материалы: <map>_opaque_N.pyc / <map>_alpha_N.pyc."""
    out = []
    for fn in sorted(os.listdir(mats_dir)):
        if not fn.endswith('.pyc'):
            continue
        base = fn[:-4]
        if '_opaque_' in base or '_alpha_' in base:
            out.append(fn)
    return out


def build_hooks():
    """Компиляция хук-материалов 2.2.3; возвращает {hook: pyc_path}."""
    made = {}
    for hook in HOOKS:
        src = os.path.join(PDIR, hook + '.py')
        if not os.path.isfile(src):
            raise SystemExit('missing %s' % src)
        srcw = paths.win_path(src)
        out, rc = pycbuild.run_worker('compile', srcw)
        if 'COMPILE-OK' not in out and 'OK' not in out:
            raise SystemExit('COMPILE FAIL %s: %s' % (hook, out.strip()[:400]))
        pyc = os.path.join(PDIR, hook + '.pyc')
        out, rc = pycbuild.run_worker('build', srcw, paths.win_path(pyc))
        if 'BUILD-OK' not in out:
            raise SystemExit('BUILD FAIL %s: %s' % (hook, out.strip()[:400]))
        made[hook] = pyc
    return made


def main():
    ap = argparse.ArgumentParser(description='ground-texture test hook (scratch tree only)')
    ap.add_argument('--tree', required=True, help='scratch-дерево игры')
    ap.add_argument('--restore', action='store_true', help='вернуть оригиналы из backup')
    ap.add_argument('--size', type=int, default=1024, help='размер тестового паттерна')
    ap.add_argument('--solid', action='store_true', help='вся картинка одним цветом')
    ap.add_argument('--quad', default='tl', choices=('tl', 'tr', 'bl', 'br'))
    ap.add_argument('--color', default='white', choices=('white', 'red', 'green', 'blue'))
    a = ap.parse_args()

    tree = paths.norm(a.tree)
    if tree == paths.install_dir():
        raise SystemExit('refusing to touch the installed game: %s '
                         '(experiments go to a scratch tree)' % tree)
    mats = os.path.join(tree, 'data', 'materials')
    tex = os.path.join(tree, 'data', 'textures')
    backup = os.path.join(tree, 'data', 'materials_ground_backup')
    if not os.path.isdir(mats):
        raise SystemExit('no data/materials in tree %s' % tree)

    if a.restore:
        if not os.path.isdir(backup):
            raise SystemExit('no backup dir %s' % backup)
        n = 0
        for fn in sorted(os.listdir(backup)):
            src = os.path.join(backup, fn)
            shutil.copyfile(src, os.path.join(mats, fn))
            n += 1
        print('restored %d ground materials in %s' % (n, mats))
        return

    # 1. тестовый паттерн -> .btf дерева (шаблон = родный .btf этого дерева)
    scratch = paths.scratch_dir('ld')
    png = os.path.join(scratch, 'tex_test_pattern.png')
    w, h, rows = build_pattern(a.size, a.quad, a.color, a.solid)
    from unpack_btf import png_write
    png_write(png, w, h, rows)
    template = os.path.join(tex, TEST_TEX_NAME)
    if not os.path.isfile(template):
        raise SystemExit('template %s missing in tree' % template)
    P.pack_one(png, template, template)   # перезаписывает ресурс дерева паттерном
    print('test pattern %dx%d packed into %s' % (w, h, template))

    # 2. хук-материалы -> .pyc (2.2.3)
    hooks = build_hooks()

    # 3. раскладка по всем почвенным материалам, оригиналы в backup
    os.makedirs(backup, exist_ok=True)
    made = 0
    for fn in ground_materials(mats):
        tgt = os.path.join(mats, fn)
        if not os.path.isfile(os.path.join(backup, fn)):
            shutil.copyfile(tgt, os.path.join(backup, fn))
        hook = 'tex_hook_opaque' if '_opaque_' in fn else 'tex_hook_alpha'
        shutil.copyfile(hooks[hook], tgt)
        made += 1
    print('hooked %d ground materials in %s (originals -> %s)' % (made, mats, backup))
    print('съёмка оверленда дерева:')
    print("  LD_GAME='<tree>' LD_REPO='<repo>' LD_SCRATCH='<scratch>' powershell "
          "-NoProfile -ExecutionPolicy Bypass -File tools/archive/probes/probe_loadshot.ps1 "
          "3840 2160 1 <Tag> -Mode new -SkipClicks 2 -CharX 349 -CharY 658 -EnterCount 2 -LoadWait 5")
    print('реестр для дерева: HIGHDPIAWARE на <tree>/LETHALDREAMS.EXE')
    print('откат: python3 tools/tex/hook_ground.py --tree <tree> --restore')


if __name__ == '__main__':
    main()
