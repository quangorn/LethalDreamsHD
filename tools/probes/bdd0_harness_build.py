#!/usr/bin/env python3
"""Сборка офлайн-харнесса боевого тика: build/ldhook/bdd0_harness.exe.

Флаги — те же, что tools/probes/battle_replay_build.py (MSVC x86, /O2,
/fp:precise, макрос BATTLE_REPLAY). Линкуется тело порта battle/tick.cpp под
BATTLE_REPLAY; нативный слой игры НЕ линкуется — вместо battle/native.cpp
собирается replay-слой tools/probes/bdd0/native_stubs.cpp (31 обёртка, из них
24 no-op) + native_replay.cpp (6 листьев и 14 RECORD-нативов из записанного
потока).

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
    os.path.join(_HERE, "bdd0", "arena.cpp"),
    # native_stubs.cpp обязателен обеим сборкам: tick.cpp зовёт msg/string/log
    # и pathfinder_dtor, grid.cpp — native::tree_lookup, b0_45eee0_dispatch —
    # s6_port_enabled/s6_unit_ai_tick. Линк SYNTH без него даёт LNK2019.
    os.path.join(_HERE, "bdd0", "native_stubs.cpp"),
    os.path.join(_HERE, "bdd0", "grid_replay.cpp"),
    os.path.join(_ROOT, "tools", "ldhook", "battle", "tick.cpp"),
    # grid.cpp: тело тика зовёт s6_grid_anchor_lookup (0x458050), тот —
    # s6_grid_key_lookup/s6_grid_chain_head/s6_grid_cell. g_replay_override и
    # replay_grid_cell (хуки grid.cpp под BATTLE_REPLAY) определены в
    # bdd0/grid_replay.cpp — реестровый battle_replay.cpp сюда не линкуется.
    os.path.join(_ROOT, "tools", "ldhook", "battle", "grid.cpp"),
]
# Реестр проверок и сами проверки судит только ядро харнесса; генератор
# (bdd0_synth.exe) их символов не зовёт.
HARNESS = [os.path.join(_HERE, "bdd0_harness.cpp"),
           os.path.join(_HERE, "bdd0", "scenario.cpp"),
           os.path.join(_HERE, "bdd0", "impl_registry.cpp"),
           os.path.join(_HERE, "bdd0", "native_replay.cpp"),
           os.path.join(_HERE, "bdd0", "path_diff.cpp"),
           os.path.join(_HERE, "bdd0", "registry.cpp"),
           os.path.join(_HERE, "bdd0", "checks_builtin.cpp")] + _COMMON
SYNTH = [os.path.join(_HERE, "bdd0_synth.cpp"),
         os.path.join(_HERE, "bdd0", "native_model.cpp")] + _COMMON

VSWHERE = paths.win_path(os.path.join(
    os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    "Microsoft Visual Studio", "Installer", "vswhere.exe"))

# B0_BODY_COMPLETE: офлайн тело тика взведено. Макрос управляет только
# установкой detour'а H1 в игре (b0_port_enabled), а не содержимым тела R1..R22.
CFLAGS = ("/nologo /MT /EHsc /O2 /W3 /fp:precise /DBATTLE_REPLAY "
          "/D_CRT_SECURE_NO_WARNINGS /DB0_BODY_COMPLETE=1")
# B0_TRACE_LABELS: цепочка меток/ветвлений тела (tick.cpp) + печать в харнессе.
# Только trace-сборка харнесса; обычная сборка (и in-game .pyd) без макроса.
TRACE_CFLAGS = CFLAGS + " /DB0_TRACE_LABELS=1"


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

    def build(name, sources, cflags=CFLAGS, link_extra=""):
        out = os.path.join(OUTDIR, name + ".exe")
        bat = os.path.join(OUTDIR, "build_%s.bat" % name)
        srcs = " ".join('"%s"' % win(paths.win_path(s)) for s in sources)
        body = [
            "@echo off",
            'call "%s" x86' % win(vcvars),
            'cd /d "%s"' % win(OUTDIR),
            "cl %s %s /link %s /OUT:%s.exe" % (cflags, srcs, link_extra, name),
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
    if "--trace" in sys.argv:
        build("bdd0_harness_trace", HARNESS, TRACE_CFLAGS)
    if "--trace-map" in sys.argv:
        build("bdd0_harness_trace_map", HARNESS, TRACE_CFLAGS,
              "/MAP:trace_map.map /BASE:0x400000")


if __name__ == "__main__":
    main()
