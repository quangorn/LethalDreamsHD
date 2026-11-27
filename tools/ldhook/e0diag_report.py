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
E0COST/E0COSTSUM (план 9.d2d) — разложение µs вызова e0a0: us_lb ⊆ us_path ⊆
us_update, доли scan/(update−path)/path/other дизъюнктны, lb — subset и печатается
отдельно; `bad=1` — строка нарушившая самосогласованность; `depth` в E0COSTSUM =
вложенность s6_ai_update (0 = все выходы сбалансированы).

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

# E0COST (план 9.d2d) — разложение µs одного вызова e0a0; кап 400 строк.
# ПРАВИЛО ПЕРЕСЕЧЕНИЯ: us_lb ⊆ us_path ⊆ us_update. us_lb считается и в
# s6_ai_update (map2 lower_bound), и внутри s6_path_search; us_path — время,
# проведённый внутри s6_path_search, то есть часть us_update. Поэтому доли
# scan / (update − path) / path / other — дизъюнктны и складываются в 100 %,
# а lb — subset, печатается отдельной долей и в сумму НЕ входит.
COST_RE = re.compile(
    r"E0COST call=(\d+) filter=(-?\d+) iters=(\d+) code=(-?\d+) capped=(\d+) us_total=(\d+) "
    r"us_scan=(\d+) us_update=(\d+) us_path=(\d+) us_lb=(\d+) nodes_scanned=(\d+) "
    r"live_before=(-?\d+) live_after=(-?\d+) b2=(\d+) map1=(-?\d+)->(-?\d+) "
    r"tick=(\d+) arm=port( bad=1)?")
COST_KEYS = ["call", "filter", "iters", "code", "capped", "us_total", "us_scan",
             "us_update", "us_path", "us_lb", "nodes", "live_before",
             "live_after", "b2", "map1_0", "map1_1", "tick"]
# Формат до 9.1c (без capped=): старые логи читаются как capped=0.
COST_RE_OLD = re.compile(
    r"E0COST call=(\d+) filter=(-?\d+) iters=(\d+) code=(-?\d+) us_total=(\d+) "
    r"us_scan=(\d+) us_update=(\d+) us_path=(\d+) us_lb=(\d+) nodes_scanned=(\d+) "
    r"live_before=(-?\d+) live_after=(-?\d+) b2=(\d+) map1=(-?\d+)->(-?\d+) "
    r"tick=(\d+) arm=port( bad=1)?")
COST_KEYS_OLD = ["call", "filter", "iters", "code", "us_total", "us_scan",
                 "us_update", "us_path", "us_lb", "nodes", "live_before",
                 "live_after", "b2", "map1_0", "map1_1", "tick"]
COSTSUM_RE = re.compile(
    r"E0COSTSUM tick=(\d+) call=(\d+) filter=(-?\d+) iters=(\d+) code=(-?\d+) "
    r"calls=(\d+) iters_sum=(\d+) us=(\d+) scan=(\d+) update=(\d+) path=(\d+) "
    r"lb=(\d+) nodes=(\d+) live_after=(\d+) b2=(\d+) map1=(-?\d+)->(-?\d+) "
    r"bad=(\d+) depth=(\d+) arm=port")
COSTSUM_KEYS = ["tick", "call", "filter", "iters", "code", "calls", "iters_sum",
                "us", "scan", "update", "path", "lb", "nodes", "live_after",
                "b2", "map1_0", "map1_1", "bad", "depth"]

# Поля для сравнения. gridnull (натив) == cellnull (порт); pathnull — общий.
SUM_KEYS = ["iters", "distinct", "reg", "lb", "grid", "pfsc", "gridnull",
            "pfsnull", "pathnull", "f1", "f3", "rep", "mchg", "ovf",
            "nbrnull", "dup"]


