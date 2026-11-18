#!/usr/bin/env python3
"""Скачать официальный Python 2.2.3 (python.org) и извлечь заголовки для сборки
расширений (ldhook.pyd, tools/ldhook/build.py).

Кладёт в vendor/py223/include/ (untracked, как весь vendor/py223/):
  - Include/*.h из архива;
  - PC/pyconfig.h (Windows-конфиг).
Идемпотентно: если vendor/py223/include/Python.h уже есть — ничего не делает
(флаг --force перекачивает).
"""
import io
import os
import sys
import tarfile
import urllib.request

URL = "https://www.python.org/ftp/python/2.2.3/Python-2.2.3.tgz"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DEST = os.path.join(ROOT, "vendor", "py223", "include")


def main():
    force = "--force" in sys.argv[1:]
    if os.path.isfile(os.path.join(DEST, "Python.h")) and not force:
        print("headers already present: %s" % DEST)
        return
    print("downloading %s ..." % URL)
    with urllib.request.urlopen(URL, timeout=60) as resp:
        data = resp.read()
    print("downloaded %d bytes" % len(data))
    os.makedirs(DEST, exist_ok=True)
    count = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tf:
        for m in tf.getmembers():
            if not m.isfile():
                continue
            name = m.name
            keep = None
            if name.startswith("Python-2.2.3/Include/") and name.endswith(".h"):
                keep = name.rsplit("/", 1)[1]
            elif name == "Python-2.2.3/PC/pyconfig.h":
                keep = "pyconfig.h"
            if keep:
                src = tf.extractfile(m)
                with open(os.path.join(DEST, keep), "wb") as fh:
                    fh.write(src.read())
                count += 1
    print("extracted %d headers -> %s" % (count, DEST))
    if not os.path.isfile(os.path.join(DEST, "Python.h")):
        print("ERROR: Python.h not extracted", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()