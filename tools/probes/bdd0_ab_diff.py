#!/usr/bin/env python3
"""bdd0_ab_diff.py - offline A/B diff of FUN_0044bdd0 captures (capture.cpp v10/v11).

Input: two capture jsonl files with `bdd0_cap` (H1 @0x44bdf5) and `bdd0_res`
(H2 @0x44d074) records - one pair per call of the battle AI dispatcher, paired
by `seq` (the capture keeps its own counter g_bdd0_seq). Baseline = native body
run, candidate = ported body run. Other kinds of the file (cap/res/grid/eee0_*)
are ignored.

Compared for every seq present on BOTH sides of each record type:
  res: al, u170, out, m0, m1, m2, trap
  cap: p1, p2, p3 presence, u170, gate114, trap
v11 adds to res: anc (frame+0x98 grid-anchor result), tag (frame+0x88
grid-record tag), tri0..tri2 (frame+0x2c..0x34 pos triple), a4n (frame+0x38,
arg4 of 458050). These fields are compared only when BOTH inputs are v11; on
v10 inputs the diff is exactly the v10 behaviour. Files whose versions differ
are reported as a `version` divergence (rc 1): a v10 run is not comparable
with a v11 run.

tri0..tri2 must mirror m0..m2 inside each file (R14 copies unit+0xa4..0xac
into the frame triple); an internal disagreement is reported as `self.tri`
(rc 1), so a broken capture is visible even when both sides match each other.

Cap fields are compared for every seq that has `bdd0_cap` on both sides,
including the unpaired tail cap (the capture always ends with one - the
truncated final call), so gate114/u170 divergences in the tail are not masked.
p3 is a raw pointer: only presence (p3 != 0) is compared, as cap.p3_presence.
m0..m2 and tri0..tri2 are the raw dwords of unit+0xa4..0xac / frame slots
(float bits), compared bit-exactly; the float value is printed next to the
raw dword.

`unit` is a deterministic integer id, so it is compared on both records, but
advisory only: a mismatch prints an ADVISORY line and counts as advisory.unit,
and never sets rc 1. Never compared: self, ai, fn, tick (pointers and timing).

Equal unpaired tails on both sides are not a divergence; a presence/absence
difference for a seq (cap vs cap+res, present vs missing) is a `pairing`
divergence (rc 1). The summary lists which seqs were compared cap-only.

rc: 0 - every compared seq is equal and the seq sets match;
    1 - field divergence, pairing mismatch, self.tri, version mismatch,
        or unequal tail counts (advisory.unit alone does not set it);
    2 - file unreadable / malformed record / bad arguments.
"""
import json
import os
import struct
import sys
import tempfile

RES_FIELDS = ["al", "u170", "out", "m0", "m1", "m2", "trap"]
CAP_FIELDS = ["p1", "p2", "p3_presence", "u170", "gate114", "trap"]
# v11-only res fields: compared when both inputs are v11.
V11_RES_FIELDS = ["anc", "tag", "tri0", "tri1", "tri2", "a4n"]
SIDE_FIELDS = [("res", RES_FIELDS + V11_RES_FIELDS), ("cap", CAP_FIELDS)]
# Fields that must be present in the record as integers (p3_presence is derived
# from p3). `unit` is compared on both records (advisory).
RES_RAW = RES_FIELDS + ["unit"]
RES_RAW_V11 = RES_RAW + V11_RES_FIELDS
CAP_RAW = ["p1", "p2", "p3", "u170", "gate114", "trap", "unit"]
FLOAT_FIELDS = ("m0", "m1", "m2", "tri0", "tri1", "tri2")
# tri* must mirror m0..m2 inside one file; key is not a side.field pair.
TRI_PAIRS = [("m0", "tri0"), ("m1", "tri1"), ("m2", "tri2")]
ADV_KEY = "advisory.unit"
SELF_KEY = "self.tri"
VER_KEY = "version"
RANK = dict((key, i) for i, key in enumerate(
    [VER_KEY] + ["res.%s" % f for f in RES_FIELDS + V11_RES_FIELDS]
    + ["cap.%s" % f for f in CAP_FIELDS]
    + [SELF_KEY, "pairing", ADV_KEY]))

