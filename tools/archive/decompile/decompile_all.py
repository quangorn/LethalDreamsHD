#!/usr/bin/env python3
"""Декомпилятор .pyc (Python 2.2) скриптов Lethal Dreams через uncompyle6.
Кладёт результаты в decompiled/<путь>.py, сохраняет исходную mtime .pyc."""
*** ВНИМАНИЕ: каталог decompiled/ удалён из проекта (база = decompiled_fixed/).
*** Если запустить этот скрипт, он воссоздаст decompiled/ и разведёт базу. Не запускать.


import sys, os, time, marshal
from uncompyle6.main import decompile_file


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
BASE = paths.game_path('data', 'scripts')
OUT = os.path.join(_ROOT, 'decompiled')

def one(rel):
    src = os.path.join(BASE, rel)
    rel_py = rel[:-4] + '.py' if rel.endswith('.pyc') else rel
    dst = os.path.join(OUT, rel_py.replace('/', os.sep))
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    try:
        with open(dst, 'w', encoding='utf-8') as f:
            decompile_file(src, f)
        return True
    except Exception as e:
        return 'FAIL %s: %s' % (rel, e.__class__.__name__)

if __name__ == '__main__':
    targets = sys.argv[1:]
    if not targets:
        targets = []
        for root, _, files in os.walk(BASE):
            for f in files:
                if f.endswith('.pyc'):
                    targets.append(os.path.relpath(os.path.join(root, f), BASE).replace(os.sep, '/'))
    fails = []
    n = 0
    for rel in targets:
        r = one(rel)
        if r is True:
            n += 1
        else:
            fails.append(r)
    print('decompiled %d files, %d failures' % (n, len(fails)))
    for f in fails[:25]:
        print(f)
