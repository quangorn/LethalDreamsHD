#!/usr/bin/env python3
"""Сборка ldhook.pyd (расширение Python 2.2.3, MSVC x86).

Источники (Фаза 0 каркаса боевого порта, PLAN_BATTLE_AI_CPP.md п. 0.1):
  tools/ldhook/ldhook.cpp      — init, Python-API, логгер, PE/.text-скан,
                                 установка хуков (install_site*), общие утилиты;
  tools/ldhook/sites_hd.cpp    — site1/site2/site3 (naked-хуки, site3_fp);
  tools/ldhook/battle/*.cpp    — structs.h, native.cpp (обёртки по VA),
                                 grid.cpp, path.cpp, target.cpp, move.cpp, tick.cpp.
Объявления: ldhook_internal.h, battle/battle.h, battle/structs.h, battle/native.h.

Требует:
  - vendor/py223/include/ — python3 tools/ldhook/fetch_headers.py;
  - vendor/py223/DLLs/python22.lib — import-lib, часть vendor/py223/;
  - Visual Studio / Build Tools с C++ — ищется через vswhere.

Выход: build/ldhook/ldhook.pyd (gitignored-артефакт).
LDHOOK_B0_BODY=1 — armed-сборка тела B0 (`/DB0_BODY_COMPLETE=1`) →
build/ldhook/ldhook_armed.pyd; откат = пересборка без флага.
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

# Порядок: главный TU (init + Python-API) + сайты HD + боевые слои.
# Пути через win_path: abspath под Cygwin даёт /cygdrive/..., cl понимает C:/...
SOURCES = [paths.win_path(s) for s in (
    os.path.join(_HERE, "ldhook.cpp"),
    os.path.join(_HERE, "sites_hd.cpp"),
    os.path.join(_HERE, "battle", "native.cpp"),
    os.path.join(_HERE, "battle", "grid.cpp"),
    os.path.join(_HERE, "battle", "path.cpp"),
    os.path.join(_HERE, "battle", "target.cpp"),
    os.path.join(_HERE, "battle", "move.cpp"),
    os.path.join(_HERE, "battle", "tick.cpp"),
    os.path.join(_HERE, "battle", "trace.cpp"),
    os.path.join(_HERE, "battle", "capture.cpp"),
    os.path.join(_HERE, "battle", "leaf_sites.cpp"),
    os.path.join(_HERE, "battle", "native_sites.cpp"),
)]


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
    missing = [s for s in SOURCES if not os.path.isfile(s)]
    if missing:
        sys.exit("нет источников: %s" % ", ".join(missing))
    if not os.path.isfile(VSWHERE):
        sys.exit("нет vswhere: %s" % VSWHERE)
    vs = vs_install_path()
    vcvars = os.path.join(vs, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    if not os.path.isfile(vcvars):
        sys.exit("нет vcvarsall.bat: %s" % vcvars)

    os.makedirs(os.path.dirname(OUTDIR), exist_ok=True)
    # Взвод Фазы 5 — флаг сборки, не правка исходника: LDHOOK_B0_BODY=1
    # даёт /DB0_BODY_COMPLETE=1 и отдельное имя артефакта (откат = пересборка
    # без флага; рабочий ldhook.pyd остаётся нетронутым). Любое другое значение
    # (пусто, true, 2) — жёсткая ошибка: не «разоружено» и не «взведено».
    raw = os.environ.get("LDHOOK_B0_BODY", "0")
    if raw not in ("0", "1"):
        sys.exit("LDHOOK_B0_BODY должен быть 0 или 1; получено: %r" % raw)
    armed = raw == "1"
    name = "ldhook_armed.pyd" if armed else "ldhook.pyd"
    out = os.path.join(OUTDIR, name)
    define = " /DB0_BODY_COMPLETE=1" if armed else ""
    if armed:
        print("ARMED: B0_BODY_COMPLETE=1 -> %s" % out)
    bat = os.path.join(OUTDIR, "build.bat" if not armed else "build_armed.bat")
    win = lambda p: p.replace("/", "\\")  # noqa: E731
    srcs = " ".join('"%s"' % win(s) for s in SOURCES)
    body = [
        "@echo off",
        'call "%s" x86' % win(vcvars),
        'cd /d "%s"' % win(OUTDIR),
        # /fp:precise — сохранить порядок FP-операций (совпадает с x87-порядком
        # cave1; без contraction/reassociation, как требует побитовая identity 4:3).
        # /EHsc — модель обработки исключений для C++ (cl определяет C++ по .cpp,
        # /TP не нужен).
        # /D_CRT_SECURE_NO_WARNINGS — тот же флаг, что в харнесс-сборках
        # (battle_replay_build.py, bdd0_harness_build.py): убирает C4996
        # на getenv/fopen/strncat.
        # /DB0_BODY_COMPLETE вставляется в командную строку cl (пустая строка
        # при default — bat побайтоно тот же, что до флага).
        'cl /nologo /LD /MT /EHsc /O2 /W3 /fp:precise /D_CRT_SECURE_NO_WARNINGS '
        '/I"%s"%s %s /link /LIBPATH:"%s" python22.lib /OUT:%s'
        % (win(INCLUDE), define, srcs, win(LIBDIR), name),
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