#!/usr/bin/env python3
"""Снимок декомпилированных исходников: decompiled/ -> decompiled_snap/ + манифест.

Нужен, чтобы гейты (компиляция 2.2.3, паритет байткода) прогонялись по замороженному
набору файлов: каталог decompiled/ пишется декомпилятором, и в момент прогона
файлы не должны меняться. Заодно фиксируется соответствие .py <-> оригинальный .pyc.

Использование:
    python3 pin_decompiled.py            # снять снимок
    python3 pin_decompiled.py --check    # сравнить decompiled/ со снимком
"""
*** ВНИМАНИЕ: decompiled/ и decompiled_snap/ удалены; база правок = decompiled_fixed/.
*** Скрипт устарел: запуск воссоздаст decompiled_snap/ из ничего. Не запускать.


import hashlib, json, os, shutil, sys


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
sys.path.insert(0, _ROOT)
import paths
PROJ = _ROOT
SRC = os.path.join(PROJ, 'decompiled')
DST = os.path.join(PROJ, 'decompiled_snap')
GAME = paths.game_path('data', 'scripts')
MANIFEST = os.path.join(PROJ, 'decompiled_manifest.json')


def scan(base):
    out = {}
    for root, _, files in os.walk(base):
        for f in files:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, base).replace(os.sep, '/')
            out[rel] = {
                'md5': hashlib.md5(open(p, 'rb').read()).hexdigest(),
                'size': os.path.getsize(p),
                'mtime': int(os.path.getmtime(p)),
            }
    return out


def main():
    check = '--check' in sys.argv
    cur = scan(SRC)
    if check:
        snap = json.load(open(MANIFEST, encoding='utf-8'))
        added = sorted(set(cur) - set(snap))
        removed = sorted(set(snap) - set(cur))
        changed = sorted(k for k in set(cur) & set(snap) if cur[k]['md5'] != snap[k]['md5'])
        print('snapshot: %d files; new %d, removed %d, changed %d' % (len(snap), len(added), len(removed), len(changed)))
        for k in added: print('  NEW    ', k)
        for k in removed: print('  REMOVED', k)
        for k in changed: print('  CHANGED', k)
        return
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    os.makedirs(DST)
    man = {}
    for rel, info in sorted(cur.items()):
        dst = os.path.join(DST, rel.replace('/', os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(SRC, rel.replace('/', os.sep)), dst)
        # путь оригинального .pyc в игре (по имени модуля)
        pyc = os.path.join(GAME, rel.replace('.py', '.pyc').replace('/', os.sep))
        info['pyc'] = pyc.replace('\\', '/') if os.path.exists(pyc) else None
        man[rel] = info
    json.dump(man, open(MANIFEST, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    nopyc = [k for k in man if man[k]['pyc'] is None]
    print('pinned %d files -> decompiled_snap/, manifest written' % len(man))
    if nopyc:
        print('без оригинального .pyc (%d): %s' % (len(nopyc), ', '.join(nopyc)))


if __name__ == '__main__':
    main()
