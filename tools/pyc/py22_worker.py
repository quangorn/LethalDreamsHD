#!/usr/bin/env python
"""Рабочий процесс на Python 2.2.3 (PYTHON.EXE из py223.dir в config.toml) для
офлайн-проверки декомпилированных .py движком Lethal Dreams. Файл лежит в tools/pyc/,
вызывается pycbuild.py (cwd=tools/pyc, пути передаются в Windows-форме).

Запуск: PYTHONPATH = py223-каталог + cache/lib (кэш Lib из snapshot-архива,
`python3 tools/extract_pristine.py lib`); игровой Lib в путь компиляции не входит
(импорт не пишет .pyc в установку игры).
        PYTHON.EXE py22_worker.py <mode> <args...>

Режимы (argv[1] = mode):
  compile <path.py>            компилировать текст .py; печатает OK или SYNTAX
  build   <path.py> <out.pyc>  .py -> .pyc магией 2.2.3 (движок прочтёт)
  dis-py  <path.py>            dis компилята .py
  dis-pyc <path.pyc>           dis оригинального .pyc (магия+mtime = 8 байт)

Вывод dis нормализуется вызывающей стороной (py3): offset-числа и SET_LINENO вырезаются.
"""
import sys, os, marshal, struct, imp, time, dis


class Capture:
    def __init__(self):
        self.buf = []

    def write(self, s):
        self.buf.append(s)

    def flush(self):
        pass


def read(path):
    # текст исходника: compile() у 2.2.3 падает на CRLF, нормализуем переводы строк
    f = open(path, 'rb')
    d = f.read()
    f.close()
    return d.replace('\r\n', '\n')


def read_bin(path):
    # бинарные данные (.pyc): переводы строк не трогать
    f = open(path, 'rb')
    d = f.read()
    f.close()
    return d


def code_tree(code, acc):
    acc.append(code)
    for x in code.co_consts:
        if hasattr(x, 'co_name'):
            code_tree(x, acc)


def signature(code):
    # имя, аргументы, локальные, флаги, stacksize + имена и константы (с пометкой кодов)
    consts = []
    for x in code.co_consts:
        if hasattr(x, 'co_name'):
            consts.append('<code>')
        else:
            consts.append(repr(x))
    return 'SIGNATURE %s args=%d nlocals=%d flags=%d stack=%d names=%s consts=%s' % (
        code.co_name, code.co_argcount, code.co_nlocals, code.co_flags,
        code.co_stacksize, ' '.join(code.co_names), ' '.join(consts))


def dump_dis(code):
    old = sys.stdout
    cap = Capture()
    sys.stdout = cap
    dis.dis(code)
    sys.stdout = old
    text = ''.join(cap.buf)
    for line in text.split('\n'):
        sys.stdout.write(line + '\n')


def mode_compile(path):
    src = read(path)
    try:
        compile(src, path, 'exec')
        print 'COMPILE-OK %s' % path
    except SyntaxError, e:
        print 'SYNTAX %s line=%s msg=%s' % (path, e.lineno, e.msg)


def mode_build(path, out):
    src = read(path)
    code = compile(src, path, 'exec')
    magic = imp.get_magic()
    f = open(out, 'wb')
    f.write(magic)
    f.write(struct.pack('i', int(time.time())))
    f.write(marshal.dumps(code))
    f.close()
    size = os.path.getsize(out)
    # магия 4 байта: 0x2D 0xED + \r\n; старший байт у 2.2.3 = 0xED
    magic16 = ord(magic[1]) << 8 | ord(magic[0])
    print 'BUILD-OK %s -> %s (%d bytes, magic 0x%04X)' % (path, out, size, magic16)


def dump_py(path):
    src = read(path)
    try:
        code = compile(src, path, 'exec')
    except SyntaxError, e:
        print 'SYNTAX %s line=%s msg=%s' % (path, e.lineno, e.msg)
        return
    acc = []
    code_tree(code, acc)
    print '### FILE %s objects=%d' % (path, len(acc))
    for c in acc:
        print '### CODE %s' % c.co_name
        print signature(c)
        dump_dis(c)


