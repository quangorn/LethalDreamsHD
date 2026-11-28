#!/usr/bin/env python3
"""Индекс снапшота установки: путь + size + crc32 каждого файла.

Каноническая команда (из корня репозитория):
    python3 tools/release/snapshot_index.py
Пишет `release/snapshot_index.json` — коммитный эталон состояния установки:
`--untouched` классифицирует им побитовые пары после PRISTINE. Читает установку
только; в git идут относительные пути, личные пути не пишутся.

Смена состояния установки (принятый накат) — перегнать индекс и закоммитить:
индекс описывает принятое состояние, он не обновляется молча.
"""
import json
import os
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))
import paths  # noqa: E402

OUT = paths.snapshot_index_path()
ALLOWLIST = str(Path(__file__).resolve().parent.parent.parent / "release" / "allowlist.json")


def crc32_of(path):
    crc = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            crc = zlib.crc32(chunk, crc)
    return "%08x" % (crc & 0xFFFFFFFF)


def build(install, skip):
    entries = []
    for r, dirs, files in os.walk(install):
        dirs[:] = [d for d in dirs if d != "__pycache__"]
        for f in files:
            full = os.path.join(r, f)
            rel = os.path.relpath(full, install).replace(os.sep, "/").lower()
            if rel in skip:
                continue  # артефакты прогонов пересоздаются, состояние не фиксируется
            entries.append({"path": rel, "size": os.stat(full).st_size,
                            "crc": crc32_of(full)})
    entries.sort(key=lambda e: e["path"])
    return entries


def main():
    install = paths.install_dir()
    skip = set(x.lower() for x in json.load(open(ALLOWLIST, encoding="utf-8"))
               .get("dev_artifacts", []))
    entries = build(install, skip)
    doc = {
        "meta": {
            "note": "recorded clean state of the install: path (relative to "
                    "install_dir, lower case) + size + crc32; the checker "
                    "--untouched matches install files against it after "
                    "PRISTINE. Regenerate after any accepted install change.",
            "file_count": len(entries),
            "generated": time.strftime("%Y-%m-%d"),
        },
        "entries": entries,
    }
    tmp = OUT + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=1, separators=(",", ":"))
    os.replace(tmp, OUT)
    print("snapshot index: %d файлов -> %s" % (len(entries), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
