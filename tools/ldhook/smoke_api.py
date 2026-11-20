#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Локальный смоук Python-API ldhook.pyd (Фаза 3, PLAN_proxy_dll.md §4).

Грузит расширение в интерпретатор 2.2.3 (vendor/py223), без игры:
проверяет `log` / `set_level` / `stats`, уровень 0 (нет записи) и TypeError
на неверные типы. Заодно проверяет, что скан `.text` идёт по PE-заголовкам:
под python.exe сигнатур сайтов нет, в логе три `signature NOT found`.

    PYTHONDONTWRITEBYTECODE=1 LDHOOK_LOG=2 vendor/py223/python.exe \
        tools/ldhook/smoke_api.py build/ldhook/ldhook.pyd

Лог `hdlog.txt` создаётся рядом с python.exe (каталог EXE) — после прогона
его и `ldhook_marker.txt` можно удалить: в игру они не попадают.
"""
import imp
import os
import sys


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: smoke_api.py <path-to-ldhook.pyd>")
    pyd = os.path.abspath(sys.argv[1])
    m = imp.load_dynamic('ldhook', pyd)

    m.log('hello from local test')
    assert m.set_level(2) in (1, 2), 'set_level returns previous level'
    s = m.stats()
    assert len(s) == 12 and s[3] == 2, ('stats tuple (site1, site2, site3, level, '
                                        'fc_calls, fc_iters, fc_code1, fc_code3, '
                                        'fc_code4, fc_rot, fc_us_sum, fc_us_max)')
    sys.stdout.write('stats -> %r\n' % (s,))

    for name in ('set_cap', 'cap_stats', 'diag_stats'):
        assert not hasattr(m, name), '%s must be removed from the API' % name

    s6 = m.site6_stats()
    assert len(s6) == 4, 'site6_stats tuple (calls, heavy, max_iter, level)'
    sys.stdout.write('site6_stats -> %r\n' % (s6,))

    assert m.set_level(0) == 2
    m.log('this line must NOT be in hdlog.txt')
    assert m.set_level(1) == 0
    sys.stdout.write('stats -> %r\n' % (m.stats(),))

    try:
        m.set_level('x')
    except TypeError:
        pass
    else:
        raise SystemExit('expected TypeError from set_level')
    try:
        m.log(123)
    except TypeError:
        pass
    else:
        raise SystemExit('expected TypeError from log')
    sys.stdout.write('OK: ldhook Python API\n')


if __name__ == '__main__':
    main()
