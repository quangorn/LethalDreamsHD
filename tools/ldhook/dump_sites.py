#!/usr/bin/env python3
"""Дизасм сайтов боевого слоя по EXE (read-only, без игры).

Нужен для проверки байтов новых хуков: каждый сайт обязан быть 5-байтовым
`E8 rel32` на ожидаемый лист (проверка при установке — leaf_site_patch),
а пролог функции — сигнатура фиксированной длины (write_jmp).

    python3 tools/ldhook/dump_sites.py [--exe PATH] --from 0x45dd00 --to 0x45dd70
    python3 tools/ldhook/dump_sites.py --exe PATH --va 0x45dd00 --n 40

EXE по умолчанию — установленный; baseline (`backup/LETHALDREAMS.EXE.pre4`)
задаётся через --exe.
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402

VA_BASE = 0x400000


def main():
    argv = sys.argv[1:]
    exe = None
    va = None
    n = 64
    lo, hi = None, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--exe":
            exe = argv[i + 1]
            i += 2
        elif a == "--va":
            va = int(argv[i + 1], 16)
            i += 2
        elif a == "--n":
            n = int(argv[i + 1])
            i += 2
        elif a == "--from":
            lo = int(argv[i + 1], 16)
            i += 2
        elif a == "--to":
            hi = int(argv[i + 1], 16)
            i += 2
        else:
            print("usage: dump_sites.py [--exe PATH] [--va VA --n N] | [--from VA --to VA]")
            return 2
    if exe is None:
        exe = paths.game_path("LETHALDREAMS.EXE")
    data = open(exe, "rb").read()
    print("exe=%s size=%d" % (paths.norm(exe), len(data)))

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    if lo is not None and hi is not None:
        blob = data[lo - VA_BASE:hi - VA_BASE]
        for ins in md.disasm(blob, lo):
            tgt = ""
            if ins.mnemonic == "call" and ins.size == 5 and ins.bytes[0] == 0xE8:
                tgt = "  -> 0x%x" % (ins.address + 5 + int.from_bytes(
                    ins.bytes[1:5], "little", signed=True))
            print("%08x: %-16s %s %s%s" % (
                ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str, tgt))
        return 0
    if va is None:
        print("нужен --va/--n или --from/--to")
        return 2
    blob = data[va - VA_BASE:va - VA_BASE + n]
    for ins in md.disasm(blob, va):
        tgt = ""
        if ins.mnemonic == "call" and ins.size == 5 and ins.bytes[0] == 0xE8:
            tgt = "  -> 0x%x" % (ins.address + 5 + int.from_bytes(
                ins.bytes[1:5], "little", signed=True))
        print("%08x: %-16s %s %s%s" % (
            ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str, tgt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
