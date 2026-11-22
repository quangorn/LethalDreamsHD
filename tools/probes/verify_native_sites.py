#!/usr/bin/env python3
"""Static integrity check for the generated native-site table.

For every direct RECORD site: the bytes must be a 5-byte `E8 rel32` whose
target equals the table's target VA.  For every indirect RECORD site: the
instruction must be exactly a 3-byte `FF /2 disp8` (`call r/m32`, memory form)
with the table's vtable slot displacement.

This is the pre-install half of the patch-integrity guarantee; the post-install
half (site E8 -> runtime stub, stub tail-call -> original) is printed by the
offline emitter self-test (tools/probes/leaf_sites_selftest.cpp).

Usage: python3 tools/probes/verify_native_sites.py [--samples N]
"""
import os
import re
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402

VA_BASE = 0x400000
INC = os.path.join(_ROOT, "tools", "ldhook", "battle", "native_sites_table.inc")
ROW = re.compile(r"\{\s*0x([0-9a-f]+)u?,\s*0x([0-9a-f]+)u?,\s*(\d+),\s*"
                 r"(\d+),\s*(\d+),\s*(NS_K_\w+)\s*\}")


def main():
    rows = []
    for line in open(INC, "r", encoding="ascii"):
        m = ROW.search(line)
        if m:
            rows.append((int(m.group(1), 16), int(m.group(2), 16),
                         int(m.group(3)), int(m.group(4)), int(m.group(5)),
                         m.group(6)))
    if not rows:
        sys.exit("no rows parsed from %s" % INC)
    data = open(paths.game_path("LETHALDREAMS.EXE"), "rb").read()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    bad = 0
    dsites = [r for r in rows if r[2] == 0]
    isites = [r for r in rows if r[2] != 0]
    samples = []
    for site, tgt, slot, nargs, retn, kind in dsites:
        off = site - VA_BASE
        b = data[off:off + 5]
        got = VA_BASE + off + 5 + int.from_bytes(b[1:5], "little", signed=True)
        if b[0] != 0xE8 or got != tgt:
            bad += 1
            print("BAD direct site 0x%x: bytes %s -> 0x%x (expect 0x%x)"
                  % (site, b.hex(), got, tgt))
        elif len(samples) < 5:
            samples.append((site, tgt, b))
    for site, tgt, slot, nargs, retn, kind in isites:
        off = site - VA_BASE
        b = data[off:off + 8]
        want = {1: 0x28, 2: 0x2c, 3: 0x38, 4: 0x40}[slot]
        got = -1
        ok = False
        for ins in md.disasm(b, site):
            if (ins.mnemonic == "call" and ins.size == 3 and
                    ins.bytes[0] == 0xFF and
                    ((ins.bytes[1] >> 3) & 7) == 2):
                op = ins.operands[0]
                if op.mem.base != 0 and op.mem.disp == want:
                    got = op.mem.disp
                    ok = True
            break
        if not ok:
            bad += 1
            print("BAD indirect site 0x%x: bytes %s (want 3-byte FF /2 "
                  "disp8=0x%x)" % (site, b[:6].hex(), want))
    print("native site static check: %d direct + %d indirect, bad=%d"
          % (len(dsites), len(isites), bad))
    n = 5
    for i, a in enumerate(sys.argv):
        if a == "--samples" and i + 1 < len(sys.argv):
            n = int(sys.argv[i + 1])
    print("sample direct sites (disassembly):")
    for site, tgt, b in samples[:n]:
        blob = data[site - VA_BASE:site - VA_BASE + 5]
        for ins in md.disasm(blob, site):
            print("  %08x: %-16s %s -> 0x%x"
                  % (ins.address, ins.bytes.hex(), ins.mnemonic + " " +
                     ins.op_str, tgt))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
