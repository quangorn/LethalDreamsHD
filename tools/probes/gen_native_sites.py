#!/usr/bin/env python3
"""Generate tools/ldhook/battle/native_sites_table.inc from LETHALDREAMS.EXE.

The body FUN_0044bdd0..0x44d08e has 234 `call` sites.  The 12 logic leaves stay
in leaf_sites.*: 32 of the body's sites are leaf calls, and the 2 remaining leaf
sites (0x458142, 0x45811a) live inside leaf 0x458050, outside the body.  Every
other site is classified here by its target VA:

  RECORD  -- return value or side effect feeds a branch or a state store;
  NOOP_OK -- GUI/string/log/dtor plumbing: cannot affect the body's flow.

Only RECORD sites are emitted into the hook table.  The target ABI table
(nargs / ret_n / result kind) is curated from battle/native.h and the listing;
the site list itself is generated from the bytes, so no site is hand-copied.

Indirect sites (`call *disp(%reg)`) cannot be patched as 5-byte `call rel32`
(they are 3 bytes).  They are handled by hooking the vtable slot function
(one stub per (vtable, slot)); the site rows only carry the slot id.

Canonical command: python3 tools/probes/gen_native_sites.py [--check]
  --check  verify the checked-in .inc is current (rc 1 if stale).
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402
from capstone.x86 import X86_OP_IMM, X86_OP_MEM  # noqa: E402

VA_BASE = 0x400000
BODY_START = 0x44bdd0
BODY_END = 0x44d08e
OUT = os.path.join(_ROOT, "tools", "ldhook", "battle", "native_sites_table.inc")

# 32 leaf call sites (keep bdd0_leaf; do not double-record as bdd0_native).
LEAF_SITES = {
    0x44bea1, 0x44bee7, 0x44bef2, 0x44bef9, 0x44c247, 0x44c276,
    0x44c36f, 0x44c37e, 0x44c3b7, 0x44c3c6, 0x44c3cd, 0x44c3fa,
    0x44c41c, 0x44c423, 0x44c4c4, 0x44c4cb, 0x44c54d, 0x44c554,
    0x44c5d9, 0x44c5e0, 0x44c65f, 0x44c666, 0x44c6fc, 0x44c730,
    0x44c7da, 0x44c8c8, 0x44cd83, 0x44cd8e, 0x44cd95, 0x44cfec,
    0x44cff7, 0x44cffe,
}

# 0x44cbd0 (`call 45eee0`) lies inside the 16-byte H3 capture hook
# (0x44cbc5..0x44cbd4).  It is RECORD, but recorded by the H3 path as a
# synthesized bdd0_native event (capture.cpp: battle_capture_eee0_*).  Patching
# the bytes there would collide with H3 and be skipped.
H3_OVERLAP = { 0x44cbd0 }

# slot ids (mirror native_sites.h)
SLOT_NONE, SLOT_U28, SLOT_U2C, SLOT_U38, SLOT_U40 = 0, 1, 2, 3, 4
SLOT_BY_DISP = {0x28: SLOT_U28, 0x2c: SLOT_U2C, 0x38: SLOT_U38, 0x40: SLOT_U40}

# result kinds (mirror native_sites.h)
K_INT, K_AL, K_F32, K_VOID, K_PTR, K_PTR_W3 = 0, 1, 2, 3, 4, 5
KNAME = {K_INT: "NS_K_INT", K_AL: "NS_K_AL", K_F32: "NS_K_F32",
         K_VOID: "NS_K_VOID", K_PTR: "NS_K_PTR", K_PTR_W3: "NS_K_PTR_W3"}

# Distinct targets.  (nargs, ret_n, kind, class, reason)
# class: "R" = RECORD (return/side effect feeds flow or a state store),
#        "N" = NOOP_OK (GUI/string/log/dtor; cannot affect flow).
TARGETS = {
    0x442570: (1, 4, K_PTR, "R",
               "game-state accessor; (*eax)==0/1/3 gates, +4..+0xc/+0x10 read"),
    0x439ae0: (2, 0, K_AL, "R",
               "string-object compare; AL tested by the caller"),
    0x42da60: (1, 4, K_PTR, "R",
               "player flag object; return is dereferenced via 435d50"),
    0x435d50: (0, 0, K_PTR_W3, "R",
               "player flags; returned bytes tested (and 2/and 4)"),
    0x448c30: (1, 4, K_VOID, "R",
               "cursor/mode update writes unit state (ambiguous -> RECORD)"),
    0x446a80: (1, 0x3c, K_VOID, "R",
               "message timer update stores unit+0x54 (state store)"),
    0x448ee0: (1, 4, K_VOID, "R",
               "state flag writes unit+0x114, read by the entry gate"),
    0x42d9c0: (1, 4, K_VOID, "R",
               "player pos store (state store, ambiguous -> RECORD)"),
    0x45f430: (0, 0, K_VOID, "R",
               "PathFinder ctor; object consumed by 45e1d0 (state store)"),
    0x45eee0: (1, 4, K_INT, "R",
               "unit-AI tick; return code drives the dispatch cascade"),
    0x4040a0: (0, 0, K_VOID, "N", "msg ctor: GUI/message plumbing"),
    0x4040f0: (1, 4, K_VOID, "N", "msg copy: GUI/message plumbing"),
    0x462780: (0, 0, K_VOID, "N", "msg string dtor"),
    0x446af0: (2, 8, K_VOID, "N", "assert/log path"),
    0x49b0f0: (1, 4, K_PTR, "N", "string ctor (GUI)"),
    0x413060: (3, 0, K_PTR, "N", "string format (GUI)"),
    0x49ad70: (0, 0, K_PTR, "N", "c_str (GUI)"),
    0x49ac70: (0, 0, K_VOID, "N", "string dtor"),
    0x45e5e0: (0, 0, K_VOID, "N", "PathFinder dtor"),
    0x4ec064: (1, 4, K_VOID, "N", "OutputDebugStringA (IAT)"),
}
# leaves (recorded by leaf_sites.*); listed for completeness of the 234 map
for _va, _meta in {
    0x45c6f0: (1, 4, K_INT), 0x45c370: (0, 0, K_INT),
    0x45af50: (0, 0, K_INT), 0x45c730: (0, 0, K_INT),
    0x45b150: (3, 12, K_F32), 0x45b3e0: (3, 12, K_AL),
    0x403e80: (1, 4, K_INT), 0x458050: (6, 24, K_INT),
    0x45e1d0: (2, 8, K_INT), 0x44bbf0: (4, 16, K_INT),
}.items():
    TARGETS[_va] = (_meta[0], _meta[1], _meta[2], "L", "logic leaf (bdd0_leaf)")

# vtable slots (receiver = unit; ECX = unit at every site).
SLOTS = {
    SLOT_U28: (8, 0x20, K_VOID, "u+0x28 world_notify (snapshot by value)"),
    SLOT_U2C: (0, 0, K_F32, "u+0x2c phase_time (result ST(0))"),
    SLOT_U38: (1, 4, K_INT, "u+0x38 pointer call (return EAX)"),
    SLOT_U40: (1, 0, K_PTR, "u+0x40 player_get (return EAX)"),
}


def disasm_calls(data):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    blob = data[BODY_START - VA_BASE:BODY_END - VA_BASE]
    out = []
    for ins in md.disasm(blob, BODY_START):
        if ins.mnemonic != "call":
            continue
        op = ins.operands[0]
        if op.type == X86_OP_IMM:
            out.append((ins.address, op.imm, SLOT_NONE, ins.size))
        elif op.type == X86_OP_MEM:
            slot = SLOT_BY_DISP.get(op.mem.disp, SLOT_NONE)
            if op.mem.base == 0:
                slot = SLOT_NONE      # IAT absolute call
                tgt = op.mem.disp
            else:
                tgt = 0
                if ins.size != 3:
                    sys.exit("indirect slot site 0x%x is %d bytes, expected 3"
                             % (ins.address, ins.size))
            out.append((ins.address, tgt, slot, ins.size))
    return out


def build_rows(calls):
    direct, indirect, noop = [], [], []
    for site, tgt, slot, size in calls:
        if site in LEAF_SITES:
            continue
        if site in H3_OVERLAP:
            continue
        if slot != SLOT_NONE:
            nargs, ret_n, kind, _ = SLOTS[slot]
            indirect.append((site, 0, slot, nargs, ret_n, kind))
            continue
        meta = TARGETS.get(tgt)
        if meta is None:
            sys.exit("unclassified target 0x%x at site 0x%x" % (tgt, site))
        nargs, ret_n, kind, cls, _ = meta
        if cls == "R":
            direct.append((site, tgt, SLOT_NONE, nargs, ret_n, kind))
        else:
            noop.append(site)
    return direct, indirect, noop


def emit(rows):
    direct, indirect, _ = rows
    lines = []
    lines.append("/* GENERATED by tools/probes/gen_native_sites.py -- do not edit.")
    lines.append(" * Source: LETHALDREAMS.EXE, FUN_0044bdd0..0x44d08e (234 call sites).")
    lines.append(" * %d RECORD direct sites + %d RECORD indirect sites." %
                 (len(direct), len(indirect)))
    lines.append(" * 0x44cbd0 (call 45eee0) is RECORD but lies inside the H3 hook;")
    lines.append(" * it is emitted by the H3 capture path, not patched here.")
    lines.append(" * Indirect rows carry slot id; target is resolved at runtime from")
    lines.append(" * the receiver vtable.  Keys: site, target, slot, nargs, ret_n, kind. */")
    lines.append("static const native_site_def k_ns_sites[] = {")
    for site, tgt, slot, nargs, ret_n, kind in direct:
        lines.append("    { 0x%06xu, 0x%06xu, %d, %d, %d, %s },"
                     % (site, tgt, slot, nargs, ret_n, KNAME[kind]))
    for site, tgt, slot, nargs, ret_n, kind in indirect:
        lines.append("    { 0x%06xu, 0x%06xu, %d, %d, %d, %s },"
                     % (site, tgt, slot, nargs, ret_n, KNAME[kind]))
    lines.append("};")
    return "\n".join(lines) + "\n"


def report(calls):
    import collections
    cnt = collections.Counter(c[1] for c in calls if c[1] is not None)
    icnt = collections.Counter(c[2] for c in calls if c[2] != SLOT_NONE)
    print("call-site classification (234 sites):")
    rec = noop = leaf = 0
    for va in sorted(TARGETS):
        nargs, ret_n, kind, cls, reason = TARGETS[va]
        n = cnt.get(va, 0)
        if cls == "R":
            rec += n
        elif cls == "N":
            noop += n
        else:
            leaf += n
        print("  %06x  %-6s %2d  %s" % (va, cls, n, reason))
    print("  indirect slots:")
    for slot, (nargs, ret_n, kind, reason) in sorted(SLOTS.items()):
        d = {1: 0x28, 2: 0x2c, 3: 0x38, 4: 0x40}[slot]
        print("  u+0x%02x %-6s %2d  %s" % (d, "R", icnt.get(slot, 0), reason))
    ind = sum(icnt.values())
    print("  totals: RECORD=%d (direct %d + indirect %d + leaves %d), "
          "NOOP_OK=%d, non-leaf RECORD=%d"
          % (rec + ind + leaf, rec, ind, leaf, noop, rec + ind))


def main():
    exe = paths.game_path("LETHALDREAMS.EXE")
    data = open(exe, "rb").read()
    calls = disasm_calls(data)
    if "--report" in sys.argv:
        report(calls)
        return
    rows = build_rows(calls)
    text = emit(rows)
    if "--check" in sys.argv:
        cur = ""
        if os.path.isfile(OUT):
            cur = open(OUT, "r", encoding="ascii").read()
        if cur != text:
            sys.exit("native_sites_table.inc is stale -- regenerate")
        print("native_sites_table.inc is current")
        return
    if os.path.isfile(OUT):
        old = open(OUT, "r", encoding="ascii").read()
    else:
        old = None
    if old != text:
        open(OUT, "w", encoding="ascii", newline="\n").write(text)
        print("wrote %s" % OUT)
    direct, indirect, noop = rows
    rec = len(direct) + len(indirect) + len(H3_OVERLAP)
    print("call sites=%d  RECORD=%d (direct %d + indirect %d + H3 %d)  "
          "NOOP_OK=%d  leaves=%d" % (len(calls), rec + 32, len(direct),
                                     len(indirect), len(H3_OVERLAP), len(noop),
                                     len(LEAF_SITES)))


if __name__ == "__main__":
    main()
