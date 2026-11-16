#!/usr/bin/env python3
"""Пофайловый замер времени декомпиляции .pyc (uncompyle6). Печатает прогресс сразу."""
import sys, os, time
from uncompyle6.main import decompile_file


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
BASE = paths.game_path('data', 'scripts')

def timed(rel, limit):
    src = os.path.join(BASE, rel)
    t0 = time.time()
    try:
        with open(os.devnull, 'w', encoding='utf-8') as f:
            decompile_file(src, f)
        r = 'OK'
    except Exception as e:
        r = 'FAIL ' + e.__class__.__name__
    dt = time.time() - t0
    print('%6.2fs %s %s' % (dt, rel, r), flush=True)
    return dt

if __name__ == '__main__':
    for rel in sys.argv[1:]:
        timed(rel, 0)
