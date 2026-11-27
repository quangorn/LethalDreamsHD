#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""root_pyc_layer.py - снятие корневых stdlib-`.pyc` (Python 2.2.3) из установки.

Зачем: корневые `.pyc` - компиляты stdlib, собранные движком из `.py`-копий,
лежавших в корне игры (сами `.py` удалены). Они не нужны: stdlib берётся из
`Lib/` (sys.path интерпретатора = `['', <каталог python22.dll>\\DLLs, ...]`,
проверено на том же 2.2.3). Побочный вред: `''` (cwd) идёт первым, поэтому
любой Python 3, запущенный с cwd = корень игры, падает на
`ImportError: bad magic number in 'types'`.

Проверено на scratch-копии 27.11.2026: без корневых `.pyc` игра проходит
меню -> выбор персонажа -> оверленд (снимки, hdlog с загрузкой ldhook из
`ui/ui.pyc`); `Lib/` при этом не переписывается.

  python3 tools/pyc/root_pyc_layer.py --check     # сверить md5, ничего не менять
  python3 tools/pyc/root_pyc_layer.py --install   # бэкап в scratch + удаление
  python3 tools/pyc/root_pyc_layer.py --restore   # вернуть из бэкапа

Запись в игровую установку - только под явным флагом (правило AGENTS.md).
"""
import hashlib
import os
import shutil
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

# имя -> md5 эталонной установки (замер 27.11.2026)
LAYER = [
    ('__future__.pyc', '00458d4440f9374b3c877b92cfb06c37'),
    ('copy.pyc', 'f17cf161fa817a0a14da1f677f6f24d8'),
    ('linecache.pyc', 'e9a9fd373a53a1fa50fec58d429f4439'),
    ('ntpath.pyc', 'cb2f989a7c74c99e673137cadffc5d26'),
    ('os.pyc', '8ef4a595979a1f74b70569fcc5062576'),
    ('site.pyc', 'a4f3adf6ddd10b79be4fc7d7fa3369a1'),
    ('stat.pyc', '072573807f20b4647bf02aee15b70183'),
    ('string.pyc', 'efadf465a6ccc0f62824979d57e779e6'),
    ('traceback.pyc', '477b7f0b274bdc3bb3ac9f7930a67204'),
    ('types.pyc', '4cf20b99c196782f3cba62c64de75e63'),
    ('whrandom.pyc', 'e4dbaea3b25e4d8c32580aa5a8c2f3ca'),
]


def md5(p):
    with open(p, 'rb') as fh:
        return hashlib.md5(fh.read()).hexdigest()


def backup_dir():
    return os.path.join(paths.scratch_dir('root_pyc_layer'), 'backup')


def main():
    game = paths.install_dir()
    args = set(sys.argv[1:])
    bdir = backup_dir()

    if '--restore' in args:
        if not os.path.isdir(bdir):
            raise SystemExit('нет бэкапа: %s' % bdir)
        n = 0
        for name, want in LAYER:
            src = os.path.join(bdir, name)
            if not os.path.exists(src):
                print('SKIP %-16s нет в бэкапе' % name)
                continue
            shutil.copyfile(src, os.path.join(game, name))
            print('RESTORED %-15s %s' % (name, md5(os.path.join(game, name))))
            n += 1
        print('восстановлено: %d' % n)
        return

    # --check / --install: сверка того, что стоит сейчас
    present, absent, drift = [], [], []
    for name, want in LAYER:
        p = os.path.join(game, name)
        if not os.path.exists(p):
            absent.append(name)
        elif md5(p) != want:
            drift.append((name, md5(p)))
        else:
            present.append(name)
    print('игра: %s' % game)
    print('совпадают с эталоном: %d, отсутствуют: %d, дрейф: %d'
          % (len(present), len(absent), len(drift)))
    for name, got in drift:
        print('DRIFT %-16s md5=%s != %s' % (name, got, dict(LAYER)[name]))
    if absent:
        print('отсутствуют: %s' % ', '.join(absent))

    if '--install' not in args:
        print('режим проверки (--install не указан): ничего не меняю')
        return
    if drift:
        raise SystemExit('найден дрейф - удаление запрещено (разбери файлы вручную)')

    os.makedirs(bdir, exist_ok=True)
    for name in present:
        shutil.copyfile(os.path.join(game, name), os.path.join(bdir, name))
        assert md5(os.path.join(bdir, name)) == dict(LAYER)[name]
        os.remove(os.path.join(game, name))
        print('REMOVED %-16s' % name)
    print('бэкап: %s (восстановление: --restore)' % bdir)


main()
