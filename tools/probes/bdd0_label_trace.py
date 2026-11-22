#!/usr/bin/env python3
"""Insert the compile-time-gated label/branch trace into tools/ldhook/battle/tick.cpp.

Every source label of the body gets `B0_LBL(id)`, every `if (cond)` gets
`B0_TST(gate, cond)`, every `return` of b0_body_r1_r2 gets `B0_EXIT(id)`.  With
-DB0_TRACE_LABELS=1 the stamps push one event per label / branch / exit into a
fixed ring that the harness reads; without the flag every macro expands to
nothing, so tick.obj is byte-identical and the in-game build is unaffected.

Ids are assigned by first appearance.  Condition names are the source
expressions (the printed trace shows the condition + Taken/Not-Taken); exit
names are `<label>_ret_<value>`.  The trace section is rewritten between the
markers `=== BEGIN LABEL TRACE ===` / `=== END LABEL TRACE ===`.  `--check`
reports the counts without writing.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TICK = os.path.join(ROOT, "tools", "ldhook", "battle", "tick.cpp")

BEGIN = "/* === BEGIN LABEL TRACE === */"
END = "/* === END LABEL TRACE === */"

label_re = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:\s*$")
return_re = re.compile(r"^\s*return\b(.*)$")
if_head_re = re.compile(r"^(\s*)if\s*\(\s*(.*)$")


def strip_comments(text):
    """comments -> spaces, keeping line structure (for brace counting)"""
    out = []
    i, n = 0, len(text)
    in_block = False
    while i < n:
        c = text[i]
        if in_block:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                in_block = False
                out.append("  ")
                i += 2
            else:
                out.append(" " if c != "\n" else "\n")
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            in_block = True
            out.append("  ")
            i += 2
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def find_body(text, fname):
    """line indices of the function `fname` (start..end inclusive, 0-based)"""
    lines = text.split("\n")
    clean = strip_comments(text).split("\n")
    start = None
    for i, ln in enumerate(clean):
        if re.match(r"^\s*[A-Za-z_].*\b%s\s*\(" % fname, ln):
            start = i
            break
    if start is None:
        return None
    depth = 0
    opened = False
    for i in range(start, len(clean)):
        for ch in clean[i]:
            if ch == "{":
                depth += 1
                opened = True
            elif ch == "}":
                depth -= 1
        if opened and depth == 0:
            return start, i
    return None


def match_cond(lines, i):
    """condition text of an `if (` starting on line i (1-based, comment-blanked
    lines), balancing parens.  Returns (cond, last_line_1_based, col_of_closing)
    where col is the column of the closing `)` in the last line."""
    m = if_head_re.match(lines[i - 1])
    rest = m.group(2)
    depth = 1
    cond = ""
    for pos, ch in enumerate(rest):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                # column inside the original line: prefix of `if (` + rest
                col = len(lines[i - 1]) - len(rest) + pos
                return cond, i, col
        cond += ch
    j = i
    while j < len(lines):
        ln = lines[j]
        cur = ""
        for pos, ch in enumerate(ln):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    cond += cur
                    return cond, j + 1, pos
            cur += ch
        cond += cur + " "
        j += 1
    return None, None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    text = open(TICK, encoding="utf-8").read()
    lines = text.split("\n")
    clean = strip_comments(text).split("\n")   # comments blanked for detection

    body = find_body(text, "b0_body_r1_r2")
    if body is None:
        sys.exit("b0_body_r1_r2 not found in %s" % TICK)
    b0, b1 = body
    print("body b0_body_r1_r2: source lines %d..%d" % (b0 + 1, b1 + 1))

    stamps = []
    labels = []
    label_id = {}
    conds = {}
    for i in range(b0, b1 + 1):
        ln = clean[i]
        m = label_re.match(ln)
        if m:
            name = m.group(1)
            if name not in label_id:
                label_id[name] = len(labels)
                labels.append(name)
            stamps.append((i, "label", label_id[name]))
            continue
        if if_head_re.match(ln):
            cond, endl, col = match_cond(clean, i + 1)
            if cond is None:
                sys.exit("cannot balance if-condition at line %d" % (i + 1))
            gid = len(conds)
            conds[gid] = " ".join(cond.split())
            stamps.append((i, "if", (gid, lines[i][:len(lines[i]) -
                                             len(lines[i].lstrip())], cond,
                                     endl, col)))

    exits = []
    last_label = "entry"
    in_handler = False
    for i in range(b0, b1 + 1):
        ln = clean[i]
        if "__except" in ln:
            in_handler = True
        m = label_re.match(ln)
        if m and m.group(1) in label_id:
            last_label = m.group(1)
        m = return_re.match(ln)
        if m:
            val = m.group(1).strip().rstrip(";").strip()
            if in_handler:
                exits.append((i, "__except_handler_ret_%s" % val))
            else:
                exits.append((i, "%s_ret_%s" % (last_label, val)))

    print("tick.cpp body: %d labels, %d if-conditions, %d returns"
          % (len(labels), len(conds), len(exits)))
    if args.check:
        for g, c in sorted(conds.items()):
            print("  cond %d: %s" % (g, c))
        for i, n in exits:
            print("  exit %d (line %d): %s" % (128 + exits.index((i, n)), i + 1, n))
        sys.exit(0)

    out = list(lines)
    # multi-line ifs: collapse the condition into the first line, then stamp.
    # the closing `)` column is found in the comment-blanked line, so a `)`
    # inside a trailing comment is never mistaken for the end of the condition.
    ops = [(i, "label", p) for (i, k, p) in stamps if k == "label"]
    ops += [(i, "if", p) for (i, k, p) in stamps if k == "if"]
    ops += [(i, "exit", n) for (i, n) in exits]
    for i, kind, payload in sorted(ops, key=lambda t: -t[0]):
        indent = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
        if kind == "label":
            out[i] = out[i] + "\n" + indent + "B0_LBL(%d);" % payload
        elif kind == "exit":
            eid = 128 + exits.index((i, payload))
            out[i] = indent + "B0_EXIT(%d);" % eid + "\n" + out[i]
        else:
            gid, _ind, cond, endl, col = payload
            suffix = lines[endl - 1][col + 1:]
            if endl > i + 1:
                del out[i + 1:endl]
            out[i] = indent + "if (B0_TST(%d, (%s))) %s" % (
                gid, " ".join(cond.split()), suffix)

    for i, name in exits:
        eid = 128 + exits.index((i, name))
        indent = out[i][:len(out[i]) - len(out[i].lstrip())]
        out[i] = indent + "B0_EXIT(%d);" % eid + "\n" + out[i]

    def q(s):
        return s.replace("\\", "\\\\").replace('"', '\\"')

    pad = [""] * (128 - len(labels))
    names = labels + pad + [n for (_i, n) in exits]
    if len(names) > 128 + len(exits):
        sys.exit("more than 128 labels: expand the name table layout")
    lbl_tbl = "\n".join('    "%s",' % q(n) for n in names)
    cond_max = max(conds) + 1
    cond_tbl = "\n".join('    "%s",' % q(conds.get(g, ""))
                         for g in range(cond_max))

    block = BEGIN + (
        """
