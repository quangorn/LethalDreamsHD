#!/usr/bin/env python3
"""Delta-debugging (ddmin): ищем минимальный набор модулей, при котором движок
выдаёт «Error while starting LethalDreams».

Свойство: test(set) == True  <=>  прогон успешен (нет диалога, нет кода выхода
0xFFFFFFFF). Отказ монотонен по set: если набор валится, валится и любой его
надмножество.

Кандидаты логируются (tag = c<len>_<i>), F после каждого сокращения печатается,
в конце финальная проверка найденного набора.
"""
import json, os, re, subprocess, sys


_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_HERE)))
PROJ = _ROOT
PROBE = os.path.join(_ROOT, 'tools', 'archive', 'probes', 'probe_measure.ps1')
RESTORE = os.path.join(_ROOT, 'tools', 'archive', 'probes', 'restore_res.ps1')


def mods_all():
    man = json.load(open(os.path.join(PROJ, 'decompiled_manifest.json'), encoding='utf-8'))
    return sorted(k[:-3] for k in man.keys() if k[:-3] != 'dictionary_rus')


def test(items, tag):
    # устанавливает только эти модули (остальные - оригиналы)
    open(tag + '.txt', 'w').write('\n'.join(items))
    subprocess.run(['python3', 'tools/pyc/pycbuild.py', 'install-list', tag + '.txt'],
                   capture_output=True, cwd=_ROOT)
    subprocess.run(['powershell', '-NoProfile', '-Command',
                    'Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force'],
                   capture_output=True)
    p = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', PROBE,
                        '-WaitSec', '3', '-Tag', tag], capture_output=True)
    out = p.stdout.decode('utf-8', 'replace')
    sizes = []
    for m in re.finditer(r'size=(\d+)x(\d+)', out):
        sizes.append((int(m.group(1)), int(m.group(2))))
    # игра = 1280x960 fullscreen; диалог «Error while starting» = 1023x769/476x291
    bad = ('EXITED' in out) or any((w, h) in [(1023, 769), (476, 291)] for w, h in sizes)
    ok = not bad
    subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', RESTORE],
                   capture_output=True)
    print('  %-12s n=%-4d sizes=%s -> %s' % (tag, len(items), sizes, 'PASS' if ok else 'FAIL'),
          flush=True)
    return ok


def ddmin(F, n=2):
    """Канонический ddmin: F валится, |F| >= n. Возвращает минимальный отказной набор."""
    while len(F) >= n:
        chunks = [F[i * len(F) // n:(i + 1) * len(F) // n] for i in range(n)]
        found = False
        for i, c in enumerate(chunks):
            cand = F[:i * len(F) // n] + F[(i + 1) * len(F) // n:]
            if not cand:
                continue
            if test(cand, 'c%d_%d' % (n, i)):
                F = cand
                found = True
                n = max(n - 1, 1)
                break
        if not found:
            if n >= len(F):
                return F
            n = min(n + 1, len(F))
    return F


def main():
    mods = mods_all()
    F = mods[:427]
    # контроль: полный набор F должен валиться
    if test(F, 'ddctl'):
        print('набор F проходит - бисект не имеет смысла')
        return
    R = ddmin(F)
    print('кандидат (%d):' % len(R))
    for m in R:
        print('  ', m)
    # финальная проверка кандидата
    if test(R, 'verify'):
        print('кандидат НЕ валится - свойство не монотонно / шум прогона')
    else:
        print('кандидат подтверждён')
        open('dd_min.txt', 'w').write('\n'.join(R))


if __name__ == '__main__':
    main()
