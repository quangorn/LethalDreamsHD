#!/usr/bin/env python3
"""Фаза 0-бис (PLAN_proxy_dll.md, §2A): спайк .pyd-пути.

1. Полная копия установки -> <scratch>/ldhook/tree (scratch.dir из config.toml,
   LD_CONFIG не нужен: читаем основной config).
2. build/ldhook/ldhook.pyd -> корень scratch-дерева (рядом с LETHALDREAMS.EXE).
3. data/scripts/ui/ui.pyc -> компилят patches/ui/ui.py + блок imp.load_dynamic
   (сборка воркером 2.2.3, как в pyc-пайплайне).
4. config.toml для scratch -> <scratch>/ldhook/config.toml (game.install_dir =
   scratch-дерево, остальные ключи — из основного config).

Запуск игры: tools/ldhook/probe_spike.ps1 (снимок ОКНА + гашение).
Гейт: <scratch>/ldhook/tree/ldhook_marker.txt существует (initldhook执行) и
окно игры отрисовалось.
"""
import os
import shutil
import subprocess
import sys

# tools/ldhook -> tools -> корень репозитория
_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, "tools", "pyc"))
import paths  # noqa: E402
import pycbuild  # noqa: E402

PYD = os.path.join(_ROOT, "build", "ldhook", "ldhook.pyd")
UI_SRC = os.path.join(_ROOT, "patches", "ui", "ui.py")

SPIKE_BLOCK = """

# --- ldhook spike (Фаза 0-бис, PLAN_proxy_dll.md §2A) ---
try:
    import imp, os, sys
    _ld_paths = [os.path.join(os.getcwd(), 'ldhook.pyd')]
    try:
        _ld_paths.append(os.path.join(os.path.dirname(os.path.abspath(sys.executable)), 'ldhook.pyd'))
    except Exception:
        pass
    try:
        _ld_paths.append(os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), 'ldhook.pyd'))
    except Exception:
        pass
    for _ld_p in _ld_paths:
        if os.path.isfile(_ld_p):
            imp.load_dynamic('ldhook', _ld_p)
            break
except Exception:
    pass
"""


def write_scratch_config(cfg, tree, cfg_path):
    g = cfg["game"]
    lines = ["[game]"]
    lines.append('install_dir      = "%s"' % paths.norm(tree))
    lines.append('pristine_archive = "%s"' % paths.norm(g["pristine_archive"]))
    lines.append('snapshot_archive = "%s"' % paths.norm(g["snapshot_archive"]))
    lines.append("")
    lines.append("[assets]")
    lines.append('hd_pack = "%s"' % paths.norm(cfg["assets"]["hd_pack"]))
    base = cfg.get("assets", {}).get("base")
    if base:
        lines.append('base    = "%s"' % paths.norm(base))
    lines.append("")
    lines.append("[py223]")
    lines.append('dir = "%s"' % paths.norm(cfg["py223"]["dir"]))
    lines.append("")
    lines.append("[scratch]")
    lines.append('dir = "%s"' % paths.norm(cfg["scratch"]["dir"]))
    lines.append("")
    lines.append("[cache]")
    lines.append('dir = "%s"' % paths.norm(cfg["cache"]["dir"]))
    excl = cfg.get("check", {}).get("exclude")
    if excl:
        lines.append("")
        lines.append("[check]")
        lines.append("exclude = [%s]" % ", ".join('"%s"' % e for e in excl))
    lines.append("")
    with open(cfg_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(lines))


def build_ui_pyc(tree):
    """patches/ui/ui.py + блок спайка -> <tree>/data/scripts/ui/ui.pyc (воркер 2.2.3)."""
    text = open(UI_SRC, "rb").read().replace(b"\r\n", b"\n")
    src = os.path.join(tree, "_ui_spike_src.py")
    with open(src, "wb") as fh:
        fh.write(text + SPIKE_BLOCK.encode("utf-8"))
    out, rc = pycbuild.run_worker("compile", paths.win_path(src))
    if "COMPILE-OK" not in out:
        raise SystemExit("COMPILE FAIL: %s" % out.strip()[:400])
    dst = os.path.join(tree, "data", "scripts", "ui", "ui.pyc")
    out, rc = pycbuild.run_worker("build", paths.win_path(src), paths.win_path(dst))
    if "BUILD-OK" not in out:
        raise SystemExit("BUILD FAIL: %s" % out.strip()[:400])
    os.remove(src)
    print("ui.pyc -> %s" % dst)
    print("  %s" % out.strip())


def set_hidpi_flag(exe_path):
    """AppCompat-флаг ~ HIGHDPIAWARE для scratch-EXE (как у установки): без него
    Windows масштабирует окно игры (175% -> 6720x3780 вместо 3840x2160).
    Cygwin-python без winreg — флаг ставим через PowerShell."""
    ps = ("New-Item -Path 'HKCU:\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\"
          "AppCompatFlags\\Layers' -Force | Out-Null; "
          "Set-ItemProperty -Path 'HKCU:\\SOFTWARE\\Microsoft\\Windows NT\\"
          "CurrentVersion\\AppCompatFlags\\Layers' -Name '%s' -Value '~ HIGHDPIAWARE'"
          % exe_path)
    p = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)
    if p.returncode != 0:
        raise SystemExit("не удалось поставить AppCompat-флаг: %s"
                         % p.stderr.decode("utf-8", "replace"))
    print("appcompat ~ HIGHDPIAWARE -> %s" % exe_path)


def main():
    cfg = paths.load()
    src_root = paths.install_dir()
    base = paths.scratch_dir("ldhook")
    tree = os.path.join(base, "tree")
    cfg_path = os.path.join(base, "config.toml")

    if not os.path.isfile(PYD):
        raise SystemExit("нет артефакта сборки: %s\nсначала: python3 tools/ldhook/build.py"
                         % PYD)
    if os.path.isdir(base):
        print("清理 scratch: %s" % base)
        shutil.rmtree(base)
    os.makedirs(base)

    print("копия установки: %s -> %s" % (src_root, tree))
    shutil.copytree(src_root, tree)
    shutil.copyfile(PYD, os.path.join(tree, "ldhook.pyd"))
    print("ldhook.pyd -> %s" % os.path.join(tree, "ldhook.pyd"))
    set_hidpi_flag(paths.win_path(os.path.join(tree, "LETHALDREAMS.EXE")))

    build_ui_pyc(tree)

    write_scratch_config(cfg, tree, cfg_path)
    print("config -> %s" % cfg_path)
    print("")
    print("gtest: powershell -NoProfile -ExecutionPolicy Bypass -File "
          "tools/ldhook/probe_spike.ps1 -GameRoot \"%s\"" % tree)
    print("гейт: %s должен появиться" % os.path.join(tree, "ldhook_marker.txt"))


if __name__ == "__main__":
    main()