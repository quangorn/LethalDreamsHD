#!/usr/bin/env python3
"""Покрытие тела FUN_0044bdd0 каталогом — измерение по id (метки, условия+направление,
выходы, лист-сайты, RECORD-сайты) через trace-сборку харнесса.

Запуском из корня репозитория:
    python3 tools/probes/bdd0_coverage.py <capture1.jsonl> [capture2.jsonl ...]

Каталог: b0_label_names (tools/ldhook/battle/tick.cpp), cond/exit
(tools/probes/bdd0_label_trace.py --check), LS_SITE
(tools/ldhook/battle/leaf_sites.cpp), native_sites_table.inc. Печатает покрытие
union всех захватов и пробелы каталога. GUI/string site — наблюдение, не гейт.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HARNESS = os.path.join(ROOT, "build", "ldhook", "bdd0_harness_trace.exe")

CALL_RE = re.compile(r"^  call #\d+ (leaf|native) va=(0x[0-9a-f]+) site=(0x[0-9a-f]+)")
BRANCH_RE = re.compile(r"^  branch \[(.*?)\] (TAKEN|NOT-TAKEN)")
LABEL_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def catalog():
    text = open(os.path.join(ROOT, "tools", "ldhook", "battle", "tick.cpp"),
               encoding="utf-8", errors="replace").read()
    block = re.search(r"const char \*b0_label_names\[\] = \{(.*?)\n\};", text, re.S)
    labels = set(t for t in re.findall(r'"([^"]+)"', block.group(1))
                 if LABEL_RE.match(t))

    out = subprocess.run(["python3", "tools/probes/bdd0_label_trace.py", "--check"],
                         capture_output=True, cwd=ROOT).stdout.decode("utf-8", "replace")
    conds = set(m.group(1) for m in
                re.finditer(r"^  cond \d+: B0_TST\(\d+, \((.*)\)\)$", out, re.M))
    exits = set(m.group(1) for m in
                re.finditer(r"^  exit \d+ \(line \d+\): (\S+)$", out, re.M))

    leaves = set(int(m.group(1), 16) for m in
                 re.finditer(r"LS_SITE\(\d+,\s*(0x[0-9a-f]+)",
                             open(os.path.join(ROOT, "tools", "ldhook", "battle",
                                               "leaf_sites.cpp"),
                                  encoding="utf-8", errors="replace").read()))
    sites = set(int(m.group(1), 16) for m in
                re.finditer(r"^\s*\{ (0x[0-9a-f]+)u,",
                            open(os.path.join(ROOT, "tools", "ldhook", "battle",
                                              "native_sites_table.inc"),
                                encoding="utf-8", errors="replace").read(), re.M))
    return {"labels": labels, "conds": conds, "exits": exits,
            "leaves": leaves, "sites": sites}


def collect(cap):
    p = subprocess.run([HARNESS, "--impl", "port", "--trace-labels", cap],
                       capture_output=True)
    text = (p.stdout + p.stderr).decode("utf-8", "replace")
    cov = {"labels": set(), "dirs": set(), "exits": set(),
           "leaves": set(), "sites": set(), "rc": p.returncode}
    for ln in text.splitlines():
        m = re.match(r"^  chain:(.*)$", ln)
        if m:
            for tok in m.group(1).split(">")[1:]:
                tok = tok.strip()
                if tok.startswith("exit:"):
                    cov["exits"].add(tok[5:])
                elif tok:
                    cov["labels"].add(tok)
            continue
        m = BRANCH_RE.match(ln)
        if m:
            cov["dirs"].add((m.group(1), m.group(2)))
            continue
        m = CALL_RE.match(ln)
        if m:
            site = int(m.group(3), 16)
            (cov["leaves"] if m.group(1) == "leaf" else cov["sites"]).add(site)
    return cov


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: bdd0_coverage.py <capture.jsonl> [...]")
    cat = catalog()
    cov = {"labels": set(), "dirs": set(), "exits": set(),
           "leaves": set(), "sites": set()}
    for cap in sys.argv[1:]:
        c = collect(cap)
        for k in ("labels", "dirs", "exits", "leaves", "sites"):
            cov[k] |= c[k]

    evaled = set(d[0] for d in cov["dirs"])
    one_sided = [d for d in sorted(cov["dirs"])
                 if (d[0], "TAKEN") in cov["dirs"] and
                    (d[0], "NOT-TAKEN") not in cov["dirs"]]

    print("метки     : %d/%d" % (len(cov["labels"] & cat["labels"]), len(cat["labels"])))
    for x in sorted(cat["labels"] - cov["labels"]):
        print("   MISSING label %s" % x)
    print("выходы    : %d/%d" % (len(cov["exits"] & cat["exits"]), len(cat["exits"])))
    for x in sorted(cat["exits"] - cov["exits"]):
        print("   MISSING exit %s" % x)
    print("условия   : %d/%d evaluated, %d one-sided" %
          (len(evaled & cat["conds"]), len(cat["conds"]), len(one_sided)))
    for x in sorted(cat["conds"] - evaled):
        print("   MISSING cond %s" % x)
    for d in one_sided:
        print("   ONE-SIDED [%s] %s" % (d[1], d[0]))
    print("лист-сайты: %d/%d" % (len(cov["leaves"] & cat["leaves"]), len(cat["leaves"])))
    for x in sorted(cat["leaves"] - cov["leaves"]):
        print("   MISSING leaf site 0x%06x" % x)
    print("RECORD-сайты: %d/%d" % (len(cov["sites"] & cat["sites"]), len(cat["sites"])))
    for x in sorted(cat["sites"] - cov["sites"]):
        print("   MISSING record site 0x%06x" % x)


if __name__ == "__main__":
    main()
