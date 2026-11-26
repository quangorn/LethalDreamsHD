#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Снапшот дерева установки: путь<TAB>размер<TAB>mtime_ns<TAB>md5 построчно.

    python3 tools/snap_tree.py <root> <out.txt>

Сравнение двух снапшотов — обычный diff: строки отсортированы по пути.
Служебные файлы прогона (hdlog.txt, marker) в снапшот входят: их появление
после запуска игры — ожидаемый runtime-эффект, см. docs/VERIFY.md.
"""
import hashlib
import os
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: snap_tree.py <root> <out.txt>")
    root, out = os.path.abspath(sys.argv[1]), sys.argv[2]
    rows = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            p = os.path.join(dirpath, name)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            try:
                st = os.stat(p)
                with open(p, "rb") as fh:
                    h = hashlib.md5()
                    for chunk in iter(lambda: fh.read(1 << 20), b""):
                        h.update(chunk)
                rows.append("%s\t%d\t%d\t%s" % (rel, st.st_size, st.st_mtime_ns,
                                                h.hexdigest()))
            except OSError as e:
                rows.append("%s\tERR\t%s" % (rel, e))
    with open(out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(rows) + "\n")
    print("%s: %d files" % (out, len(rows)))


main()
