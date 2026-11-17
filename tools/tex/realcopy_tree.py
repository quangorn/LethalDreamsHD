# tools/tex/realcopy_tree.py — перевод hardlink-дерева scratch в безопасный вид:
# файлы, которые игра или наши скрипты перезаписывают, становятся настоящими
# копиями (содержимое берётся из установки); всё остальное остаётся ссылками.
#
# Игра при запуске генерирует `Lib/**/*.pyc` из `.py`, трогает `hd.cfg`,
# `hdlog.txt`, font-сайкары (`.fdf`/`ld_*.btf`) — это проверено снапшотом
# md5+mtime установки до/после прогона. Писать через hardlink = писать в установку,
# поэтому эти файлы обязаны быть копиями.
#
#   python3 tools/tex/realcopy_tree.py --tree <hardlink-дерево>
import argparse
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402


def writable_set(tree):
    """Файлы, которые игра перезаписывает при запуске (по снапшоту md5+mtime),
    + файлы, которые пишут наши скрипты."""
    src = paths.install_dir()
    items = []
    # Lib: игра генерирует .pyc рядом с .py; существующие .pyc перезаписываются
    libdir = os.path.join(tree, 'Lib')
    for dp, dns, fns in os.walk(libdir):
        for f in fns:
            if f.endswith('.pyc'):
                items.append(os.path.join(dp, f))
    # корень: конфиг и лог игры
    for f in ('hd.cfg', 'hdlog.txt'):
        items.append(os.path.join(tree, f))
    # font-сайкары и фон меню (переписываются при запуске / нашим packer'ом)
    for d, pats in (('data/fonts', ('.fdf',)),
                    ('data/textures', ('.btf',))):
        dd = os.path.join(tree, d)
        if not os.path.isdir(dd):
            continue
        for f in sorted(os.listdir(dd)):
            if any(f.endswith(p) for p in pats):
                items.append(os.path.join(dd, f))
    # почвенные материалы: их перезаписывает хук
    md = os.path.join(tree, 'data', 'materials')
    if os.path.isdir(md):
        for f in sorted(os.listdir(md)):
            if f.endswith('.pyc') and ('_opaque_' in f or '_alpha_' in f):
                items.append(os.path.join(md, f))
    return src, items


def main():
    ap = argparse.ArgumentParser(description='make writable files real copies in a hardlink tree')
    ap.add_argument('--tree', required=True)
    a = ap.parse_args()
    tree = paths.norm(a.tree)
    if tree == paths.install_dir():
        raise SystemExit('refusing to touch the installed game: %s' % tree)
    src, items = writable_set(tree)
    done = 0
    for tgt in items:
        rel = os.path.relpath(tgt, tree).replace('\\', '/')
        origin = os.path.join(src, rel)
        if not os.path.isfile(origin):
            continue
        data = open(origin, 'rb').read()
        # unlink removes the shared record, then we write our own file
        if os.path.exists(tgt):
            os.remove(tgt)
        with open(tgt, 'wb') as fh:
            fh.write(data)
        done += 1
    print('real copies: %d files in %s (content taken from %s)' % (done, tree, src))
    # директории, куда игра может писать новые файлы, тоже должны быть своими
    for d in ('data/materials_ground_backup',):
        p = os.path.join(tree, d)
        os.makedirs(p, exist_ok=True)


if __name__ == '__main__':
    main()