def dump_pyc(path):
    d = read_bin(path)
    magic = struct.unpack('<H', d[:2])[0]
    if magic != 60717:
        print 'MAGIC-MISMATCH %s got 0x%04X' % (path, magic)
        return
    code = marshal.loads(d[8:])
    acc = []
    code_tree(code, acc)
    print '### PYC %s objects=%d' % (path, len(acc))
    for c in acc:
        print '### CODE %s' % c.co_name
        print signature(c)
        dump_dis(c)


def mode_consts(path):
    """Для каждого кодового объекта: имя и consts (значения), чтобы py3 восстановил
    литералы, где uncompyle6 записал ИНДЕКСЫ consts вместо значений."""
    d = read_bin(path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', path
        return
    code = marshal.loads(d[8:])
    acc = []
    code_tree(code, acc)
    for c in acc:
        vals = []
        for x in c.co_consts:
            if hasattr(x, 'co_name'):
                vals.append('<code>')
            else:
                vals.append(repr(x))
        print 'CONSTS %s %d %s' % (c.co_name, len(vals), ' ; '.join(vals))


def mode_lit(path):
    """Восстановление литералов из оригинального .pyc.

    uncompyle6 для Python 2.2 местами пишет ИНДЕКСЫ co_consts вместо значений
    (пример: cur_table = [[1, 2, 2, 3, 3], ...] вместо [['cursor_arrow.tga', 32, 32, 2, 2], ...]).
    Здесь проходим по байткоду, ведём стек значений; чистая литеральная последовательность
    (LOAD_CONST / BUILD_LIST / BUILD_TUPLE), завершаемая STORE_NAME/STORE_ATTR, даёт
    точный литерал оригинала. Печатаем: LIT <codeobj> <имя> <repr>
    """
    d = read_bin(path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', path
        return
    code = marshal.loads(d[8:])
    acc = []
    code_tree(code, acc)
    opname = dis.opname
    HAVE = dis.HAVE_ARGUMENT
    LOAD_CONST = opname.index('LOAD_CONST')
    BUILD_LIST = opname.index('BUILD_LIST')
    BUILD_TUPLE = opname.index('BUILD_TUPLE')
    STORE_NAME = opname.index('STORE_NAME')
    STORE_ATTR = opname.index('STORE_ATTR')
    STORE_GLOBAL = opname.index('STORE_GLOBAL')
    for c in acc:
        stack = []
        i = 0
        bc = c.co_code
        while i < len(bc):
            op = ord(bc[i])
            if op < HAVE:
                arg = None
                i += 1
            else:
                arg = ord(bc[i + 1]) | (ord(bc[i + 2]) << 8)
                i += 3
            if op == LOAD_CONST:
                v = c.co_consts[arg]
                if hasattr(v, 'co_name'):
                    stack = []
                else:
                    stack.append(v)
            elif op == BUILD_LIST:
                if len(stack) >= arg:
                    items = stack[-arg:]
                    stack = stack[:-arg]
                    stack.append(items)
                else:
                    stack = []
            elif op == BUILD_TUPLE:
                if len(stack) >= arg:
                    items = stack[-arg:]
                    stack = stack[:-arg]
                    stack.append(tuple(items))
                else:
                    stack = []
            elif op in (STORE_NAME, STORE_ATTR, STORE_GLOBAL):
                # вершина стека - литерал; для STORE_ATTR ниже лежит self, оно не мешает
                if len(stack) >= 1:
                    v = stack[-1]
                    print 'LIT %s %s %s' % (c.co_name, c.co_names[arg], repr(v))
                stack = []
            elif op not in (opname.index('SET_LINENO'),):
                stack = []


def hexstr(s):
    out = ''
    i = 0
    while i < len(s):
        out += '%02x' % ord(s[i])
        i += 1
    return out


def enc_code(code):
    """Кодовый объект -> список токенов 'OP resolved', где аргумент разрешён к значению
    (константа/имя), чтобы сравнивать компилят .py с оригинальным .pyc по смыслу."""
    res = []
    i = 0
    bc = code.co_code
    while i < len(bc):
        op = ord(bc[i])
        if op < dis.HAVE_ARGUMENT:
            res.append(dis.opname[op])
            i += 1
            continue
        arg = ord(bc[i + 1]) | (ord(bc[i + 2]) << 8)
        i += 3
        name = dis.opname[op]
        if name == 'SET_LINENO':
            continue  # номера строк не сравниваем
        if name == 'LOAD_CONST':
            if arg < len(code.co_consts):
                v = code.co_consts[arg]
                if hasattr(v, 'co_name'):
                    res.append('LOAD_CONST <code>')
                else:
                    res.append('LOAD_CONST HEX %s' % hexstr(repr(v)))
            else:
                res.append('LOAD_CONST ?%d' % arg)
        elif name in ('LOAD_NAME', 'LOAD_GLOBAL', 'LOAD_FAST', 'LOAD_ATTR', 'STORE_NAME',
                      'STORE_ATTR', 'STORE_GLOBAL', 'STORE_SUBSCR', 'DELETE_NAME',
                      'DELETE_GLOBAL', 'LOAD_GLOBAL'):
            if arg < len(code.co_names):
                res.append('%s %s' % (name, code.co_names[arg]))
            else:
                res.append('%s ?%d' % (name, arg))
        else:
            res.append('%s %d' % (name, arg))
    return res


def mode_vdiff(pyc_path, py_path):
    """Кодовые объекты оригинального .pyc и компилята .py, в порядке обхода дерева.
    Вывод: TREE A|B idx name, затем TOKEN A|B idx i token."""
    d = read_bin(pyc_path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', pyc_path
        return
    a = []
    code_tree(marshal.loads(d[8:]), a)
    b = []
    code_tree(compile(read(py_path), py_path, 'exec'), b)
    for side, lst in (('A', a), ('B', b)):
        idx = 0
        for c in lst:
            print 'TREE %s %d %s' % (side, idx, c.co_name)
            for ki in range(len(c.co_consts)):
                k = c.co_consts[ki]
                if hasattr(k, 'co_name'):
                    print 'CONSTS %s %d %d <code>' % (side, idx, ki)
                else:
                    print 'CONSTS %s %d %d %s' % (side, idx, ki, hexstr(repr(k)))
            j = 0
            for t in enc_code(c):
                print 'TOKEN %s %d %d %s' % (side, idx, j, t)
                j += 1
            idx += 1


def mode_fnames(pyc_path, py_path):
    """Имена всех кодовых объектов (функции/классы/лямбды) в .pyc и в компиляте .py.
    Вывод: FNAMES A|B имя, по одному на строку (порядок обхода дерева)."""
    d = read_bin(pyc_path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', pyc_path
        return
    a = []
    code_tree(marshal.loads(d[8:]), a)
    b = []
    code_tree(compile(read(py_path), py_path, 'exec'), b)
    for side, lst in (('A', a), ('B', b)):
        for c in lst:
            print 'FNAMES %s %s' % (side, c.co_name)


def mode_litfix(pyc_path, py_path):
    """Для восстановления литералов: A - токены оригинального .pyc (без SET_LINENO),
    B - токены компилята .py, где SET_LINENO сохранён как 'SLINE <строка>', чтобы
    по индексу токена B найти строку исходника, в которой заменять целое."""
    d = read_bin(pyc_path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', pyc_path
        return
    a = []
    code_tree(marshal.loads(d[8:]), a)
    b = []
    code_tree(compile(read(py_path), py_path, 'exec'), b)
    for side, lst in (('A', a), ('B', b)):
        idx = 0
        for c in lst:
            j = 0
            i = 0
            bc = c.co_code
            while i < len(bc):
                op = ord(bc[i])
                if op < dis.HAVE_ARGUMENT:
                    if side == 'B' and dis.opname[op] == 'SET_LINENO':
                        pass
                    i += 1
                    continue
                arg = ord(bc[i + 1]) | (ord(bc[i + 2]) << 8)
                i += 3
                name = dis.opname[op]
                if name == 'SET_LINENO':
                    if side == 'B':
                        print 'TOKEN B %d %d SLINE %d' % (idx, j, arg)
                        j += 1
                    continue
                if name == 'LOAD_CONST':
                    if arg < len(c.co_consts):
                        v = c.co_consts[arg]
                        if hasattr(v, 'co_name'):
                            tok = 'LOAD_CONST <code>'
                        else:
                            tok = 'LOAD_CONST HEX %s' % hexstr(repr(v))
                    else:
                        tok = 'LOAD_CONST ?%d' % arg
                elif name in ('LOAD_NAME', 'LOAD_GLOBAL', 'LOAD_FAST', 'LOAD_ATTR', 'STORE_NAME',
                              'STORE_ATTR', 'STORE_GLOBAL', 'STORE_SUBSCR', 'DELETE_NAME',
                              'DELETE_GLOBAL'):
                    if arg < len(c.co_names):
                        tok = '%s %s' % (name, c.co_names[arg])
                    else:
                        tok = '%s ?%d' % (name, arg)
                else:
                    tok = '%s %d' % (name, arg)
                print 'TOKEN %s %d %d %s' % (side, idx, j, tok)
                j += 1
            idx += 1


def mode_litfix2(pyc_path, py_path):
    """Как litfix, но токены помечены индексом кодового объекта, плюс отдельными строками
    идут имя объекта и его константы. Позволяет ВЫРАВНИВАТЬ кодовые объекты по ИМЕНАМ
    (декомпилят может терять функции: дерево A длиннее дерева B, и выравнивание по
    индексу в litfix даёт полностью расходящиеся потоки токенов)."""
    d = read_bin(pyc_path)
    magic = ord(d[1]) << 8 | ord(d[0])
    if magic != 60717:
        print 'MAGIC-MISMATCH', pyc_path
        return
    a = []
    code_tree(marshal.loads(d[8:]), a)
    b = []
    code_tree(compile(read(py_path), py_path, 'exec'), b)
    for side, lst in (('A', a), ('B', b)):
        idx = 0
        for c in lst:
            print 'CODE %s %d %s' % (side, idx, c.co_name)
            for v in c.co_consts:
                if hasattr(v, 'co_name'):
                    print 'CONST %s %d <code>' % (side, idx)
                else:
                    print 'CONST %s %d %s' % (side, idx, hexstr(repr(v)))
            j = 0
            i = 0
            bc = c.co_code
            while i < len(bc):
                op = ord(bc[i])
                if op < dis.HAVE_ARGUMENT:
                    i += 1
                    continue
                arg = ord(bc[i + 1]) | (ord(bc[i + 2]) << 8)
                i += 3
                name = dis.opname[op]
                if name == 'SET_LINENO':
                    if side == 'B':
                        print 'SLINE %d %d %d' % (idx, j, arg)
                    continue
                if name == 'LOAD_CONST':
                    if arg < len(c.co_consts):
                        v = c.co_consts[arg]
                        if hasattr(v, 'co_name'):
                            tok = 'LOAD_CONST <code>'
                        else:
                            tok = 'LOAD_CONST HEX %s' % hexstr(repr(v))
                    else:
                        tok = 'LOAD_CONST ?%d' % arg
                elif name in ('LOAD_NAME', 'LOAD_GLOBAL', 'LOAD_FAST', 'LOAD_ATTR', 'STORE_NAME',
                              'STORE_ATTR', 'STORE_GLOBAL', 'STORE_SUBSCR', 'DELETE_NAME',
                              'DELETE_GLOBAL'):
                    if arg < len(c.co_names):
                        tok = '%s %s' % (name, c.co_names[arg])
                    else:
                        tok = '%s ?%d' % (name, arg)
                else:
                    tok = '%s %d' % (name, arg)
                print 'TOK %s %d %d %s' % (side, idx, j, tok)
                j += 1
            idx += 1


def main():
    mode = sys.argv[1]
    args = sys.argv[2:]
    if mode == 'compile':
        for p in args:
            mode_compile(p)
    elif mode == 'build':
        mode_build(args[0], args[1])
    elif mode == 'consts':
        mode_consts(args[0])
    elif mode == 'lit':
        mode_lit(args[0])
    elif mode == 'vdiff':
        mode_vdiff(args[0], args[1])
    elif mode == 'fnames':
        mode_fnames(args[0], args[1])
    elif mode == 'litfix':
        mode_litfix(args[0], args[1])
    elif mode == 'litfix2':
        mode_litfix2(args[0], args[1])
    elif mode == 'dis-py':
        for p in args:
            dump_py(p)
    elif mode == 'dis-pyc':
        for p in args:
            dump_pyc(p)
    else:
        print 'unknown mode', mode


main()
