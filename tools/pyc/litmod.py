#!/usr/bin/env python3
"""litmod.py — восстановление source «плоского литерального» модуля по bytecode.

Fallback для `tools/pyc/decompyle_base.py`: decompyle 2.3.2 не берёт модули
с очень длинным телом (у `dictionary_rus` 10 561 операция в одном кодовом
объекте — его spark-парсер падает «Stack overflow»). Такие модули состоят из
одних присваиваний литералов: `name = <литерал>`, словарей `{k: v}` (в 2.2 —
`BUILD_MAP` + `DUP_TOP; <value>; ROT_TWO; <key>; STORE_SUBSCR`) и склейки
строк с `d_tools.*`.

Эмиттер — стековая машина по `co_code` (только литеральные опкоды). Верность
проверяется гейтом: source компилируется `vendor/py223` и сравнивается с родным
`.pyc` по «ядру» опкодов (`pyc_bytes.core`). Неподдержанный опкод — исключение,
то есть тихая порча невозможна.

Использование:
  from litmod import emit_module, LitModError
  text = emit_module(code_dict)      # code_dict — как из pyc_scan.read
"""
import sys

import pyc_bytes as PB

HAVE_ARG_PUSHV = ()


class LitModError(Exception):
    pass


def _cstr(v):
    """py2 bytes -> литерал source (с \\x экранированием непечатного)."""
    out = ["'"]
    for ch in v:
        c = ch if isinstance(ch, int) else ord(ch)
        if c == 0x27:
            out.append("\\'")
        elif c == 0x5c:
            out.append('\\\\')
        elif c == 0x0a:
            out.append('\\n')
        elif c == 0x0d:
            out.append('\\r')
        elif c == 0x09:
            out.append('\\t')
        elif 0x20 <= c < 0x7f:
            out.append(chr(c))
        else:
            out.append('\\x%02x' % c)
    out.append("'")
    return ''.join(out)


def _lit(v):
    if v is None:
        return 'None'
    if v is True or v is False:
        return 'True' if v else 'False'
    if isinstance(v, (bytes, bytearray)):
        return _cstr(bytes(v))
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, tuple):
        if len(v) == 1:
            return '(' + _lit(v[0]) + ',)'
        return '(' + ', '.join(_lit(x) for x in v) + ')'
    raise LitModError('unsupported const %r' % (v,))


class _Map(object):
    def __init__(self):
        self.items = []

    def src(self):
        return '{' + ', '.join('%s: %s' % (_src(k), _src(v)) for k, v in self.items) + '}'


class _List(object):
    def __init__(self, items):
        self.items = items

    def src(self):
        return '[' + ', '.join(_src(x) for x in self.items) + ']'


class _Import(object):
    def __init__(self, name):
        self.name = name


def _src(x):
    if isinstance(x, (_Map, _List)):
        return x.src()
    return x


def emit_module(code):
    """code — dict как из pyc_scan.read (code/names/consts/varnames)."""
    names = [n.decode('latin1') if isinstance(n, bytes) else n for n in code['names']]
    consts = code['consts']
    stack = []
    lines = []
    for op, arg in PB.core(code):
        name = PB.OPS.get(op)
        if name == 'LOAD_CONST':
            stack.append(_lit(consts[arg]))
        elif name in ('LOAD_NAME', 'LOAD_GLOBAL'):
            stack.append(names[arg])
        elif name == 'LOAD_FAST':
            stack.append(code['varnames'][arg].decode('latin1'))
        elif name == 'LOAD_ATTR':
            stack.append('%s.%s' % (stack.pop(), names[arg]))
        elif name == 'BINARY_ADD':
            b = stack.pop()
            a = stack.pop()
            stack.append('%s + %s' % (a, b))
        elif name == 'BUILD_MAP':
            stack.append(_Map())
        elif name == 'BUILD_LIST':
            items = stack[-arg:] if arg else []
            if arg:
                del stack[-arg:]
            stack.append(_List(items))
        elif name == 'BUILD_TUPLE':
            items = stack[-arg:] if arg else []
            if arg:
                del stack[-arg:]
            stack.append('(' + ', '.join(_src(x) for x in items) + (',' if arg == 1 else '') + ')')
        elif name == 'DUP_TOP':
            stack.append(stack[-1])
        elif name == 'ROT_TWO':
            stack[-1], stack[-2] = stack[-2], stack[-1]
        elif name == 'STORE_SUBSCR':
            # 2.2 dict display: DUP_TOP; <value>; ROT_TWO; <key>; STORE_SUBSCR
            key = stack.pop()
            obj = stack.pop()
            val = stack.pop()
            if not isinstance(obj, _Map):
                raise LitModError('STORE_SUBSCR on %r' % (obj,))
            obj.items.append((key, val))
        elif name == 'STORE_NAME':
            val = stack.pop()
            if isinstance(val, _Import):
                lines.append('import %s' % val.name)
            else:
                lines.append('%s = %s' % (names[arg], _src(val)))
        elif name == 'IMPORT_NAME':
            stack.pop()
            stack.append(_Import(names[arg]))
        elif name == 'POP_TOP':
            stack.pop()
        elif name == 'RETURN_VALUE':
            stack.pop()
        else:
            raise LitModError('unsupported opcode %s' % name)
    if stack:
        raise LitModError('stack not empty at end: %r' % (stack[:3],))
    return '# restored by tools/pyc/litmod.py (flat literal module)\n' + '\n'.join(lines) + '\n'


if __name__ == '__main__':
    sys.exit('use tools/pyc/decompyle_base.py (this module is a fallback emitter)')
