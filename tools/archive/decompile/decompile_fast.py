#!/usr/bin/env python3
"""Полный прогон декомпиляции .pyc с прогрессом, параллельно и с лимитом на файл.

decompiled/<путь>.py; зависший файл помечается и не блокирует цикл.
Использование: python3 decompile_fast.py [-j N] [-t сек] [файл ...]
Без файлов — обход всех .pyc в игре."""
*** ВНИМАНИЕ: каталог decompiled/ удалён из проекта (база = decompiled_fixed/).
*** Если запустить этот скрипт, он воссоздаст decompiled/ и разведёт базу. Не запускать.


import sys, os, signal, time

LIMIT = 120

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
BASE = paths.game_path('data', 'scripts')
OUT = os.path.join(_ROOT, 'decompiled')

def alarm(signum, frame):
    raise TimeoutError()

def one(rel):
    src = os.path.join(BASE, rel)
    dst = os.path.join(OUT, (rel[:-4] + '.py') if rel.endswith('.pyc') else rel)
    dst = dst.replace('/', os.sep)
    t0 = time.time()
    signal.signal(signal.SIGALRM, alarm)
    signal.alarm(LIMIT)
    try:
        from uncompyle6.main import decompile_file
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w', encoding='utf-8') as f:
            decompile_file(src, f)
        r = 'OK'
    except Exception as e:
        r = 'FAIL ' + e.__class__.__name__
    finally:
        signal.alarm(0)
    return (rel, r, round(time.time() - t0, 2))

def main():
    args = sys.argv[1:]
    global LIMIT
    jobs = 8
    targets = []
    i = 0
    while i < len(args):
        if args[i] == '-j':
            jobs = int(args[i + 1]); i += 2
        elif args[i] == '-t':
            LIMIT = int(args[i + 1]); i += 2
        else:
            targets.append(args[i]); i += 1
    if not targets:
        for root, _, files in os.walk(BASE):
            for f in files:
                if f.endswith('.pyc'):
                    targets.append(os.path.relpath(os.path.join(root, f), BASE).replace(os.sep, '/'))
    t0 = time.time()
    ok = 0
    fails = []
    done = 0
    total = len(targets)
    if jobs <= 1:
        for rel in targets:
            rel, r, dt = one(rel)
            done += 1
            if r == 'OK':
                ok += 1
            else:
                fails.append(rel + ' ' + r)
            print('%4d/%4d %6.2fs %s %s' % (done, total, dt, rel, r), flush=True)
    else:
        from multiprocessing import Pool
        with Pool(jobs) as p:
            for rel, r, dt in p.imap_unordered(one, targets):
                done += 1
                if r == 'OK':
                    ok += 1
                else:
                    fails.append(rel + ' ' + r)
                print('%4d/%4d %6.2fs %s %s' % (done, total, dt, rel, r), flush=True)
    print('DONE %d ok, %d fail, %.1fs' % (ok, len(fails), time.time() - t0), flush=True)
    for f in fails[:40]:
        print(' ' + f, flush=True)

if __name__ == '__main__':
    main()
