#!/usr/bin/env python3
"""Обновление записи code_tier в release/manifest.json по готовому patches/<module>.py.

Канонический порядок при правке игрового модуля:

  1. правится `patches/<module>.py` (база `decompiled_fixed/<module>.py` + правка);
  2. `python3 tools/release/update_entry.py <module> [--note "..."] [--mtime N]`
     — пишет `release/patches/<module>.patch` (diff база -> patches) и пересобирает
     запись манифеста: `py_md5`/`py_size`, `md5`/`size`/`crc32` .pyc, `pyc_magic`,
     `pyc_source_mtime` (по умолчанию сохраняется прежний), `state`;
  3. `python3 tools/apply_patch.py <module>` — гейт: .py md5 == манифест и
     .pyc md5 == манифест (канонический путь репозитория);
  4. `python3 tools/pyc/install_patch.py <module> --install` — запись в игру
     (только явным --install, AGENTS.md).

`installed_recorded_md5` НЕ трогается: это md5 нетронутой копии установки,
по нему checker отличает дрейф от base-печати.
"""
import argparse
import difflib
import hashlib
import json
import os
import struct
import sys
import zlib

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, _ROOT)
sys.path.insert(0, os.path.join(_ROOT, "tools", "pyc"))
import paths  # noqa: E402
import install_patch  # noqa: E402

MANIFEST = os.path.join(_ROOT, "release", "manifest.json")


def md5_bytes(data):
    return hashlib.md5(data).hexdigest()


def crc32_bytes(data):
    return format(zlib.crc32(data) & 0xFFFFFFFF, "08x")


def entry_for(man, module):
    for e in man["code_tier"]["entries"]:
        if e.get("module") == module:
            return e
    raise SystemExit("в манифесте нет записи module=%s" % module)


def main():
    ap = argparse.ArgumentParser(description="rebuild release/patches/<m>.patch and the manifest entry")
    ap.add_argument("module")
    ap.add_argument("--note", default=None, help="новый текст поля note (по умолчанию прежний)")
    ap.add_argument("--mtime", type=int, default=None, help="pyc_source_mtime (по умолчанию прежний)")
    ap.add_argument("--no-patch", action="store_true", help="не перезаписывать .patch (только манифест)")
    a = ap.parse_args()
    module = a.module

    with open(MANIFEST, encoding="utf-8") as fh:
        man = json.load(fh)
    e = entry_for(man, module)

    base_path = os.path.join(_ROOT, e["base"])
    py_path = os.path.join(_ROOT, e["repo_py"])
    if not os.path.exists(base_path):
        raise SystemExit("нет базы %s (декомпилят: tools/pyc/decompile_lit.py %s)" % (e["base"], module))
    if not os.path.exists(py_path):
        raise SystemExit("нет %s" % e["repo_py"])

    with open(base_path, encoding="utf-8") as fh:
        base_lines = fh.read().split("\n")
    with open(py_path, encoding="utf-8") as fh:
        new_text = fh.read()
    new_lines = new_text.split("\n")

    patch_rel = e["patch_file"]
    patch_path = os.path.join(_ROOT, patch_rel)
    if not a.no_patch:
        diff = list(difflib.unified_diff(
            base_lines, new_lines,
            fromfile="a/%s.py" % module, tofile="b/%s.py" % module, lineterm="", n=3))
        body = "\n".join(diff) + "\n"
        with open(patch_path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(body)
        print("patch: %s (%d B, %d строк)" % (patch_rel, len(body.encode("utf-8")), len(diff)))

    py_bytes = new_text.encode("utf-8")
    e["py_md5"] = md5_bytes(py_bytes)
    e["py_size"] = len(py_bytes)

    if a.mtime is not None:
        e["pyc_source_mtime"] = a.mtime
    mtime = e["pyc_source_mtime"]

    pyc = install_patch.build_module(module)
    install_patch.fix_header(pyc, module)
    os.utime(py_path, (mtime, mtime))
    data = open(pyc, "rb").read()
    d = bytearray(data)
    d[4:8] = struct.pack("<i", mtime)
    with open(pyc, "wb") as fh:
        fh.write(bytes(d))
    data = bytes(d)

    e["md5"] = md5_bytes(data)
    e["size"] = len(data)
    e["crc32"] = crc32_bytes(data)
    e["pyc_magic"] = data[:4].hex()
    e["state"] = "MATCH"
    if a.note is not None:
        e["note"] = a.note

    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(man, fh, indent=1, ensure_ascii=False)
        fh.write("\n")

    print("module %s" % module)
    print("  %s: md5 %s, %d B" % (e["repo_py"], e["py_md5"], e["py_size"]))
    print("  %s: md5 %s, %d B, crc32 %s, magic %s, mtime %d"
          % (e["repo_pyc"], e["md5"], e["size"], e["crc32"], e["pyc_magic"], mtime))
    print("  state %s, installed_recorded_md5 %s (не тронут)" % (e["state"], e["installed_recorded_md5"]))
    print("  note: %s" % e["note"])
    return 0


if __name__ == "__main__":
    sys.exit(main())