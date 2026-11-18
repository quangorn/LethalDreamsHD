#!/usr/bin/env python3
"""Фаза 4 (PLAN_proxy_dll.md §4): аудит трёх сайтов хуков на диске — без игры.

Сканирует `.text` EXE ровно так, как это делает `ldhook.pyd` (границы секции из
PE-заголовков, только `VirtualSize`; zero-run пещеры `0x4ebf12..0x4ec000` лежит
ЗА `VirtualSize` и в скан не попадает). Для каждой сигнатуры `exepatch12.py`
печатает число вхождений и VA.

Файлы по умолчанию: установленный EXE (`config[game.install_dir]`) и baseline
(`backup/LETHALDREAMS.EXE.pre4`, `LD_BAK`). Вердикты:

  installable  — все 3 сигнатуры по одному разу в `.text` → in-memory хуки
                 (`ldhook.pyd`) встанут (baseline `45e93d46…`);
  patched      — сигнатур нет, на сайтах статические `E9 rel32` → это cave-EXE
                 `exepatch12` (`171319bb…`); `ldhook.pyd` штатно ничего не
                 ставит (no-op), игра работает на cave;
  UNEXPECTED   — иначе: новая правка EXE сдвинула/унесла сайты (rc 1).

    python3 tools/ldhook/check_sites.py
    python3 tools/ldhook/check_sites.py --exe <путь к EXE> [--exe ...]
"""
import hashlib
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

BAK = os.environ.get("LD_BAK") or (paths.norm(_ROOT) + "/backup/LETHALDREAMS.EXE.pre4")

SITES = (
    # имя, сигнатура, ожидаемый VA в baseline
    ("site1", bytes.fromhex("c641580159"), 0x41CE31),
    ("site2", bytes.fromhex("898194020000"), 0x40B494),
    ("site3", bytes.fromhex("e8eb1bffff"), 0x47DD80),
)

MD5_BASELINE = "45e93d46aad6ceb3dfc17e86d04e4f82"
MD5_INSTALL = "171319bbc170cf6e57c8d36a8ae5f50d"


def parse_text(data):
    """(ImageBase, .text VA, .text VirtualSize, .text raw offset, raw size)."""
    if data[:2] != b"MZ":
        raise SystemExit("не PE-файл (нет MZ)")
    e = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e:e + 4] != b"PE\0\0":
        raise SystemExit("не PE-файл (нет PE-подписи)")
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    optsz = struct.unpack_from("<H", data, e + 20)[0]
    base = struct.unpack_from("<I", data, e + 24 + 28)[0]
    off = e + 24 + optsz
    for _ in range(nsec):
        name = data[off:off + 8].rstrip(b"\0")
        if name == b".text":
            vsize, va, rsize, raw = struct.unpack_from("<IIII", data, off + 8)
            if vsize == 0:
                vsize = rsize
            return base, va, vsize, raw, rsize
        off += 40
    raise SystemExit("в PE нет секции .text")


def scan(data):
    base, va, vsize, raw, rsize = parse_text(data)
    text = data[raw:raw + rsize][:vsize]
    found = []
    for name, sig, want in SITES:
        pos = []
        i = text.find(sig)
        while i >= 0:
            pos.append(base + va + i)
            i = text.find(sig, i + 1)
        found.append((name, sig, want, pos))
    return base, va, vsize, raw, found


def bytes_at_va(data, base, text_va, raw, want, n):
    delta = want - (base + text_va)
    if delta < 0:
        return b""
    return data[raw + delta:raw + delta + n]


def audit(path):
    data = open(path, "rb").read()
    md5 = hashlib.md5(data).hexdigest()
    base, va, vsize, raw, found = scan(data)
    print("%s" % path)
    print("  md5=%s size=%d .text base=0x%08x VA=0x%08x VirtualSize=0x%x"
          % (md5, len(data), base, base + va, vsize))

    counts = [len(pos) for (_n, _s, _w, pos) in found]
    for name, sig, want, pos in found:
        occ = ", ".join("0x%08x" % p for p in pos) if pos else "-"
        note = ""
        if pos and pos != [want]:
            note = " (ожидался 0x%08x)" % want
        print("  %-5s %-12s count=%d  at %s%s"
              % (name, sig.hex(), len(pos), occ, note))

    if all(c == 1 for c in counts):
        verdict = "installable"
        print("  VERDICT: installable — in-memory хуки ldhook.pyd встанут")
    elif all(c == 0 for c in counts):
        firsts = [bytes_at_va(data, base, va, raw, want, 1)
                  for (_n, _s, want, _p) in found]
        if all(b == b"\xe9" for b in firsts):
            verdict = "patched"
            print("  VERDICT: patched — статические E9 на всех трёх сайтах "
                  "(cave exepatch12); ldhook.pyd штатно no-op")
        else:
            verdict = "UNEXPECTED"
            print("  VERDICT: UNEXPECTED — сигнатур нет, но сайты не E9: "
                  "неизвестная сборка")
    else:
        verdict = "UNEXPECTED"
        print("  VERDICT: UNEXPECTED — сигнатуры не уникальны "
              "(или пропала часть), скан нестабилен")
    print("")
    return md5, verdict


def main():
    argv = sys.argv[1:]
    exes = []
    i = 0
    while i < len(argv):
        if argv[i] == "--exe" and i + 1 < len(argv):
            exes.append(argv[i + 1])
            i += 2
        else:
            raise SystemExit("usage: check_sites.py [--exe PATH ...]\n"
                             "  --exe задаёт файлы; по умолчанию установка + baseline")
    if not exes:
        exes = [paths.game_path("LETHALDREAMS.EXE"), BAK]

    ok = True
    for path in exes:
        if not os.path.isfile(path):
            print("MISSING %s" % path)
            ok = False
            continue
        md5, verdict = audit(path)
        if md5 == MD5_BASELINE and verdict != "installable":
            print("  ! baseline обязан быть installable")
            ok = False
        if md5 == MD5_INSTALL and verdict != "patched":
            print("  ! установленный cave-EXE обязан быть patched")
            ok = False
        if verdict == "UNEXPECTED":
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