def parse(path):
    """-> списки записей {поле: int}, каждая = один long-вызов."""
    nat = []
    p1, p2 = {}, {}
    cost, sums = [], []
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
            continue
        m = COST_RE.search(line)
        if m:
            vals = list(map(int, m.groups()[:len(COST_KEYS)]))
            rec = dict(zip(COST_KEYS, vals))
            rec["bad"] = 1 if m.group(len(COST_KEYS) + 1) else 0
            cost.append(rec)
            continue
        m = COST_RE_OLD.search(line)
        if m:
            vals = list(map(int, m.groups()[:len(COST_KEYS_OLD)]))
            rec = dict(zip(COST_KEYS_OLD, vals))
            rec["capped"] = 0
            rec["bad"] = 1 if m.group(len(COST_KEYS_OLD) + 1) else 0
            cost.append(rec)
            continue
        m = COSTSUM_RE.search(line)
        if m:
            sums.append(dict(zip(COSTSUM_KEYS, map(int, m.groups()))))
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
    return nat, port, cost, sums


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


def cost_split(calls):
    """µs-взвешенное разложение; us_lb — subset (см. правило выше), доля печатается
    отдельно и в сумму не входит. Возвращает доли и поузловые средние."""
    t = sum(c["us_total"] for c in calls)
    if t == 0:
        return None
    scan = sum(c["us_scan"] for c in calls)
    upd = sum(c["us_update"] for c in calls)
    path = sum(c["us_path"] for c in calls)
    lb = sum(c["us_lb"] for c in calls)
    it = sum(c["iters"] for c in calls)
    return {"n": len(calls), "us": t,
            "scan_pct": 100.0 * scan / t,
            "upd_only_pct": 100.0 * (upd - path) / t,
            "path_pct": 100.0 * path / t,
            "other_pct": 100.0 * (t - scan - upd) / t,
            "lb_pct": 100.0 * lb / t,
            "nodes_per_iter": sum(c["nodes"] for c in calls) / float(it),
            "scan_us_per_iter": scan / float(it),
            "mean_map1_end": sum(c["map1_1"] for c in calls) / len(calls),
            "mean_live_after": sum(c["live_after"] for c in calls) / len(calls),
            "bad": sum(c["bad"] for c in calls),
            "b2": sum(c["b2"] for c in calls)}


def fmt_cost(base, calls, sums):
    """Сводка E0COST/E0COSTSUM одного лога (plan 9.2d)."""
    print("E0COST %s: строк=%d" % (base, len(calls)))
    for label, group in (("long code=3", [c for c in calls if c["iters"] > 1000
                                          and c["code"] == 3]),
                         ("all E0COST lines", calls)):
        if not group:
            continue
        a = cost_split(group)
        print("  %-16s n=%d capped=%d scan=%.1f%% update-minus-path=%.1f%% path=%.1f%% "
              "other=%.1f%%  lb=%.1f%% of total  nodes/iter=%.2f "
              "scan_us/iter=%.2f mean_map1=%.1f mean_live_after=%.1f bad=%d b2=%d"
              % (label, a["n"], sum(c["capped"] for c in group), a["scan_pct"],
                 a["upd_only_pct"], a["path_pct"],
                 a["other_pct"], a["lb_pct"], a["nodes_per_iter"],
                 a["scan_us_per_iter"], a["mean_map1_end"],
                 a["mean_live_after"], a["bad"], a["b2"]))
    for s in sums:
        print("  E0COSTSUM call=%d calls=%d us=%d scan=%d update=%d path=%d lb=%d "
              "nodes=%d bad=%d depth=%d" %
              (s["call"], s["calls"], s["us"], s["scan"], s["update"], s["path"],
               s["lb"], s["nodes"], s["bad"], s["depth"]))


def main():
    runs = []
    for path in sys.argv[1:]:
        base = path.replace("\\", "/").split("/")[-1]
        nat, port, cost, sums = parse(path)
        if cost or sums:
            fmt_cost(base, cost, sums)
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
