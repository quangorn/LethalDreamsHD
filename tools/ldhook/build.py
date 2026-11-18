#!/usr/bin/env python3
"""Сборка ldhook.pyd (расширение Python 2.2.3, MSVC x86) — tools/ldhook/ldhook.c.

Требует:
  - vendor/py223/include/ — python3 tools/ldhook/fetch_headers.py;
  - vendor/py223/DLLs/python22.lib — import-lib, часть vendor/py223/;
  - Visual Studio / Build Tools с C++ — ищется через vswhere.

Выход: build/ldhook/ldhook.pyd (gitignored-артефакт).
Запуск — через .bat (Cygwin манглит кавычки/«/»-флаги при передаче в cmd,
поэтому команда cl пишется в файл и исполняется native cmd).
"""
import os
import subprocess
import sys

# tools/ldhook -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

INCLUDE = paths.win_path(os.path.join(_ROOT, "vendor", "py223", "include"))
LIBDIR = paths.win_path(os.path.join(_ROOT, "vendor", "py223", "DLLs"))
OUTDIR = paths.win_path(os.path.join(_ROOT, "build", "ldhook"))
SRC = paths.win_path(os.path.join(_ROOT, "tools", "ldhook", "ldhook.c"))

VSWHERE = paths.win_path(os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio", "Installer", "vswhere.exe"))


def vs_install_path():
    p = subprocess.run(
        [VSWHERE, "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True)
    out = p.stdout.decode("utf-8", "replace").strip()
    if p.returncode != 0 or not out:
        sys.exit("vswhere failed: %s" % out)
    return out


def decode(data):
    for enc in ("utf-8", "cp866", "cp1251"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", "replace")


def main():
    if not os.path.isfile(os.path.join(INCLUDE, "Python.h")):
        sys.exit("нет заголовков Python.h: python3 tools/ldhook/fetch_headers.py")
    if not os.path.isfile(os.path.join(LIBDIR, "python22.lib")):
        sys.exit("нет python22.lib: %s" % LIBDIR)
    if not os.path.isfile(SRC):
        sys.exit("нет источника: %s" % SRC)
    if not os.path.isfile(VSWHERE):
        sys.exit("нет vswhere: %s" % VSWHERE)
    vs = vs_install_path()
    vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    if not os.path.isfile(vcvars):
        sys.exit("нет vcvarsall.bat: %s" % vcvars)

    os.makedirs(os.path.dirname(OUTDIR), exist_ok=True)
    out = os.path.join(OUTDIR, "ldhook.pyd")
    bat = os.path.join(OUTDIR, "build.bat")
    win = lambda p: p.replace("/", "\\")  # noqa: E731
    body = [
        "@echo off",
        'call "%s" x86' % win(vcvars),
        'cd /d "%s"' % win(OUTDIR),
        # /fp:precise — сохранить порядок FP-операций (совпадает с x87-порядком
        # cave1; без contraction/reassociation, как требует побитовая identity 4:3).
        'cl /nologo /LD /MT /O2 /W3 /fp:precise /I"%s" "%s" '
        '/link /LIBPATH:"%s" python22.lib /OUT:ldhook.pyd'
        % (win(INCLUDE), win(SRC), win(LIBDIR)),
        "exit /b %errorlevel%",
        "",
    ]
    with open(bat, "w", encoding="ascii", newline="\r\n") as fh:
        fh.write("\n".join(body))
    print("bat: %s" % bat)
    p = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
    text = decode(p.stdout) + decode(p.stderr)
    print(text)
    if p.returncode != 0:
        sys.exit("cl failed rc=%d" % p.returncode)
    if not os.path.isfile(out):
        sys.exit("артефакт не создан: %s" % out)
    print("OK: %s (%d bytes)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()