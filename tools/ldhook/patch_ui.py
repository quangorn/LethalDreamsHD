#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Каноническая установка загрузчика ldhook.pyd в игру (docs/archive/HANDOFF_ldhook.md, B.2).
Зачем: `.pyd` живёт только если его кто-то загрузит. Загрузчик — блок
`imp.load_dynamic('ldhook', ...)` в `data/scripts/ui/ui.pyc`: игра исполняет
этот модуль на старте. Скрипт собирает `ui.pyc` воркером 2.2.3, кладёт
`ldhook.pyd` рядом с EXE и умеет откатить обе правки.

  PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/patch_ui.py --build
  PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/patch_ui.py --install
  PYTHONDONTWRITEBYTECODE=1 python3 tools/ldhook/patch_ui.py --restore

EXE не трогается: хуки ставятся в памяти (VirtualProtect) при загрузке .pyd.
Откат = вернуть `ui/ui.pyc` из бэкапа и удалить `ldhook.pyd`.

--install также проверяет AppCompat-флаг `~ HIGHDPIAWARE` для EXE установки
(без него игра не влезает в 4K) и восстанавливает его, если он слетел.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, "tools", "pyc"))
sys.path.insert(0, _HERE)
import paths  # noqa: E402
import pycbuild  # noqa: E402
import spike  # noqa: E402  (SPIKE_BLOCK — тот же загрузчик, что в dev-спайке)

PYD = os.path.join(_ROOT, "build", "ldhook", "ldhook.pyd")
UI_SRC = os.path.join(_ROOT, "patches", "ui", "ui.py")
UI_REL = ("data", "scripts", "ui", "ui.pyc")
PYD_NAME = "ldhook.pyd"


def md5(path):
    with open(path, "rb") as fh:
        return hashlib.md5(fh.read()).hexdigest()


def work_dir():
    d = paths.scratch_dir("ldhook")
    if not os.path.isdir(d):
        os.makedirs(d)
    return d


DPIAWARE_KEY = r"HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"