USAGE = ("usage: bdd0_ab_diff.py <baseline.jsonl> <candidate.jsonl> "
         "[--max-show=N] [--quiet]\n"
       "       bdd0_ab_diff.py --self-test <file.jsonl>")

HELP = (USAGE + "\n"
        "  --help: notes\n"
        "  cap fields are compared for every seq with bdd0_cap on BOTH sides, "
        "including\n  the unpaired tail cap; equal tails on both sides are not "
        "a divergence, a\n  presence/absence difference for a seq is a "
        "`pairing` divergence (rc 1).\n"
        "  cap p3 is a raw pointer: only presence (p3 != 0) is compared, as "
        "cap.p3_presence.\n"
        "  v11 res adds anc, tag, tri0..tri2, a4n: compared only when BOTH "
        "inputs\n  are v11; tri* must mirror m0..m2 in each file, and an "
        "internal disagreement\n  is reported as self.tri (rc 1). Files of "
        "different versions are a version\n  divergence (rc 1). v10 inputs "
        "behave exactly as before.\n"
        "  unit is a deterministic id, compared on both records as ADVISORY: "
        "a mismatch\n  prints an ADVISORY line and counts as advisory.unit, "
        "and never sets rc 1.\n"
        "  never compared: self, ai, fn, tick. rc: 0 identical, 1 diverged, "
        "2 format/IO.")


