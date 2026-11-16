#!/usr/bin/env python3
"""uninstall_hd.py - полный откат HD/Q-патча Lethal Dreams к оригинальной установке.
Файл лежит в tools/pyc/; снимок оригиналов backup_orig/ - в корне репозитория,
игровые пути берутся из install_dir (config.toml).

Что делает (по порядку):
  1. .pyc: все модули из backup_orig (gamescript, ui/* и т.д.), плюс удаление
     наших модулей hd_res/hd_scale/hd_tex.
  2. EXE: побайтово из backup_orig/LETHALDREAMS.EXE.orig (снимает exepatch12
     (пещеры масштаба листов + проекция мира) и все прежние exepatch*).
  3. Шрифты/листы: удаляет сайдкары *_169.* / *_43.* и Q-листы *q.* из
     data/fonts и data/textures; mm_bkg01.btf восстанавливается из
     backup_orig/mm_bkg01_orig.btf (hd_res.Prepare меняет его при старте).
  4. data/common.tt: вырезает строки регистрации Q-листов (ld_*q.tga).
  5. Артефакты: удаляет hd.cfg, hdlog.txt, blackbg.flag из корня игры.
  6. Проверяет md5 восстановленных файлов против backup_orig и печатает список.

  python3 tools/pyc/uninstall_hd.py            # только проверка (ничего не меняет)
  python3 tools/pyc/uninstall_hd.py --check    # то же самое
  python3 tools/pyc/uninstall_hd.py --install  # откат: запись оригиналов в игру
                                                       # (правило AGENTS.md: явный флаг)
"""
import hashlib
import os
import re
import shutil
import sys

# tools/pyc -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths

REPO_ROOT = paths.win_path(_ROOT)
GAME = paths.install_dir()
BO = os.path.join(REPO_ROOT, 'backup_orig')
SCRIPTS = os.path.join(GAME, 'data', 'scripts')
FONTS = os.path.join(GAME, 'data', 'fonts')
TEX = os.path.join(GAME, 'data', 'textures')

# наши .pyc в игре (не оригинальные)
OUR_PYC = ['hd_res.pyc', 'hd_scale.pyc', 'hd_tex.pyc', 'hd_scale.pyc.origcheck']
# восстановленные .pyc: (путь_вbackup, путь_вигре)
PYC_MAP = [
    ('gamescript.pyc', 'gamescript.pyc'),
    ('ui/bspellbook.pyc', 'ui/bspellbook.pyc'),
    ('ui/enmenu.pyc', 'ui/enmenu.pyc'),
    ('ui/help.pyc', 'ui/help.pyc'),
    ('ui/ui.pyc', 'ui/ui.pyc'),
    ('ui/cursor.pyc', 'ui/cursor.pyc'),
    ('ui/battlemenu.pyc', 'ui/battlemenu.pyc'),
    ('ui/btutorial.pyc', 'ui/btutorial.pyc'),
    ('ui/olmenu.pyc', 'ui/olmenu.pyc'),
]
QTT = ['ld_tahoma1q.tga', 'ld_uncial1q.tga', 'ld_uncial2q.tga', 'ld_numbers1q.tga']


def md5(p):
    return hashlib.md5(open(p, 'rb').read()).hexdigest()


def strip_tt(path, names):
    d = open(path, 'rb').read()
    out = []
    for ln in d.split(b'\r\n'):
        head = ln.split(b'|')[0].strip().decode('latin-1')
        if head in names:
            continue
        out.append(ln)
    new = b'\r\n'.join(out)
    if new != d:
        open(path, 'wb').write(new)
        print('common.tt: снято строк:', len(d.split(b'\r\n')) - len(out))
    else:
        print('common.tt: Q-строк нет')


def main():
    # запись в установку — только под явным --install; по умолчанию проверка
    check = ('--install' not in sys.argv) or ('--check' in sys.argv)
    for b, g in PYC_MAP:
        src = os.path.join(BO, b.replace('/', os.sep))
        dst = os.path.join(SCRIPTS, g.replace('/', os.sep))
        if not check and os.path.exists(src):
            shutil.copyfile(src, dst)
            os.utime(dst, None)
        ok = md5(src) == md5(dst)
        print(('OK ' if ok else 'BAD ') + dst + ' ' + md5(dst)[:8])
    exeb = os.path.join(BO, 'LETHALDREAMS.EXE.orig')
    exe = os.path.join(GAME, 'LETHALDREAMS.EXE')
    if not check:
        shutil.copyfile(exeb, exe)
    print(('OK ' if md5(exeb) == md5(exe) else 'BAD ') + exe + ' ' + md5(exe)[:8])
    for nm in ('ld_uncial12', 'ld_uncial20', 'ld_tahoma11', 'ld_numbers15'):
        for ext, d in (('.fdf', FONTS), ('.btf', TEX)):
            for sfx in ('_169', '_43', 'q'):
                p = os.path.join(d, nm + sfx + ext)
                if os.path.exists(p):
                    if check:
                        print('PRESENT(need remove)', p)
                    else:
                        os.remove(p)
                        print('removed', p)
    # Q-листы с собственными именами (ld_tahoma1q и т.д.)
    for qn in ('ld_tahoma1q', 'ld_uncial1q', 'ld_uncial2q', 'ld_numbers1q'):
        for ext, d in (('.fdf', FONTS), ('.btf', TEX)):
            p = os.path.join(d, qn + ext)
            if os.path.exists(p):
                if check:
                    print('PRESENT(need remove)', p)
                else:
                    os.remove(p)
                    print('removed', p)
    for nm in ('mm_bkg01_169.btf', 'mm_bkg01_43.btf', 'mm_bkg01_black_169.btf',
              'mm_bkg01_black_43.btf'):
        p = os.path.join(TEX, nm)
        if os.path.exists(p):
            if check:
                print('PRESENT(need remove)', p)
            else:
                os.remove(p)
                print('removed', p)
    # mm_bkg01.btf: восстановить оригинал
    src = os.path.join(BO, 'mm_bkg01_orig.btf')
    dst = os.path.join(TEX, 'mm_bkg01.btf')
    if not check:
        shutil.copyfile(src, dst)
    print(('OK ' if md5(src) == md5(dst) else 'BAD ') + dst + ' ' + md5(dst)[:8])
    # common.tt
    tt = os.path.join(GAME, 'data', 'common.tt')
    if check:
        d = open(tt, 'rb').read()
        n = sum(1 for x in QTT if x.encode() in d)
        print('common.tt Q-строк:', n)
    else:
        strip_tt(tt, QTT)
    # артефакты
    for f in ('hd.cfg', 'hdlog.txt', 'blackbg.flag'):
        p = os.path.join(GAME, f)
        if os.path.exists(p):
            if check:
                print('PRESENT(need remove)', p)
            else:
                os.remove(p)
                print('removed', p)
    # наши pyc
    for f in OUR_PYC:
        p = os.path.join(SCRIPTS, f)
        if os.path.exists(p):
            if check:
                print('PRESENT(need remove)', p)
            else:
                os.remove(p)
                print('removed', p)


if __name__ == '__main__':
    main()
