#!/usr/bin/env python3
"""Сборка офлайн-харнесса боевого тика: build/ldhook/bdd0_harness.exe.

Флаги — те же, что tools/probes/battle_replay_build.py (MSVC x86, /O2,
/fp:precise, макрос BATTLE_REPLAY). Линкуется тело порта battle/tick.cpp под
BATTLE_REPLAY; нативный слой игры НЕ линкуется — вместо battle/native.cpp
собирается replay-слой tools/probes/bdd0/native_stubs.cpp +
native_replay.cpp (20 no-op обёрток + 10 листьев из записанного потока).

Дополнительно собирается генератор синтетического захвата bdd0_synth.exe:
то же тело, но логические листья обслуживает модель
(tools/probes/bdd0/native_model.cpp) и пишут поток bdd0_leaf — так
синтетический сценарий остаётся самосогласованным.

Запуск: python3 tools/probes/bdd0_harness_build.py
"""
import os
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

OUTDIR = paths.win_path(os.path.join(_ROOT, "build", "ldhook"))
_COMMON = [
    os.path.join(_HERE, "bdd0", "registry.cpp"),
    os.path.join(_HERE, "bdd0", "checks_builtin.cpp"),
    os.path.join(_HERE, "bdd0", "arena.cpp"),
    os.path.join(_HERE, "bdd0", "native_stubs.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "tick.cpp"),
]
HARNESS = [os.path.join(_HERE, "bdd0_harness.cpp"),
           os.path.join(_HERE, "bdd0", "scenario.cpp"),
           os.path.join(_HERE, "bdd0", "impl_registry.cpp"),
           os.path.join(_HERE, "bdd0", "native_replay.cpp")] + _COMMON
SYNTH = [os.path.join(_HERE, "bdd0_synth.cpp"),
         os.path.join(_HERE, "bdd0", "native_model.cpp")] + _COMMON

VSWHERE = paths.win_path(os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio", "Installer", "vswhere.exe"))

# B0_BODY_COMPLETE: офлайн тело тика взведено. Макрос управляет только
# установкой detour'а H1 в игре (b0_port_enabled), а не содержимым тела R1..R22.
CFLAGS = ("/nologo /MT /EHsc /O2 /W3 /fp:precise /DBATTLE_REPLAY "
          "/D_CRT_SECURE_NO_WARNINGS /DB0_BODY_COMPLETE=1")


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

    def build(name, sources):
        out = os.path.join(OUTDIR, name + ".exe")
        bat = os.path.join(OUTDIR, "build_%s.bat" % name)
        srcs = " ".join('"%s"' % win(paths.win_path(s)) for s in sources)
        body = [
            "@echo off",
            'call "%s" x86' % win(vcvars),
            'cd /d "%s"' % win(OUTDIR),
            "cl %s %s /link /OUT:%s.exe" % (CFLAGS, srcs, name),
            "exit /b %errorlevel%",
            "",
        ]
        with open(bat, "w", encoding="ascii", newline="\r\n") as fh:
            fh.write("\n".join(body))
        proc = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
        print(decode(proc.stdout) + decode(proc.stderr))
        if proc.returncode != 0:
            sys.exit("%s: cl failed rc=%d" % (name, proc.returncode))
        if not os.path.isfile(out):
            sys.exit("артефакт не создан: %s" % out)
        print("OK: %s (%d bytes)" % (out, os.path.getsize(out)))

    build("bdd0_harness", HARNESS)
    build("bdd0_synth", SYNTH)


if __name__ == "__main__":
    main()
