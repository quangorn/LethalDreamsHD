#!/usr/bin/env python3
"""pyc_text.py — нормализация текста декомпилята (косметика, не семантика).

Три преобразования приводят вывод decompyle 2.3.2 к стилю прежней базы; ни одно
не меняет байткод (каждое проверено гейтом «ядро опкодов» в
`tools/pyc/decompyle_base.py`):

  merge_imports(text)     `import a` + `import b` -> `import a, b`
  minimize_parens(text)   `x = (((a + b) + c) + d)` -> `x = a + b + c + d`
  unwrap_literals(text)   многострочный литерал -> одна строка
"""
import ast
import re

IMPORT_LINE = re.compile(r'^([ \t]*)import[ \t]+([A-Za-z_][\w.]*(?:[ \t]*,[ \t]*[A-Za-z_][\w.]*)*)[ \t]*$')
ASSIGN_LINE = re.compile(r'^([ \t]*)([A-Za-z_]\w*)[ \t]*=[ \t]*(.+)$')
OPEN_BR = '([{'
CLOSE_BR = ')]}'


def merge_imports(text):
    """Склеивает подряд идущие `import a` в `import a, b, c` (как печатал uncompyle6)."""
    out = []
    for line in text.split('\n'):
        m = IMPORT_LINE.match(line)
        if m and out:
            prev = IMPORT_LINE.match(out[-1])
            if prev and prev.group(1) == m.group(1):
                out[-1] = out[-1] + ', ' + m.group(2)
                continue
        out.append(line)
    return '\n'.join(out)


def _concat_only(node):
    if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Add):
        return _concat_only(node.left) and _concat_only(node.right)
    return isinstance(node, (ast.Constant, ast.Name, ast.Attribute, ast.Subscript, ast.Call))


def minimize_parens(text):
    """Снимает лесенку скобок в длинных склейках строк (иначе парсер 2.2 падает)."""
    out = []
    for line in text.split('\n'):
        if line.count('(') >= 16:
            m = ASSIGN_LINE.match(line)
            if m:
                try:
                    node = ast.parse(m.group(3), mode='eval').body
                except SyntaxError:
                    node = None
                if node is not None and isinstance(node, ast.BinOp) and _concat_only(node):
                    try:
                        line = m.group(1) + m.group(2) + ' = ' + ast.unparse(node)
                    except Exception:  # noqa: BLE001 — unparse не умеет узел
                        pass
        out.append(line)
    return '\n'.join(out)


def unwrap_literals(text):
    """Схлопывает переносы внутри скобок, не заходя в строки и комментарии."""
    out = []
    depth = 0
    triple = None
    ins = None
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if triple:
            if text.startswith(triple, i):
                out.append(triple)
                i += 3
                triple = None
                continue
            out.append(c)
            i += 1
            continue
        if ins:
            if c == '\\':
                out.append(text[i:i + 2])
                i += 2
                continue
            if c == ins:
                ins = None
            out.append(c)
            i += 1
            continue
        if text.startswith('"""', i) or text.startswith("'''", i):
            triple = text[i:i + 3]
            out.append(triple)
            i += 3
            continue
        if c == "'" or c == '"':
            ins = c
            out.append(c)
            i += 1
            continue
        if c == '#':
            j = text.find('\n', i)
            if j < 0:
                out.append(text[i:])
                break
            out.append(text[i:j])
            i = j
            continue
        if c in OPEN_BR:
            depth += 1
            out.append(c)
            i += 1
            continue
        if c in CLOSE_BR:
            depth -= 1
            out.append(c)
            i += 1
            continue
        if c == '\n' and depth > 0:
            j = i + 1
            while j < n and text[j] in ' \t':
                j += 1
            out.append(' ')
            i = j
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def normalize(text):
    return unwrap_literals(minimize_parens(merge_imports(text)))
