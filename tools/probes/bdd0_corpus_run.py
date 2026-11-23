#!/usr/bin/env python3
"""Run the checked-in v12 bdd0_native corpus with semantic assertions.

Positive: tools/probes/bdd0/corpus/synth_v12_native.jsonl
  rc 0 AND exact per-check counts (so an all-SKIP capture cannot pass):
  native_seq 3/0/0, native_args 1/2/0, res_match 3/0/0, state_match 3/0/0,
  leaf_value 1/2/0 (порт листа 45c370 заявляет значение — claim сверен).
Positive: tools/probes/bdd0/corpus/synth_band_begin_zero.jsonl
  rc 0, leaf_seq 1/0/0, leaf_value 1/0/0 — begin == 0 при end != 0: голова
  обязана быть NULL (правило TEST/JZ в 45af50), порт не идёт по длине.
Negatives:
  dropped/trap/order      -> --check native_seq, rc 1
  corrupt_native_args     -> --check native_args, rc 1, arg mismatch reported
  corrupt_native_ptr_unbound -> --check native_args, rc 0 but pass=0 skip=3 and
                             a visible `unbound` counter (not rc0-with-pass)
  corrupt_leaf_ported_request -> rc 1, leaf_seq fail (в потоке запись
                             портированного листа там, где порт просит лист)
  corrupt_leaf_claim_extra / _missing -> rc 1, leaf_seq fail, leaf_value не
                             fail (лишняя/пропавшая запись claim — это поток)

Build first: python3 tools/probes/bdd0_harness_build.py
Usage: python3 tools/probes/bdd0_corpus_run.py
"""
import os
import re
import subprocess
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
import paths  # noqa: E402

HARNESS = paths.win_path(os.path.join(_ROOT, "build", "ldhook", "bdd0_harness.exe"))
CORPUS = paths.win_path(os.path.join(_HERE, "bdd0", "corpus"))

POS_EXPECT = {
    "synth_v12_native.jsonl": {
        "native_seq": (3, 0, 0),
        "native_args": (1, 2, 0),
        "res_match": (3, 0, 0),
        "state_match": (3, 0, 0),
        "leaf_value": (1, 2, 0),
    },
    "synth_band_begin_zero.jsonl": {
        "leaf_seq": (1, 0, 0),
        "leaf_value": (1, 0, 0),
    },
}

# name, expected rc, single check to run (None = all)
CASES = [
    ("synth_v12_native.jsonl", 0, None),
    ("synth_band_begin_zero.jsonl", 0, None),
    ("corrupt_native_dropped.jsonl", 1, "native_seq"),
    ("corrupt_native_trap.jsonl", 1, "native_seq"),
    ("corrupt_native_order.jsonl", 1, "native_seq"),
    ("corrupt_native_args.jsonl", 1, "native_args"),
    ("corrupt_native_ptr_unbound.jsonl", 0, "native_args"),
    ("corrupt_leaf_ported_request.jsonl", 1, None),
    ("corrupt_leaf_claim_extra.jsonl", 1, None),
    ("corrupt_leaf_claim_missing.jsonl", 1, None),
]

# negatives whose failure must be attributed to leaf_seq, not leaf_value
LEAF_SEQ_NEG = {
    "corrupt_leaf_ported_request.jsonl",
    "corrupt_leaf_claim_extra.jsonl",
    "corrupt_leaf_claim_missing.jsonl",
}


def run(name, check):
    path = os.path.join(CORPUS, name)
    cmd = [HARNESS, "--impl", "port"]
    if check:
        cmd += ["--check", check]
    cmd.append(path)
    p = subprocess.run(cmd, capture_output=True)
    text = (p.stdout + p.stderr).decode("utf-8", "replace")
    counts = {}
    for m in re.finditer(r"^\s+(\w+)\s+pass=(\d+) skip=(\d+) fail=(\d+)\s*$",
                         text, re.M):
        counts[m.group(1)] = (int(m.group(2)), int(m.group(3)),
                              int(m.group(4)))
    return p.returncode, text, counts


def main():
    if not os.path.isfile(HARNESS):
        sys.exit("no %s -- run: python3 tools/probes/bdd0_harness_build.py"
                 % HARNESS)
    bad = 0
    for name, want_rc, check in CASES:
        rc, text, counts = run(name, check)
        ok = (rc == want_rc)
        detail = "rc=%d" % rc
        if name in POS_EXPECT:
            for k, v in POS_EXPECT[name].items():
                got = counts.get(k)
                detail += " %s=%s" % (k, got)
                if got != v:
                    ok = False
        elif check == "native_args":
            na = counts.get("native_args", (0, 0, 0))
            if name == "corrupt_native_args.jsonl":
                ok = ok and na[2] >= 1 and "arg[" in text
                detail += " native_args=%s" % (na,)
            else:
                ok = ok and na[0] == 0 and na[1] >= 1 and na[2] == 0 \
                     and "unbound" in text
                detail += " native_args=%s unbound=%s" % (
                    na, "yes" if "unbound" in text else "no")
        elif name in LEAF_SEQ_NEG:
            ls = counts.get("leaf_seq", (0, 0, 0))
            lv = counts.get("leaf_value", (0, 0, 0))
            ok = ok and ls[2] >= 1 and lv[2] == 0
            detail += " leaf_seq=%s leaf_value=%s" % (ls, lv)
        print("  %-36s %s  %s" % (name, "PASS" if ok else "FAIL", detail))
        if not ok:
            bad += 1
    print("bdd0 corpus: %s (%d failures)"
          % ("OK" if bad == 0 else "FAILED", bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())