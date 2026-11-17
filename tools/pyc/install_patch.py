#!/usr/bin/env python3
"""Собрать patches/<module>.py (каталог в корне репозитория) компилятором 2.2.3
и установить .pyc в игру. Файл скрипта лежит в tools/pyc/.
Назначение: точечный накат собственного модуля (hd_scale/hd_tex) без трогания
оригинальных .pyc и общей tree build/.

  python3 tools/pyc/install_patch.py hd_scale          # собрать, НЕ писать в игру
  python3 tools/pyc/install_patch.py hd_scale --install  # записать .pyc в игру

Перед записью header .pyc переводится на `pyc_source_mtime` манифеста (рецепт
`docs/PATCHES.md`, «Рецепт .pyc»: воркер пишет mtime = время сборки). md5
канонической компиляции сверяется с манифестом.

Запись в игровую установку — только под явным --install (правило AGENTS.md:
установка читается, пишет только этот скрипт и только по флагу). Без флага
сборка идёт в patches/<module>.pyc (gitignored-артефакт) и печатается адрес.
"""
import hashlib
import json
import struct
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


def manifest_entry(name):
    """Запись code tier манифеста для модуля (None, если модуля там нет)."""
    p = os.path.join(pycbuild.REPO_ROOT, 'release', 'manifest.json')
    if not os.path.exists(p):
        return None
    with open(p, encoding='utf-8') as fh:
        m = json.load(fh)
    for e in m.get('code_tier', {}).get('entries', []):
        if e.get('module') == name:
            return e
    return None


def fix_header(pyc, name):
    """Рецепт .pyc (`docs/PATCHES.md`, «Рецепт .pyc»): заголовок = magic + mtime
    исходника из манифеста. Воркер пишет mtime = время сборки, поэтому
    установщик обязан переписать `bytes[4:8]` на `pyc_source_mtime` — иначе
    записанный в игру .pyc не равен base-печати манифеста."""
    e = manifest_entry(name)
    if not e or not e.get('pyc_source_mtime'):
        print('  в манифесте нет pyc_source_mtime для %s: header остался build-time' % name)
        return None
    d = bytearray(open(pyc, 'rb').read())
    d[4:8] = struct.pack('<i', e['pyc_source_mtime'])
    with open(pyc, 'wb') as fh:
        fh.write(bytes(d))
    got = hashlib.md5(bytes(d)).hexdigest()
    print('  header mtime -> pyc_source_mtime %d' % e['pyc_source_mtime'])
    if got == e.get('md5'):
        print('  md5 %s == manifest %s: OK (канонический путь репозитория)'
              % (got, e.get('md5')))
    else:
        print('  md5 %s != manifest %s: компиляция НЕ из канонического пути'
              ' репозитория (co_filename в теле)' % (got, e.get('md5')))
    return got


def build_and_install(name, install=True):
    pyc = build_module(name)
    dst = os.path.join(pycbuild.GAME_SCRIPTS, name)
    if not install:
        fix_header(pyc, name)
        print('built %s; установка НЕ тронута (для записи нужен --install)' % pyc)
        print('цель при --install: %s.pyc' % dst)
        return None
    fix_header(pyc, name)
    shutil.copyfile(pyc, dst + '.pyc')
    os.utime(dst + '.pyc', None)
    print('installed', dst + '.pyc')
    return dst + '.pyc'


if __name__ == '__main__':
    install = '--install' in sys.argv[1:]
    names = [a for a in sys.argv[1:] if a != '--install']
    for name in names:
        build_and_install(name, install=install)
