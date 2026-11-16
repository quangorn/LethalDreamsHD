#!/usr/bin/env python3
"""Собрать patches/<module>.py (каталог в корне репозитория) компилятором 2.2.3
и установить .pyc в игру. Файл скрипта лежит в tools/pyc/.
Назначение: точечный накат собственного модуля (hd_scale/hd_tex) без трогания
оригинальных .pyc и общей tree build/.

  python3 tools/pyc/install_patch.py hd_scale          # собрать, НЕ писать в игру
  python3 tools/pyc/install_patch.py hd_scale --install  # записать .pyc в игру

Запись в игровую установку — только под явным --install (правило AGENTS.md:
установка читается, пишет только этот скрипт и только по флагу). Без флага
сборка идёт в patches/<module>.pyc (gitignored-артефакт) и печатается адрес.
"""
import os
import shutil
import sys

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths
import pycbuild

# patches/<module>.py - в корне репозитория
PDIR = os.path.join(pycbuild.REPO_ROOT, 'patches')


def build_module(name):
    src = os.path.join(PDIR, name + '.py')
    if not os.path.exists(src):
        raise Exception('нет источника patches/%s.py' % name)
    out, rc = pycbuild.run_worker('compile', src.replace(os.sep, '/'))
    if 'COMPILE-OK' not in out:
        raise Exception('COMPILE FAIL: %s' % out.strip()[:400])
    pyc = os.path.join(PDIR, name + '.pyc')
    out, rc = pycbuild.run_worker('build', src.replace(os.sep, '/'), pyc.replace(os.sep, '/'))
    if 'BUILD-OK' not in out:
        raise Exception('BUILD FAIL: %s' % out.strip()[:400])
    return pyc


def build_and_install(name, install=True):
    pyc = build_module(name)
    dst = os.path.join(pycbuild.GAME_SCRIPTS, name)
    if not install:
        print('built %s; установка НЕ тронута (для записи нужен --install)' % pyc)
        print('цель при --install: %s.pyc' % dst)
        return None
    shutil.copyfile(pyc, dst + '.pyc')
    os.utime(dst + '.pyc', None)
    print('installed', dst + '.pyc')
    return dst + '.pyc'


if __name__ == '__main__':
    install = '--install' in sys.argv[1:]
    names = [a for a in sys.argv[1:] if a != '--install']
    for name in names:
        build_and_install(name, install=install)
