#!/usr/bin/env python3
"""Сборка offline self-test хуков call-сайтов: build/ldhook/leaf_sites_selftest.exe.

Линкуется НАСТОЯЩИЙ модуль хуков (tools/ldhook/battle/leaf_sites.cpp) + тест
(tools/probes/leaf_sites_selftest.cpp). Игра и native-слой не нужны: заглушки
листьев имеют те же ABI (__stdcall `ret 4`, float в ST(0), результат в AL), а
сайт — такой же 5-байтовый `call rel32`.

Запуск: python3 tools/probes/leaf_sites_selftest_build.py
"""
import os
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

OUTDIR = paths.win_path(os.path.join(_ROOT, "build", "ldhook"))
SOURCES = [os.path.join(_HERE, "leaf_sites_selftest.cpp"),
           os.path.join(_ROOT, "tools", "ldhook", "battle", "leaf_sites.cpp")]

VSWHERE = paths.win_path(os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio", "Installer", "vswhere.exe"))

CFLAGS = "/nologo /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS"


def decode(data):
    for enc in ("utf-8", "cp866", "cp1251"):
        try:
            return data.decode(enc)
        except UnicodeDecodeError:
            continue
    return data.decode("utf-8", "replace")


def main():
    if not os.path.isfile(VSWHERE):
        sys.exit("нет vswhere: %s" % VSWHERE)
    p = subprocess.run(
        [VSWHERE, "-latest", "-products", "*",
         "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"], capture_output=True)
    vs = p.stdout.decode("utf-8", "replace").strip()
    if p.returncode != 0 or not vs:
        sys.exit("vswhere failed: %s" % vs)
    vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    if not os.path.isfile(vcvars):
        sys.exit("нет vcvarsall.bat: %s" % vcvars)

    os.makedirs(os.path.dirname(OUTDIR), exist_ok=True)
    win = lambda x: x.replace("/", "\\")  # noqa: E731
    out = os.path.join(OUTDIR, "leaf_sites_selftest.exe")
    bat = os.path.join(OUTDIR, "build_leaf_sites_selftest.bat")
    srcs = " ".join('"%s"' % win(paths.win_path(s)) for s in SOURCES)
    body = [
        "@echo off",
        'call "%s" x86' % win(vcvars),
        'cd /d "%s"' % win(OUTDIR),
        'cl %s %s /Fe:%s'
        % (CFLAGS, srcs, win(os.path.basename(out))),
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