def read_lines(path):
    """Lines of the file; a trailing partial record (game killed mid-write) is
    dropped so an aborted capture does not give rc 2."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        data = fh.read()
    lines = data.split("\n")
    if lines and lines[-1] != "":
        lines.pop()
    return lines


def is_int(v):
    """JSON integer: int, not bool (true/false is not an integer field)."""
    return isinstance(v, int) and not isinstance(v, bool)


def load(path):
    """(caps, res, v): seq -> (rec, raw_line) for one run, plus the version of
    its bdd0 pair. Validates v/fn/kind, unique integer seq, and that every
    compared field is present as an integer (a non-integer JSON value is a
    format error, rc 2). A file mixing v10 and v11 bdd0 records is a format
    error: the capture writes one version per run. Other kinds are ignored."""
    caps, res = {}, {}
    ver = None
    for line in read_lines(path):
        if not line.strip():
            continue
        rec = json.loads(line)
        kind = rec.get("kind")
        if kind not in ("bdd0_cap", "bdd0_res"):
            continue
        dst = caps if kind == "bdd0_cap" else res
        v = rec.get("v")
        if v is True or v is False or v not in (10, 11):
            raise ValueError("bad v=%r in %s" % (v, path))
        if ver is None:
            ver = v
        elif ver != v:
            raise ValueError("mixed bdd0 v=%r and v=%r in %s" % (ver, v, path))
        ints = CAP_RAW if kind == "bdd0_cap" else (
            RES_RAW_V11 if v == 11 else RES_RAW)
        if rec.get("fn") != "44bdd0":
            raise ValueError("bad fn=%r at seq=%r in %s"
                             % (rec.get("fn"), rec.get("seq"), path))
        seq = rec.get("seq")
        if not is_int(seq):
            raise ValueError("bad seq=%r in %s" % (seq, path))
        if seq in dst:
            raise ValueError("duplicate %s seq=%d in %s" % (kind, seq, path))
        for f in ints:
            if f not in rec:
                raise ValueError("missing %s field %s at seq=%d in %s"
                                 % (kind, f, seq, path))
            if not is_int(rec[f]):
                raise ValueError("non-integer %s field %s=%r at seq=%d in %s"
                                 % (kind, f, rec[f], seq, path))
        dst[seq] = (rec, line)
    if not caps:
        raise ValueError("no bdd0_cap records in %s" % path)
    if not res:
        raise ValueError("no bdd0_res records in %s" % path)
    return caps, res, ver


def field_value(rec, f):
    if f == "p3_presence":
        return "present" if rec["p3"] != 0 else "absent"
    return rec[f]


def fmt_val(key, v, rec):
    """Field value for display; m0..m2 print the raw dword and the float bits."""
    name = key.split(".", 1)[1] if "." in key else key
    if name in FLOAT_FIELDS:
        f = struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0]
        return "%d fp=%r" % (v, f)
    if name == "p3_presence":
        return "%s(ptr=%d)" % (v, rec["p3"])
    return "%s" % v


def cap_raw(side, seq):
    return side[0][seq][1] if seq in side[0] else "missing"


def present(caps, res, seq):
    s = "cap" if seq in caps else ""
    return (s + "+res") if seq in res else s


def pairing_issues(base, cand):
    """seqs whose presence vector (cap/res) differs between the sides."""
    out = []
    for seq in sorted(set(base[0]) | set(base[1]) | set(cand[0]) | set(cand[1])):
        b = present(base[0], base[1], seq)
        c = present(cand[0], cand[1], seq)
        if b != c:
            out.append((seq, "pairing", b, c, cap_raw(base, seq),
                        cap_raw(cand, seq), "DIVERGE"))
    return out


def scan(base, cand, idx, side, seqs, counts, events, v11):
    """Compare the fields of one record type for the seqs present on both sides.
    v11 res fields (anc, tag, tri0..tri2, a4n) are compared only when both
    inputs are v11."""
    if side == "cap":
        fields = CAP_FIELDS
    else:
        fields = RES_FIELDS + V11_RES_FIELDS if v11 else RES_FIELDS
    for seq in seqs:
        a = base[idx][seq][0]
        b = cand[idx][seq][0]
        raws = (cap_raw(base, seq), cap_raw(cand, seq))
        for f in fields:
            va, vb = field_value(a, f), field_value(b, f)
            if va != vb:
                key = "%s.%s" % (side, f)
                counts[key] = counts.get(key, 0) + 1
                events.append((seq, key, fmt_val(key, va, a), fmt_val(key, vb, b),
                               raws[0], raws[1], "DIVERGE"))
        if a["unit"] != b["unit"]:
            counts[ADV_KEY] = counts.get(ADV_KEY, 0) + 1
            events.append((seq, "%s(%s)" % (ADV_KEY, side), str(a["unit"]),
                           str(b["unit"]), raws[0], raws[1], "ADVISORY"))


def self_tri(base, cand, counts, events):
    """v11: tri0..tri2 must mirror m0..m2 inside each file (R14 copies
    unit+0xa4..0xac into the frame triple). A file whose own slots disagree is
    a broken capture: reported as self.tri (rc 1)."""
    for seq in sorted(set(base[1]) & set(cand[1])):
        desc = []
        for side in (base, cand):
            rec = side[1][seq][0]
            bad = ["%s=%d %s=%d" % (m, rec[m], t, rec[t])
                   for m, t in TRI_PAIRS if rec[m] != rec[t]]
            desc.append("; ".join(bad) if bad else "ok")
        if desc[0] != "ok" or desc[1] != "ok":
            counts[SELF_KEY] = counts.get(SELF_KEY, 0) + 1
            events.append((seq, SELF_KEY, desc[0], desc[1],
                           cap_raw(base, seq), cap_raw(cand, seq), "DIVERGE"))


def fmt_seqs(seqs, limit=10):
    s = ",".join(str(x) for x in seqs[:limit])
    left = len(seqs) - limit
    return "[%s%s]" % (s, ",...+%d more]" % left if left > 0 else "")


def compare(base, cand, max_show, quiet):
    """Returns (rc, lines, events). events is the full list of divergences and
    advisories: (seq, field, base_str, cand_str, base_cap_raw, cand_cap_raw,
    tag), where tag is DIVERGE or ADVISORY."""
    counts, events = {}, []
    v11 = base[2] == 11 and cand[2] == 11
    cap_common = sorted(set(base[0]) & set(cand[0]))
    res_common = sorted(set(base[1]) & set(cand[1]))
    scan(base, cand, 0, "cap", cap_common, counts, events, v11)
    scan(base, cand, 1, "res", res_common, counts, events, v11)
    if v11:
        self_tri(base, cand, counts, events)
    if base[2] != cand[2]:
        counts[VER_KEY] = counts.get(VER_KEY, 0) + 1
        events.append((0, VER_KEY, "v%d" % base[2], "v%d" % cand[2],
                       "n/a", "n/a", "DIVERGE"))
    issues = pairing_issues(base, cand)
    events += issues
    events.sort(key=lambda e: (e[0], RANK.get(e[1], len(RANK))))
    tail = (len(set(base[0]) - set(base[1])), len(set(cand[0]) - set(cand[1])))
    rc = 1 if any(e[6] == "DIVERGE" for e in events) else 0

    lines = []
    if not quiet:
        for seq, key, a, b, rawb, rawc, tag in events[:max_show]:
            lines.append("%s seq=%d field=%s base=%s cand=%s"
                         % (tag, seq, key, a, b))
            lines.append("  cap base: %s" % rawb)
            lines.append("  cap cand: %s" % rawc)
        left = len(events) - max_show
        if left > 0:
            lines.append("... +%d more events" % left)
    tail_s = "%d" % tail[0] if tail[0] == tail[1] else "%d/%d" % tail
    lines.append("SUMMARY: seqs=%d paired=%d caponly=%s base(cap=%d res=%d) "
                 "cand(cap=%d res=%d) unpaired_tail=%s pairing=%d v=%d/%d"
                 % (len(set(cap_common) | set(res_common)),
                    len(set(cap_common) & set(res_common)),
                    fmt_seqs(sorted(set(cap_common) - set(res_common))),
                    len(base[0]), len(base[1]),
                    len(cand[0]), len(cand[1]), tail_s, len(issues),
                    base[2], cand[2]))
    lines.append("SUMMARY: mismatches %s total=%d %s=%d rc=%d"
                 % (" ".join("%s.%s=%d" % (side, f, counts.get("%s.%s" % (side, f), 0))
                             for side, fields in SIDE_FIELDS for f in fields),
                    sum(counts.get(k, 0) for k in RANK if k != ADV_KEY) + len(issues),
                    ADV_KEY, counts.get(ADV_KEY, 0), rc))
    return rc, lines, events


def write_tmp(text):
    fd, p = tempfile.mkstemp(prefix="bdd0_ab_diff_", suffix=".jsonl")
    with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    return p


def lines_of(raw):
    """Lines of a builder text without the trailing empty element; every helper
    below returns text with exactly one trailing newline, so read_lines keeps
    the last record (an appended tail cap must stay visible)."""
    lines = raw.split("\n")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def edit_record(raw, kind, muts, last):
    """Apply muts (field -> op(value)) to the first (last=False) or last
    (last=True) record of `kind`. Returns (new_text, seq); seq is None if the
    kind is absent."""
    lines = lines_of(raw)
    idx = None
    for i, line in enumerate(lines):
        if line.strip() and json.loads(line).get("kind") == kind:
            idx = i
            if not last:
                break
    if idx is None:
        return raw, None
    rec = json.loads(lines[idx])
    for f, op in muts.items():
        rec[f] = op(rec[f])
    lines[idx] = json.dumps(rec, separators=(",", ":"))
    return "\n".join(lines) + "\n", rec["seq"]


def drop_record(raw, kind, last):
    """Delete the last (last=True) bdd0_cap/bdd0_res line. Returns (text, seq)."""
    lines = lines_of(raw)
    idx = None
    for i, line in enumerate(lines):
        if line.strip() and json.loads(line).get("kind") == kind:
            if last:
                idx = i
            elif idx is None:
                idx = i
    if idx is None:
        return raw, None
    seq = json.loads(lines[idx]).get("seq")
    lines.pop(idx)
    return "\n".join(lines) + "\n", seq


def append_cap(raw, new_seq):
    """Append a copy of the last bdd0_cap with seq=new_seq (an extra tail cap)."""
    rec = None
    for line in lines_of(raw):
        if line.strip() and json.loads(line).get("kind") == "bdd0_cap":
            rec = json.loads(line)
    if rec is None:
        return raw, None
    rec["seq"] = new_seq
    lines = lines_of(raw)
    lines.append(json.dumps(rec, separators=(",", ":")))
    return "\n".join(lines) + "\n", new_seq


def drop_all(raw, kind):
    out = []
    for line in lines_of(raw):
        if line.strip() and json.loads(line).get("kind") == kind:
            continue
        out.append(line)
    return "\n".join(out) + "\n"


def set_version(raw, new_v):
    """Rewrite v on every bdd0_cap/bdd0_res line (self-test version check)."""
    lines = lines_of(raw)
    for i, line in enumerate(lines):
        if line.strip() and json.loads(line).get("kind") in ("bdd0_cap",
                                                              "bdd0_res"):
            rec = json.loads(line)
            rec["v"] = new_v
            lines[i] = json.dumps(rec, separators=(",", ":"))
    return "\n".join(lines) + "\n"


def self_test(path, max_show):
    """Asserts, each printing PASS/FAIL:
      self-diff of a temp copy -> rc 0;
      res mutations (al + 1, m2 bit flip) -> rc 1 and both fields reported;
      cap mutation (gate114 ^ 1) on the tail cap, which is cap-only on both
      sides -> rc 1 and cap.gate114 reported;
      dropping the trailing cap, and appending an extra cap -> rc 1 and a
      `pairing` divergence;
      a file with no bdd0_cap records -> rc 2 (so an emptied field list cannot
      pass silently).
    On a v11 input, additionally: anc^1, a4n^1 and tri1^1 mutations -> rc 1 with
    res.anc / res.a4n / res.tri1 + self.tri reported, and a v10 candidate
    against the v11 baseline -> rc 1 with `version` reported.
    Returns 0 only if every assert passes. Temp files are created here and
    removed by this function.
    """
    ok = True
    tmps = []
    try:
        raw = "\n".join(read_lines(path)) + "\n"
        base = load(path)
        caps = [json.loads(l) for l in raw.split("\n")
                if l.strip() and json.loads(l).get("kind") == "bdd0_cap"]
        res = [json.loads(l) for l in raw.split("\n")
               if l.strip() and json.loads(l).get("kind") == "bdd0_res"]
        if not caps or not res:
            print("SELFTEST FAIL: %s needs at least one bdd0_cap and bdd0_res" % path)
            return 1

        mut_res, res_seq = edit_record(raw, "bdd0_res",
                                       {"al": lambda v: v + 1,
                                        "m2": lambda v: v ^ 1}, False)
        _, tail_seq = edit_record(raw, "bdd0_cap", {"gate114": lambda v: v}, True)
        mut_tail, _ = edit_record(raw, "bdd0_cap", {"gate114": lambda v: v ^ 1}, True)
        drop_tail, _ = drop_record(raw, "bdd0_cap", True)
        extra, extra_seq = append_cap(raw, tail_seq + 1)
        checks = [
            ("self-diff", raw, 0, []),
            ("res.al+1 and res.m2^1 at seq=%d" % res_seq, mut_res,
             1, ["res.al", "res.m2"]),
            ("cap.gate114^1 on the tail cap at seq=%d (cap-only seq)" % tail_seq,
             mut_tail, 1, ["cap.gate114"]),
            ("drop the trailing cap at seq=%d (tail count differs)" % tail_seq,
             drop_tail, 1, ["pairing"]),
            ("append an extra cap at seq=%d (tail count differs)" % extra_seq,
             extra, 1, ["pairing"]),
            ("drop all bdd0_cap records (format error)",
             drop_all(raw, "bdd0_cap"), 2, []),
        ]
        if base[2] == 11:
            mut_anc, anc_seq = edit_record(raw, "bdd0_res",
                                           {"anc": lambda v: v ^ 1}, False)
            mut_a4n, a4n_seq = edit_record(raw, "bdd0_res",
                                           {"a4n": lambda v: v ^ 1}, False)
            mut_tri, tri_seq = edit_record(raw, "bdd0_res",
                                           {"tri1": lambda v: v ^ 1}, False)
            checks += [
                ("res.anc^1 at seq=%d" % anc_seq, mut_anc, 1, ["res.anc"]),
                ("res.a4n^1 at seq=%d" % a4n_seq, mut_a4n, 1, ["res.a4n"]),
                ("res.tri1^1 at seq=%d (tri must mirror m)" % tri_seq, mut_tri,
                 1, ["res.tri1", SELF_KEY]),
                ("v10 candidate against the v11 baseline", set_version(raw, 10),
                 1, [VER_KEY]),
            ]
        for label, text, want_rc, want_keys in checks:
            p = write_tmp(text)
            tmps.append(p)
            note = ""
            try:
                rc, _, events = compare(base, load(p), max_show, True)
                keys = sorted(set(e[1] for e in events))
            except Exception as e:
                rc, keys, note = 2, [], str(e)
            caught = rc == want_rc and all(k in keys for k in want_keys)
            ok = ok and caught
            print("SELFTEST %s: rc=%d expect %d events=%s%s: %s"
                  % (label, rc, want_rc, keys, (" err=%s" % note) if note else "",
                     "PASS" if caught else "FAIL"))
    except Exception as e:
        print("SELFTEST FAIL: %s" % e)
        return 2
    finally:
        for p in tmps:
            try:
                os.remove(p)
            except OSError:
                pass
    print("SELFTEST: %s rc=%d" % ("PASS" if ok else "FAIL", 0 if ok else 1))
    return 0 if ok else 1


def parse_args(argv):
    files, max_show, quiet, st, show_help = [], 5, False, False, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--self-test":
            st = True
        elif a == "--quiet":
            quiet = True
        elif a == "--help":
            show_help = True
        elif a.startswith("--max-show="):
            max_show = int(a.split("=", 1)[1])
        elif a == "--max-show":
            i += 1
            if i >= len(argv):
                raise ValueError("missing value for --max-show")
            max_show = int(argv[i])
        elif a.startswith("--"):
            raise ValueError("unknown option %s" % a)
        else:
            files.append(a)
        i += 1
    if max_show < 0:
        raise ValueError("max-show must be >= 0")
    if show_help:
        return [], 5, False, False, True
    if st and len(files) != 1:
        raise ValueError("--self-test needs exactly one file")
    if not st and len(files) != 2:
        raise ValueError("need baseline and candidate files")
    return files, max_show, quiet, st, False


def main(argv):
    try:
        files, max_show, quiet, st, show_help = parse_args(argv)
    except ValueError as e:
        print("ERROR: %s" % e)
        print(USAGE)
        return 2
    if show_help:
        print(HELP)
        return 0
    if st:
        return self_test(files[0], max_show)
    try:
        base = load(files[0])
        cand = load(files[1])
    except (OSError, ValueError) as e:
        print("ERROR: %s" % e)
        return 2
    rc, lines, _ = compare(base, cand, max_show, quiet)
    for line in lines:
        print(line)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
