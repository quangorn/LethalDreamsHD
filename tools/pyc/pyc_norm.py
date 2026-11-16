#!/usr/bin/env python3
"""Нормализация сравнения байткода: шум, не влияющий на семантику, вырезается.
Используется pycbuild.py.

Вырезается:
  SET_LINENO - номера строк в декомпиляте сдвинуты (заголовок uncompyle6 вырезан);
  лишний return в конце функции - декомпилятор пишет LOAD_CONST None/RETURN_VALUE
    вторично, это эквивалентно неявному возврату;
  jump-адреса - 'to 334' заменяется относительным delta (длина кода у альфы и финала
    2.2.3 разная, абсолютные адреса никогда не совпадают);
  __name__/__module__ в names класса - альфа 2.2a4 class-байткод без них, финал добавляет;
  repr вложенного кодового объекта - содержит адрес и путь файла, оставляем имя;
  индексы LOAD_CONST (позиция в consts) - сдвигаются, сравнивается само значение.
"""
import re

CODE_RE = re.compile(r'<code object (\S+) at')
OP_RE = re.compile(r'^\s*(\d*)\s+([A-Za-z_][A-Za-z_0-9]*)\s+(\d+)?\s*(?:\((.*)\))?\s*$')


def norm_ops(raw_lines):
    """raw_lines - строки dis; returns список (opname, value) без шумa."""
    ops = []
    for line in raw_lines:
        line = line.rstrip('\r')
        m = OP_RE.match(line)
        if not m:
            continue
        off, op, arg, rep = m.group(1), m.group(2), m.group(3), m.group(4)
        if op == 'SET_LINENO':
            continue
        if op in ('LOAD_CONST', 'LOAD_GLOBAL', 'LOAD_NAME'):
            # индекс аргумента не значим, значимо значение
            value = rep if rep is not None else arg
            if value is not None and value.startswith('<code object '):
                mm = CODE_RE.match(value)
                value = 'code:' + (mm.group(1) if mm else '?')
            ops.append((op, value))
        elif op.startswith('JUMP_') or op == 'FOR_ITER' or op.startswith('SETUP_'):
            # нормализуем цель: относительный delta от текущего offset
            t = int(arg) if arg is not None else None
            ops.append((op, 'to+%s' % (t - int(off))))
        else:
            value = rep if rep is not None else arg
            if value is not None and value.startswith('<code object '):
                mm = CODE_RE.match(value)
                value = 'code:' + (mm.group(1) if mm else '?')
            ops.append((op, value))
    # хвостовой лишний return: приводим к одной паре LOAD_CONST None/RETURN_VALUE
    pair = [('LOAD_CONST', 'None'), ('RETURN_VALUE', None)]
    while len(ops) >= 2 and ops[-2:] == pair:
        ops = ops[:-2]
    ops += pair
    return ops


def tolerant_ops(ops):
    """Дополнительные допуски для оценки близости (диагностика, не гарантия эквивалентности):
    - class-тело: альфа 2.2a4 не эмитит LOAD_GLOBAL __name__/STORE_NAME __module__;
    - jump-цели: delta зависит от длины кода (у альфы инициализация констант идёт
      в другом порядке), сравниваем только тип перехода;
    - литерал dict: порядок пар key/value в исходнике differs, важна только количество пар."""
    out = []
    i = 0
    n = len(ops)
    while i < n:
        op, val = ops[i]
        if op == 'LOAD_GLOBAL' and val == '__name__' and i + 1 < n and \
           ops[i + 1] == ('STORE_NAME', '__module__'):
            i += 2
            continue
        if op.startswith('JUMP_') or op == 'FOR_ITER' or op.startswith('SETUP_'):
            out.append((op, None))
            i += 1
            continue
        if op == 'LOAD_CONST':
            # gather run of LOAD_CONST before BUILD_MAP -> dict literal
            j = i
            while j < n and ops[j][0] == 'LOAD_CONST':
                j += 1
            if j < n and ops[j][0] == 'BUILD_MAP':
                out.append(('BUILD_MAP', (j - i) // 2))
                i = j + 1
                continue
            out.append((op, val))
            i += 1
            continue
        out.append((op, val))
        i += 1
    # peephole альфы: -x лежит константой, финал 2.2.3 даёт LOAD_CONST x + UNARY_NEGATIVE
    merged = []
    for op, val in out:
        if op == 'UNARY_NEGATIVE' and merged and merged[-1][0] == 'LOAD_CONST':
            v = merged[-1][1]
            merged[-1] = ('LOAD_CONST', '-' + str(v))
        else:
            merged.append((op, val))
    # порядок пар констант вне dict-литералов (инициализация словарей/кортежей у альфы
    # идёт в другом порядке): длинный run LOAD_CONST сворачиваем в количество
    final = []
    i = 0
    while i < len(merged):
        op, val = merged[i]
        if op == 'LOAD_CONST':
            j = i
            while j < len(merged) and merged[j][0] == 'LOAD_CONST':
                j += 1
            if j - i >= 4:
                final.append(('CONSTS', j - i))
                i = j
                continue
        final.append((op, val))
        i += 1
    return final



def norm_names(names):
    """Имена модуля/класса: __name__/__module__ у класса альфа не эмитит."""
    out = []
    for n in names.split(' '):
        if n in ('__name__', '__module__'):
            continue
        out.append(n)
    return ' '.join(out)


def norm_signature(line):
    """SIGNATURE: flags у класса (альфа/финал) различаются, сравниваем args/nlocals/names."""
    m = re.match(r'SIGNATURE (\S+) args=(\d+) nlocals=(\d+) flags=(\d+) stack=(\d+) names=(\S*) consts=(.*)$', line)
    if not m:
        return None
    name, args, nloc, flags, stack, names, consts = m.groups()
    # stacksize и flags не сравниваем (разная оптимизация альфы/финала), имена - нормализованно
    return 'SIG %s args=%s nlocals=%s names=%s consts=%s' % (name, args, nloc, norm_names(names), consts)


def similarity(a, b):
    """Доля совпавших операций (0..1) между двумя нормализованными последовательностями."""
    if not a and not b:
        return 1.0
    n = max(len(a), len(b))
    same = sum(1 for x, y in zip(a, b) if x == y)
    return same / n