def ensure_dpi_flag(root):
    """~ HIGHDPIAWARE для EXE установки: проверить и восстановить, если слетел.

    Флаг живёт в HKCU\\...\\AppCompatFlags\\Layers под полным путём EXE. Он
    теряется (чистка Windows, обновление, переустановка) и без него игра не
    влезает в 4K — поэтому установка обязана его проверять. Существующее
    значение не затирается: HIGHDPIAWARE дописывается к уже имеющимся флагам.
    """
    exe = os.path.join(root, "LETHALDREAMS.EXE")
    if not os.path.isfile(exe):
        print("dpi: %s нет — флаг не проверялся" % exe)
        return None

    # Имя значения в реестре — Windows-путь с обратными слэшами; paths.win_path
    # отдаёт прямые, поэтому нормализуем (иначе создаётся лишнее значение).
    name = paths.win_path(exe).replace("/", "\\")
    q = subprocess.run(["reg", "query", DPIAWARE_KEY, "/v", name],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = q.stdout.decode("latin-1", "replace") if hasattr(q.stdout, "decode") else ""
    cur = ""
    if q.returncode == 0:
        m = re.search(r"REG_SZ\s+(.*)$", text, re.M)
        if m:
            cur = m.group(1).strip()
        if "HIGHDPIAWARE" in cur.split():
            print("dpi: %s ~ HIGHDPIAWARE (есть)" % name)
            return True

    flags = [f for f in cur.split() if f]
    if not flags:
        flags = ["~"]
    flags.append("HIGHDPIAWARE")
    value = " ".join(flags)
    a = subprocess.run(["reg", "add", DPIAWARE_KEY, "/v", name, "/t", "REG_SZ",
                        "/d", value, "/f"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if a.returncode != 0:
        print("dpi: НЕ удалось выставить флаг (rc=%d): %s"
              % (a.returncode, a.stdout.decode("latin-1", "replace").strip()))
        return False
    print("dpi: %s ~ HIGHDPIAWARE (восстановлен, было %r)" % (name, cur))
    return True



def ui_pyc_path(root):
    return os.path.join(root, *UI_REL)


def build(out_path):
    """patches/ui/ui.py + блок загрузчика -> out_path (воркер 2.2.3).

    Header mtime прибивается к `pyc_source_mtime` модуля ui/ui из манифеста:
    иначе md5 уезжает от времени сборки (тело побитово одинаковое), и установку
    нельзя сверить с записанным эталоном. Игровой .py для ui/ui не существует,
    поэтому staleness-проверке движка эта дата безразлична.
    """
    text = open(UI_SRC, "rb").read().replace(b"\r\n", b"\n")
    tmp = os.path.join(work_dir(), "_ui_ldhook_src.py")
    with open(tmp, "wb") as fh:
        fh.write(text + spike.SPIKE_BLOCK.encode("utf-8"))
    out, rc = pycbuild.run_worker("compile", paths.win_path(tmp))
    if "COMPILE-OK" not in out:
        raise SystemExit("COMPILE FAIL: %s" % out.strip()[:400])
    if os.path.isfile(out_path):
        os.remove(out_path)
    out, rc = pycbuild.run_worker("build", paths.win_path(tmp),
                                  paths.win_path(out_path))
    if "BUILD-OK" not in out:
        raise SystemExit("BUILD FAIL: %s" % out.strip()[:400])
    os.remove(tmp)
    stamp = module_source_mtime("ui/ui")
    if stamp is not None:
        d = bytearray(open(out_path, "rb").read())
        d[4:8] = struct.pack("<i", stamp)
        with open(out_path, "wb") as fh:
            fh.write(bytes(d))
    print("ui.pyc -> %s (%d B, md5 %s)" % (out_path, os.path.getsize(out_path),
                                           md5(out_path)))
    return out_path


def artifact():
    return os.path.join(_ROOT, "build", "ldhook", "ui.pyc")


def clean_ui_md5():
    """Эталон ui/ui.pyc (base-печать манифеста) — то, что обязано стоять без
    загрузчика. Нужен для проверки бэкапа: его снимает install() с текущего
    файла, и если там уже был загрузчик (повторный --install), бэкап не чистый."""
    e = manifest_entry("ui/ui")
    return e.get("md5") if e else None


def module_source_mtime(module):
    e = manifest_entry(module)
    return e.get("pyc_source_mtime") if e else None


def manifest_entry(module):
    """Запись code tier манифеста (тот же контракт, что у install_patch)."""
    p = os.path.join(_ROOT, "release", "manifest.json")
    try:
        with open(p, encoding="utf-8") as fh:
            m = json.load(fh)
    except (OSError, ValueError):
        return None
    for e in m.get("code_tier", {}).get("entries", []):
        if e.get("module") == module:
            return e
    return None


def install():
    root = paths.install_dir()
    if not os.path.isdir(root):
        raise SystemExit("нет каталога установки: %s" % root)
    if not os.path.isfile(PYD):
        raise SystemExit("нет артефакта: %s (сначала tools/ldhook/build.py)" % PYD)
    pyc = artifact()
    # загрузчик пересобирается из текущего patches/ui/ui.py на каждом --install:
    # иначе в игру уезжает вчерашний artifact, а md5 установки расходится с кодом
    build(pyc)

    bak = os.path.join(work_dir(), "ui_ui.pyc.pre")
    dst_pyc = ui_pyc_path(root)
    dst_pyd = os.path.join(root, PYD_NAME)
    clean = clean_ui_md5()
    bak_ok = os.path.isfile(bak) and clean and md5(bak) == clean
    if bak_ok:
        # повторный --install: бэкап снят с чистой установки, не затираем
        print("бэкап уже есть: %s (md5 %s == эталон) — не перезаписываю" % (bak, md5(bak)))
    else:
        # НЕ чистый бэкап (снят с уже пропатченного файла) — не затираем его
        # самим собой: чистый файл собирается каноническим пайплайном.
        shutil.copyfile(dst_pyc, bak + ".prev")
        shutil.copyfile(os.path.join(_ROOT, "patches", "ui", "ui.pyc"), bak)
        print("бэкап ui/ui.pyc -> %s (md5 %s; прежний — %s.prev, md5 %s)"
              % (bak, md5(bak), os.path.basename(bak),
                 md5(bak + ".prev") if os.path.isfile(bak + ".prev") else "-"))
        if clean and md5(bak) != clean:
            print("warning: бэкап %s != эталон манифеста %s (соберите"
                  " patches/ui/ui.pyc: python3 tools/apply_patch.py ui/ui)" % (md5(bak), clean))

    if os.path.isfile(dst_pyd) and md5(dst_pyd) != md5(PYD):
        os.remove(dst_pyd)
    shutil.copyfile(PYD, dst_pyd)
    shutil.copyfile(pyc, dst_pyc)
    print("install: %s (md5 %s)" % (dst_pyd, md5(dst_pyd)))
    print("install: %s (md5 %s)" % (dst_pyc, md5(dst_pyc)))
    ensure_dpi_flag(root)
    print("")
    print("запуск: LDHOOK_CAP=32 -> игра (0 = хук выключен, чистый A/B)")
    print("лог:    <каталог игры>/hdlog.txt")
    print("откат:  python3 tools/ldhook/patch_ui.py --restore")


def restore():
    root = paths.install_dir()
    bak = os.path.join(work_dir(), "ui_ui.pyc.pre")
    dst_pyc = ui_pyc_path(root)
    dst_pyd = os.path.join(root, PYD_NAME)
    clean = clean_ui_md5()
    if os.path.isfile(bak) and (clean is None or md5(bak) == clean):
        shutil.copyfile(bak, dst_pyc)
        print("restore: %s (md5 %s)" % (dst_pyc, md5(dst_pyc)))
    else:
        # Бэкап не эталонный (или его нет): чистый файл даёт только канонический
        # пайплайн (base + release/patches/ui_ui.patch), копия бэкапа — нет.
        print("restore: бэкап %s %s эталону %s — чистая сборка каноническим путём"
              % (bak, "не равен" if os.path.isfile(bak) else "отсутствует", clean))
        src = os.path.join(_ROOT, "patches", "ui", "ui.pyc")
        if not os.path.isfile(src) or (clean and md5(src) != clean):
            raise SystemExit("нет чистого patches/ui/ui.pyc: python3 tools/apply_patch.py ui/ui")
        shutil.copyfile(src, dst_pyc)
        print("restore: %s (md5 %s == эталон)" % (dst_pyc, md5(dst_pyc)))
    if os.path.isfile(dst_pyd):
        os.remove(dst_pyd)
        print("restore: удалён %s" % dst_pyd)
    for extra in ("hdlog.txt", "hdlog.txt.1", "ldhook_marker.txt"):
        p = os.path.join(root, extra)
        if os.path.isfile(p):
            print("note: лог остался: %s (не удаляю)" % p)


def main():
    args = sys.argv[1:]
    if "--build" in args:
        build(artifact())
    elif "--install" in args:
        install()
    elif "--restore" in args:
        restore()
    else:
        raise SystemExit(__doc__.strip())


if __name__ == "__main__":
    main()
