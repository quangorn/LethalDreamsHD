#!/usr/bin/env python3
"""e0diag_report.py — сводка NATIVE-vs-PORT по строкам E0DIAG из логов прогонов.

Вход — файлы логов (имя содержит arm: native / e0a0|port). Разбираются:
  натив:  E0DIAG call=.. iters distinct null_returns code map1 reg lb grid
          gridnull pfsc pfsnull rep ovf f3 f1 mchg tick arm=native (все long-вызовы,
          кап 400 строк на процесс)
  порт:   E0DIAG call=.. iters distinct rep null_returns code map1 f3 f1 mchg
          msame ovf reg   — кап 120 строк (emit + 3 dump) = первые 30 emit
          E0DIAG2 call=.. lb nbrnull dup cellnull pathnull pfsnull — парный, кап
          НЕ трогает: long-вызовов посчитано полное число.
Сопоставление полевых сумок по вызовам: поля iters/reg/rep/f1/f3/mchg/ovf —
только из E0DIAG (cap 30); lb/nbrnull/dup/cellnull/pathnull/pfsnull — по всем
long-вызовам. Итог печатает доли на итерацию по покрытым вызовам и объявляет
покрытие.

Запуск:
  python3 tools/ldhook/e0diag_report.py LOG1 LOG2 [...]
"""
import re
import sys
from collections import defaultdict

N_RE = re.compile(
    r"E0DIAG call=(\d+) iters=(\d+) distinct=(\d+) null_returns=(\d+) "
    r"code=(\d+) map1=(-?\d+)->(-?\d+) reg=(\d+) lb=(\d+) grid=(\d+) "
    r"gridnull=(\d+) pfsc=(\d+) pfsnull=(\d+) rep=(\d+) ovf=(\d+) "
    r"f3=(\d+) f1=(\d+) mchg=(\d+) tick=(\d+) arm=native")
N_KEYS = ["call", "iters", "distinct", "pathnull", "code", "map1_0", "map1_1",
          "reg", "lb", "grid", "gridnull", "pfsc", "pfsnull", "rep", "ovf",
          "f3", "f1", "mchg", "tick"]
P_RE = re.compile(
    r"E0DIAG call=(\d+) iters=(\d+) distinct=(\d+) rep=(\d+) null_returns=(\d+) "
    r"code=(\d+) map1=(-?\d+)->(-?\d+) f3=(\d+) f1=(\d+) mchg=(\d+) "
    r"msame=(\d+) ovf=(\d+) reg=(\d+)")
P_KEYS = ["call", "iters", "distinct", "rep", "pathnull", "code", "map1_0",
          "map1_1", "f3", "f1", "mchg", "msame", "ovf", "reg"]
P2_RE = re.compile(
    r"E0DIAG2 call=(\d+) lb=(\d+) nbrnull=(\d+) dup=(\d+) cellnull=(\d+) "
    r"pathnull=(\d+) pfsnull=(\d+) tick=(\d+) arm=port")
P2_KEYS = ["call", "lb", "nbrnull", "dup", "cellnull", "pathnull", "pfsnull",
           "tick"]

# Поля для сравнения. gridnull (натив) == cellnull (порт); pathnull — общий.
SUM_KEYS = ["iters", "distinct", "reg", "lb", "grid", "pfsc", "gridnull",
            "pfsnull", "pathnull", "f1", "f3", "rep", "mchg", "ovf",
            "nbrnull", "dup"]


def parse(path):
    """-> списки записей {поле: int}, каждая = один long-вызов."""
    nat = []
    p1, p2 = {}, {}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = N_RE.search(line)
        if m:
            nat.append(dict(zip(N_KEYS, map(int, m.groups()))))
            continue
        m = P_RE.search(line)
        if m:
            p1[int(m.group(1))] = dict(zip(P_KEYS, map(int, m.groups())))
            continue
        m = P2_RE.search(line)
        if m:
            p2[int(m.group(1))] = dict(zip(P2_KEYS, map(int, m.groups())))
    # порт: long-вызов = строка E0DIAG2; поля E0DIAG (cap) подтягиваются по call
    port = []
    for call, a in sorted(p2.items()):
        rec = dict(a)
        b = p1.get(call)
        if b:
            for k in ("iters", "distinct", "reg", "rep", "pathnull", "f3",
                      "f1", "mchg", "ovf", "code"):
                rec[k] = b[k]
        rec["gridnull"] = rec.get("cellnull", 0)   # тот же гейт: cell==NULL
        port.append(rec)
    return nat, port


def agg(calls):
    s = defaultdict(int)
    cov = defaultdict(int)
    codes = defaultdict(int)
    for c in calls:
        for k in SUM_KEYS:
            if k in c:
                s[k] += c[k]
                cov[k] += 1
        if "code" in c:
            codes[c["code"]] += 1
    s["calls"] = len(calls)
    s["codes"] = dict(codes)
    s["cov"] = dict(cov)
    return s


def rate(s, key, denom):
    d = s.get(denom, 0)
    return (s.get(key, 0) / d) if d else 0.0


def fmt_arm(tag, s):
    n = max(s["calls"], 1)
    it = s.get("iters", 0)
    print("%s long=%d iters/call=%.1f" % (tag, s["calls"], it / n))
    for k in ("reg", "lb", "grid", "pfsc", "f1", "f3", "rep", "mchg",
              "gridnull", "pfsnull", "pathnull", "nbrnull", "dup"):
        c = s["cov"].get(k, 0)
        if c == 0:
            print("  %-10s : —" % k)
        else:
            print("  %-10s %.4f/it sum=%d cov=%d" % (k, s[k] / it, s[k], c))
    print("  codes: %s" % (s["codes"],))


def main():
    runs = []
    for path in sys.argv[1:]:
        base = path.replace("\\", "/").split("/")[-1]
        nat, port = parse(path)
        if nat:
            runs.append(("%-21s native" % base, agg(nat)))
        if port:
            runs.append(("%-21s port  " % base, agg(port)))
    for tag, s in runs:
        fmt_arm(tag, s)
    print("итог по всем прогонам:")
    nall, pall = defaultdict(int), defaultdict(int)
    ncov, pcov = defaultdict(int), defaultdict(int)
    for tag, s in runs:
        tgt, cov = (nall, ncov) if tag.endswith("native") else (pall, pcov)
        for k in SUM_KEYS:
            tgt[k] += s.get(k, 0)
            cov[k] += s["cov"].get(k, 0)
        tgt["iters"] += s.get("iters", 0)
        tgt["calls"] += s["calls"]
        tgt["codes"] = {}
    nall["cov"] = dict(ncov)
    pall["cov"] = dict(pcov)
    fmt_arm("%-21s native" % "ALL", nall)
    fmt_arm("%-21s port  " % "ALL", pall)


if __name__ == "__main__":
    main()
