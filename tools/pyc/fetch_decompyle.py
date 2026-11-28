#!/usr/bin/env python3
"""fetch_decompyle.py — скачать и разложить decompyle 2.3.2 в `vendor/decompyle`.

decompyle 2.3.2 (Hartmut Goebel, MIT) — декомпилятор Python-байткода 1.5–2.3,
ровно нашей «эпохи» (игра: Python 2.2.3, magic `0xED2D`, marshal v1). Это
основной декомпилятор базы `decompiled_fixed/` (`tools/pyc/decompyle_base.py`).

Каталог `vendor/decompyle/` — untracked (как `vendor/py223/`): третий-партийный
код не коммитится, а восстанавливается этой командой. Скачивание — один tar.gz
с SourceForge, распаковка нативными средствами Python (tarfile), затем одна
правка: в `decompyle/marshal_files.py` версия 2.2 берёт нативный `marshal`
интерпретатора 2.2.3 (C-модуль `marshal_22` в дистрибутиве не собран, а формат
marshal v1 у родного `.pyc` игры тот же).

Команды (из корня репозитория):
  python3 tools/pyc/fetch_decompyle.py            скачать/разложить в vendor/decompyle
  python3 tools/pyc/fetch_decompyle.py --force    перекачать поверх
  python3 tools/pyc/fetch_decompyle.py --check    только проверить готовность
"""
import os
import shutil
import sys
import tarfile
import tempfile
import urllib.request

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

VERSION = "2.3.2"
VENDOR = os.path.join(_ROOT, "vendor", "decompyle")
MARKER = os.path.join(VENDOR, "DECOMPYLE_VERSION")
URLS = [
    "https://downloads.sourceforge.net/project/decompyle/decompyle/"
    "Decompyle%202/decompyle_2.3.2.tar.gz",
    "https://downloads.sourceforge.net/project/decompyle/decompyle/"
    "Decompyle%202/decompyle_2.3.2.orig.tar.gz",
]
SHA_TARGETS = ("decompyle/__init__.py", "decompyle/marshal_files.py",
               "decompyle/Walker.py", "decompyle/spark.py", "scripts/decompyle")


def vendor_ready():
    if not os.path.isdir(VENDOR):
        return False
    for rel in SHA_TARGETS:
        if not os.path.isfile(os.path.join(VENDOR, rel)):
            return False
    return True


def check():
    ok = vendor_ready()
    if not ok:
        print("vendor/decompyle: НЕ ГОТОВ -> python3 tools/pyc/fetch_decompyle.py")
        return 1
    got = ""
    if os.path.isfile(MARKER):
        with open(MARKER) as fh:
            got = fh.read().strip()
    mf = os.path.join(VENDOR, "decompyle", "marshal_files.py")
    with open(mf) as fh:
        patched = "    '2.2': 'marshal'," in fh.read()
    print("vendor/decompyle: decompyle %s" % (got or "2.3.2 (без маркера)"))
    print("  marshal 2.2 -> native: %s" % ("да" if patched else "НЕТ (перекачать)"))
    return 0 if patched else 1


def download(force):
    if vendor_ready() and not force:
        print("vendor/decompyle уже есть (--force чтобы перекачать)")
        return check()
    tmp = tempfile.mkdtemp(prefix="decompyle_")
    tgz = os.path.join(tmp, "decompyle.tar.gz")
    last = None
    for url in URLS:
        try:
            print("download: %s" % url)
            with urllib.request.urlopen(url, timeout=120) as resp, open(tgz, "wb") as out:
                shutil.copyfileobj(resp, out)
            with open(tgz, "rb") as fh:
                head = fh.read(2)
            if head != b"\x1f\x8b":
                raise RuntimeError("ответ не gzip (вероятно, страница-заглушка)")
            break
        except Exception as exc:  # noqa: BLE001 — пробуем следующий URL
            last = exc
            print("  failed: %s" % exc)
    else:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit("не удалось скачать decompyle %s: %s" % (VERSION, last))

    unpack = os.path.join(tmp, "x")
    with tarfile.open(tgz, "r:gz") as tar:
        tar.extractall(unpack)
    roots = [d for d in os.listdir(unpack) if os.path.isdir(os.path.join(unpack, d))]
    if len(roots) != 1:
        shutil.rmtree(tmp, ignore_errors=True)
        raise SystemExit("в архиве ожидался один корневой каталог, найдено: %s" % roots)
    src = os.path.join(unpack, roots[0])

    if os.path.isdir(VENDOR):
        shutil.rmtree(VENDOR)
    shutil.copytree(src, VENDOR)
    shutil.rmtree(tmp, ignore_errors=True)

    patch_marshal(VENDOR)
    with open(os.path.join(VENDOR, "DECOMPYLE_VERSION"), "w") as fh:
        fh.write(VERSION + "\n")
    print("vendor/decompyle: decompyle %s разложен" % VERSION)
    return check()


def patch_marshal(vendor):
    """2.2 -> нативный marshal 2.2.3 (в дистрибутиве C-модуль `marshal_22` не собран)."""
    mf = os.path.join(vendor, "decompyle", "marshal_files.py")
    with open(mf) as fh:
        text = fh.read()
    old = "    '2.2': 'marshal_22',"
    if old not in text:
        if "    '2.2': 'marshal'," in text:
            return
        raise SystemExit("marshal_files.py: ожидаемая строка не найдена")
    with open(mf, "w") as fh:
        fh.write(text.replace(old, "    '2.2': 'marshal',"))


def main(argv):
    if "--check" in argv:
        return check()
    return download("--force" in argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
