#!/usr/bin/env python3
"""decompyle_worker.py — py2-воркер под vendor/py223: decompyle 2.3.2 и компиляция.

Запускается только из `tools/pyc/decompyle_base.py` (или вручную для отладки),
интерпретатором `vendor/py223/python.exe`: decompyle 2.3.2 — код Python 2, а
байткод игры — 2.2.3. Режимы:

  batch   <listfile>   строки `src.pyc<TAB>dst.py` — декомпилировать каждый
  compile <listfile>   строки `src.py<TAB>dst.pyc` — скомпилировать и записать .pyc

Печатает по строке на файл: `OK <src>` / `FAIL <src> :: <ошибка>`.
Вывод decompyle обрезается до source: снимаются служебные строки, которые
decompyle печатает в stdout (дата, `+++ okay decompyling …`, `decompyled N files: …`),
и CRLF приводится к LF (репозиторий — LF).
"""
import imp
import marshal
import os
import struct
import sys
import time

NOISE_PREFIX = ('+++ okay decompyling', 'decompyled ', '# emacs-mode')
NOISE_TAIL = ('# local variables:', '# tab-width:')
MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
          'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')


def is_noise(line):
    s = line.strip()
    if not s:
        return False
    for p in NOISE_PREFIX:
        if s.startswith(p):
            return True
    parts = s.split(' ')
    if len(parts) >= 5 and parts[0][:3] in MONTHS and parts[2].isdigit():
        return True       # 'Sat Nov 28 11:31:45 2026'
    return False


def clean(text):
    lines = text.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    while lines and (is_noise(lines[0]) or lines[0] == '# emacs-mode: -*- python-*-' or not lines[0].strip()):
        lines.pop(0)
    while lines and (lines[-1].strip() == '' or is_noise(lines[-1]) or lines[-1].startswith('# tab-width')):
        lines.pop()
    # хвостовой '# local variables:' без пары
    if lines and lines[-1].strip() in NOISE_TAIL:
        lines.pop()
    body = '\n'.join(lines).strip('\n')
    if body:
        return body + '\n'
    return ''


class Out(object):
    def __init__(self):
        self.chunks = []

    def write(self, s):
        self.chunks.append(s)

    def writelines(self, seq):
        for s in seq:
            self.chunks.append(s)

    def flush(self):
        pass

    def text(self):
        return ''.join(self.chunks)


def mode_batch(listfile):
    import decompyle
    ran = failed = 0
    for line in open(listfile, 'rb').read().split('\n'):
        line = line.strip()
        if not line:
            continue
        src, dst = line.split('\t')
        out = Out()
        try:
            decompyle.decompyle_file(src, out)
            text = clean(out.text())
            if not text:
                raise ValueError('empty output')
        except Exception, e:
            failed += 1
            print 'FAIL %s :: %s' % (src, str(e).replace('\n', ' ')[:90])
            continue
        d = os.path.dirname(dst)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        f = open(dst, 'wb')
        f.write(text)
        f.close()
        ran += 1
        print 'OK %s' % src
    print 'BATCH %d ok, %d fail' % (ran, failed)


def mode_compile(listfile):
    magic = imp.get_magic()
    stamp = struct.pack('i', int(time.time()))
    ok = bad = 0
    for line in open(listfile, 'rb').read().split('\n'):
        line = line.strip()
        if not line:
            continue
        src, dst = line.split('\t')
        try:
            code = compile(open(src, 'rb').read(), src.replace('/', '\\'), 'exec')
        except SyntaxError, e:
            bad += 1
            print 'FAIL %s :: syntax line %s: %s' % (src, e.lineno, e.msg)
            continue
        except Exception, e:
            bad += 1
            print 'FAIL %s :: %s' % (src, str(e)[:90])
            continue
        d = os.path.dirname(dst)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        f = open(dst, 'wb')
        f.write(magic + stamp + marshal.dumps(code))
        f.close()
        ok += 1
        print 'OK %s' % src
    print 'COMPILE %d ok, %d fail' % (ok, bad)


def main():
    mode = sys.argv[1]
    if mode == 'batch':
        mode_batch(sys.argv[2])
    elif mode == 'compile':
        mode_compile(sys.argv[2])
    else:
        raise SystemExit('unknown mode %s' % mode)


if __name__ == '__main__':
    main()
