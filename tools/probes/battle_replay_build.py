#!/usr/bin/env python3
"""Сборка tools/probes/battle_replay.exe (MSVC x86, флаги как tools/ldhook/build.py).

Линкует battle_replay.cpp + battle/{grid,path,target}.cpp с -DBATTLE_REPLAY
(точки подмены нативных листьев). python22.lib НЕ нужна: харнесс — консольная
программа, а не расширение Python. Выход — build/ldhook/battle_replay.exe.
"""
import os
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

OUTDIR = paths.win_path(os.path.join(_ROOT, "build", "ldhook"))
SOURCES = [paths.win_path(s) for s in (
    os.path.join(_HERE, "battle_replay.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "grid.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "path.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "target.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "move.cpp"),
)]
VSWHERE = paths.win_path(os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio", "Installer", "vswhere.exe"))


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
    out = os.path.join(OUTDIR, "battle_replay.exe")
    bat = os.path.join(OUTDIR, "build_replay.bat")
    win = lambda x: x.replace("/", "\\")  # noqa: E731
    srcs = " ".join('"%s"' % win(s) for s in SOURCES)
    body = [
        "@echo off",
        'call "%s" x86' % win(vcvars),
        'cd /d "%s"' % win(OUTDIR),
        'cl /nologo /MT /EHsc /O2 /W3 /fp:precise /DBATTLE_REPLAY '
        '/D_CRT_SECURE_NO_WARNINGS %s /link /OUT:battle_replay.exe'
        % srcs,
        "exit /b %errorlevel%",
        "",
    ]
    with open(bat, "w", encoding="ascii", newline="\r\n") as fh:
        fh.write("\n".join(body))
    proc = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
    text = decode(proc.stdout) + decode(proc.stderr)
    print(text)
    if proc.returncode != 0:
        sys.exit("cl failed rc=%d" % proc.returncode)
    if not os.path.isfile(out):
        sys.exit("артефакт не создан: %s" % out)
    print("OK: %s (%d bytes)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