/* Label trace, gated by -DB0_TRACE_LABELS=1.
 * B0_LBL(id): label id -> ring; B0_TST(gate, cond): condition + direction as one
 * event (bit31 = condition event, bit30 = branch taken, low bits = gate);
 * B0_EXIT(128+k): exit of the body.  Without the flag all three expand to
 * nothing, so tick.obj is byte-identical and the in-game build is unaffected.
 * b0_last_label feeds the native/leaf call stamps of native_replay.cpp. */
#if B0_TRACE_LABELS
#define B0_TRACE_RING 2048
static volatile unsigned g_b0_ring[B0_TRACE_RING];
volatile int b0_trace_on = 1;
volatile unsigned b0_last_label = 0;
volatile int b0_tick_len = 0;
volatile int b0_ring_i = 0;
const unsigned *b0_trace_ring(void) { return (const unsigned *)g_b0_ring; }
/* arrays of pointers, not const arrays: a top-level const object has internal
 * linkage in C++, so the harness could not link against them. */
const char *b0_label_names[] = {
__LBL__
};
const char *b0_cond_names[] = {
__COND__
};
static void b0_lbl(int id)
{
    b0_last_label = (unsigned)id;
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)id;
    b0_ring_i++;
}
static int b0_tst(int gate, int cond)
{
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)gate | 0x80000000u |
        (cond ? 0x40000000u : 0u);
    b0_ring_i++;
    return cond;
}
static void b0_exit(int id)
{
    b0_tick_len++;
    g_b0_ring[b0_ring_i % B0_TRACE_RING] = (unsigned)id;
    b0_ring_i++;
}
#define B0_LBL(id) b0_lbl(id)
#define B0_TST(gate, cond) b0_tst((gate), (cond))
#define B0_EXIT(id) b0_exit(id)
#else
#define B0_LBL(id) ((void)0)
#define B0_TST(gate, cond) (cond)
#define B0_EXIT(id) ((void)0)
#endif
"""
        + END).replace("__LBL__", lbl_tbl).replace("__COND__", cond_tbl)

    text = "\n".join(out)
    a = text.find(BEGIN)
    if a >= 0:
        b = text.find(END)
        if b < a:
            sys.exit("trace block markers out of order")
        text = text[:a] + block + text[b + len(END):]
    else:
        anchor = '#include "native.h"'
        pos = text.find(anchor)
        if pos < 0:
            sys.exit("anchor for the trace section not found")
        text = text[:pos + len(anchor)] + "\n" + block + "\n" + text[pos + len(anchor):]

    with open(TICK, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("patched %s: %d label names, %d condition names, %d exits"
          % (TICK, len(labels), cond_max, len(exits)))


if __name__ == "__main__":
    main()
