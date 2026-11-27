#!/usr/bin/env python3
"""tick_stats.py — статистика РЕАЛЬНОГО боя по BATTLE-трассе (LDHOOK_BATTLE_TRACE=1).

Зачем: синтетический сценарий (`battle_169.ps1`) не воспроизводит настоящий бой.
Этот скрипт разбирает лог ручной игры и отвечает на два вопроса:

  1. сколько времени занимает тик юнит-AI (`45eee0`) и как часто он длинный;
  2. какая доля тика уходит в поиск цели `e0a0` (`e0n` вызовов, `e0us` µs).

Поля BATTLE (trace.cpp, H1/H2/H3): `tick unit code ... us e0n e0us`.
`us` — весь тик юнита, `e0us` — сумма µs вызовов e0a0 внутри этого тика
(аккумулятор всегда включён при trace, без QPC-накладных).

Сессии разделяются по сбросу `tick` (каждый запуск игры начинает с 1) и по
разрыву меток времени > 5 мин.

Запуск:
  python3 tools/probes/tick_stats.py "D:/Games/LethalDreams/hdlog.txt" [hdlog.txt.1 ...]
  python3 tools/probes/tick_stats.py LOG --top 15 --json out.json
"""
import argparse
import json
import re
import statistics
import sys

LINE_RE = re.compile(
    r"ldhook: (\d{2}):(\d{2}):(\d{2})\.(\d{3}) .*? BATTLE tick=(\d+) unit=(-?\d+) "
    r"code=(-?\d+) .*? i1=(-?\d+) .*? vis=(-?\d+) .*? us=(-?\d+)"
    r"(?: e0n=(\d+) e0us=(-?\d+))?")
CAP_RE = re.compile(
    r"E0CAP sum maxiter=(\d+) maxus=(\d+) calls=(\d+) capped_it=(\d+) "
    r"capped_us=(\d+) .*?code1=(\d+) code3=(\d+)")
SEC_RE = re.compile(
    r"TICKSEC sec=(-?\d+) n=(\d+) us_sum=(\d+) us_max=(\d+) e0_sum=(\d+) "
    r"e0_max=(\d+) over10=(\d+) over20=(\d+) over50=(\d+) code1=(\d+) "
    r"code3=(\d+)")
GAP_S = 300.0


def pct(vals, p):
    if not vals:
        return 0.0
    s = sorted(vals)
    k = (len(s) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def parse(paths):
    """-> список сессий: [{ticks:[{t,unit,code,i1,vis,us,e0n,e0us}], caps:[...]}]"""
    sessions = []
    cur = None
    last_ts = None
    last_tick = None
    for path in paths:
        try:
            fh = open(path, "r", encoding="latin-1", errors="replace")
        except IOError as e:
            print("не читается: %s (%s)" % (path, e), file=sys.stderr)
            continue
        with fh:
            for line in fh:
                if " BATTLE tick=" in line:
                    m = LINE_RE.search(line)
                    if m is None:
                        continue
                    h, mi, s, ms = (int(m.group(i)) for i in (1, 2, 3, 4))
                    ts = h * 3600.0 + mi * 60.0 + s + ms / 1000.0
                    tick = int(m.group(5))
                    rec = {
                        "t": tick,
                        "unit": int(m.group(6)),
                        "code": int(m.group(7)),
                        "i1": int(m.group(8)),
                        "vis": int(m.group(9)),
                        "us": int(m.group(10)),
                        "e0n": int(m.group(11)) if m.group(11) else None,
                        "e0us": int(m.group(12)) if m.group(12) else None,
                    }
                    new_session = (cur is None or tick <= (last_tick or 0) or
                                   (last_ts is not None and
                                    (ts - last_ts) % 86400.0 > GAP_S))
                    if new_session:
                        cur = {"ticks": [], "caps": [], "src": path,
                               "t0": ts, "t1": ts}
                        sessions.append(cur)
                    cur["ticks"].append(rec)
                    cur["t1"] = ts
                    last_ts = ts
                    last_tick = tick
                elif "E0CAP sum" in line:
                    m = CAP_RE.search(line)
                    if m is not None and cur is not None:
                        cur["caps"].append({
                            "maxiter": int(m.group(1)), "maxus": int(m.group(2)),
                            "calls": int(m.group(3)), "capped_it": int(m.group(4)),
                            "capped_us": int(m.group(5)), "code1": int(m.group(6)),
                            "code3": int(m.group(7))})
    return sessions


def parse_secs(paths):
    """TICKSEC-строки -> прогоны (разрыв > 60 с = новый прогон)."""
    rows = []
    for path in paths:
        try:
            fh = open(path, "r", encoding="latin-1", errors="replace")
        except IOError:
            continue
        with fh:
            for line in fh:
                if "TICKSEC sec=" not in line:
                    continue
                m = SEC_RE.search(line)
                if m is None:
                    continue
                keys = ["sec", "n", "us_sum", "us_max", "e0_sum", "e0_max",
                        "over10", "over20", "over50", "code1", "code3"]
                rows.append(dict(zip(keys, (int(m.group(i)) for i in
                                             range(1, 12)))))
    runs, cur = [], []
    for r in rows:
        if cur and r["sec"] - cur[-1]["sec"] > 60:
            runs.append(cur)
            cur = []
        cur.append(r)
    if cur:
        runs.append(cur)
    return runs


def report_secs(run, top, idx):
    if not run:
        return None
    span = run[-1]["sec"] - run[0]["sec"] + 1
    ticks = sum(r["n"] for r in run)
    busy = [100.0 * r["us_sum"] / 1e6 for r in run]      # % одной секунды
    out = {
        "run": idx, "span_s": span, "ticks": ticks,
        "ticks_per_s": round(ticks / float(span), 1),
        "busy_avg_pct": round(100.0 * sum(r["us_sum"] for r in run) /
                              (span * 1e6), 1),
        "busy_med_pct": pct(busy, 50), "busy_p90_pct": pct(busy, 90),
        "busy_p99_pct": pct(busy, 99), "busy_max_pct": max(busy),
        "over10": sum(r["over10"] for r in run),
        "over20": sum(r["over20"] for r in run),
        "over50": sum(r["over50"] for r in run),
        "secs_with_over20": sum(1 for r in run if r["over20"] > 0),
        "secs": len(run),
        "top": sorted(
            ({"sec": r["sec"], "n": r["n"],
              "busy": round(100.0 * r["us_sum"] / 1e6, 1),
              "us_max": r["us_max"], "e0_sum": r["e0_sum"],
              "over20": r["over20"]} for r in run),
            key=lambda r: -r["busy"])[:top],
    }
    print("== секундные сводки %d: %d с, тиков %d (%.1f/с), AI занято "
          "в среднем %.1f %% (медиана %.1f, p90 %.1f, p99 %.1f, max %.1f)"
          % (idx, span, ticks, out["ticks_per_s"], out["busy_avg_pct"],
             out["busy_med_pct"], out["busy_p90_pct"], out["busy_p99_pct"],
             out["busy_max_pct"]))
    print("   длинных тиков: >10ms %d, >20ms %d, >50ms %d; секунд с >20ms: "
          "%d из %d" % (out["over10"], out["over20"], out["over50"],
                        out["secs_with_over20"], out["secs"]))
    print("   худшие секунды:")
    for r in out["top"]:
        print("     sec=%-6d тиков=%-5d занято=%-6.1f%% max_us=%-7d "
              "e0_sum=%-8d >20ms=%d"
              % (r["sec"], r["n"], r["busy"], r["us_max"], r["e0_sum"],
                 r["over20"]))
    print()
    return out


def report(sess, top, idx):
    t = sess["ticks"]
    if not t:
        return None
    us = [r["us"] for r in t if r["us"] >= 0]
    wall = sess["t1"] - sess["t0"]
    if wall < 0:
        wall += 86400.0
    has_e0 = any(r["e0us"] is not None for r in t)
    e0 = [r["e0us"] for r in t if r["e0us"] is not None]
    e0n = [r["e0n"] for r in t if r["e0n"] is not None]
    codes = {}
    for r in t:
        codes[r["code"]] = codes.get(r["code"], 0) + 1
    out = {
        "session": idx, "src": sess["src"], "ticks": len(t),
        "wall_s": round(wall, 1),
        "ticks_per_s": round(len(t) / wall, 1) if wall > 0 else None,
        "units": len(set(r["unit"] for r in t)),
        "us_med": pct(us, 50), "us_p90": pct(us, 90), "us_p99": pct(us, 99),
        "us_max": max(us) if us else 0,
        "us_sum_s": round(sum(us) / 1e6, 1),
        "busy_pct": round(100.0 * sum(us) / (wall * 1e6), 1) if wall > 0 else None,
        "over_10ms": sum(1 for v in us if v > 10000),
        "over_20ms": sum(1 for v in us if v > 20000),
        "over_33ms": sum(1 for v in us if v > 33000),
        "over_50ms": sum(1 for v in us if v > 50000),
        "over_100ms": sum(1 for v in us if v > 100000),
        "codes": dict(sorted(codes.items())),
    }
    if has_e0:
        out["e0_calls"] = sum(e0n)
        out["e0_share_pct"] = round(100.0 * sum(e0) / sum(us), 1) if sum(us) else None
        out["e0_us_med"] = pct([v for v in e0 if v > 0], 50)
        out["e0_us_p90"] = pct([v for v in e0 if v > 0], 90)
        out["e0_us_max"] = max(e0) if e0 else 0
        out["e0_ticks_pct"] = round(100.0 * sum(1 for v in e0n if v > 0) / len(t), 1)
    if sess["caps"]:
        out["cap"] = sess["caps"][-1]
    out["top"] = sorted(
        ({"t": r["t"], "unit": r["unit"], "code": r["code"], "i1": r["i1"],
          "vis": r["vis"], "us": r["us"], "e0n": r["e0n"], "e0us": r["e0us"]}
         for r in t), key=lambda r: -r["us"])[:top]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--top", type=int, default=10)
    ap.add_argument("--json", default=None)
    a = ap.parse_args()

    sessions = parse(a.logs)
    if not sessions and not parse_secs(a.logs):
        print("нет BATTLE/TICKSEC-строк (нужен LDHOOK_BATTLE_TRACE=1|2)",
              file=sys.stderr)
        return 2

    reps = []
    for i, s in enumerate(sessions, 1):
        r = report(s, a.top, i)
        if r is None:
            continue
        reps.append(r)
        print("== сессия %d (%s): %d тиков, %.1f с, %.1f тиков/с, юнитов %d"
              % (r["session"], r["src"].split("/")[-1], r["ticks"], r["wall_s"],
                 r["ticks_per_s"] or 0, r["units"]))
        print("   тик: медиана %.0f µs, p90 %.0f, p99 %.0f, max %.0f; "
              "занято AI %.1f %% (%.1f с)"
              % (r["us_med"], r["us_p90"], r["us_p99"], r["us_max"],
                 r["busy_pct"] or 0, r["us_sum_s"]))
        print("   длинных: >10ms %d, >20ms %d, >33ms %d, >50ms %d, >100ms %d"
              % (r["over_10ms"], r["over_20ms"], r["over_33ms"],
                 r["over_50ms"], r["over_100ms"]))
        print("   коды: %s" % ", ".join("%d=%d" % kv for kv in r["codes"].items()))
        if "e0_share_pct" in r:
            print("   e0a0: вызовов %d, доля тика %.1f %%, медиана %.0f µs, "
                  "p90 %.0f, max %.0f; тиков с поиском %.1f %%"
                  % (r["e0_calls"], r["e0_share_pct"], r["e0_us_med"],
                     r["e0_us_p90"], r["e0_us_max"], r["e0_ticks_pct"]))
        if "cap" in r:
            c = r["cap"]
            print("   E0CAP: maxiter=%d maxus=%d calls=%d capped_it=%d "
                  "capped_us=%d code1=%.1f %% code3=%.1f %%"
                  % (c["maxiter"], c["maxus"], c["calls"], c["capped_it"],
                     c["capped_us"],
                     100.0 * c["code1"] / c["calls"] if c["calls"] else 0,
                     100.0 * c["code3"] / c["calls"] if c["calls"] else 0))
        print("   самые длинные тики:")
        for r2 in r["top"]:
            print("     tick=%-6d unit=%-4d code=%-2d i1=%-5d vis=%-5d "
                  "us=%-7d e0n=%s e0us=%s"
                  % (r2["t"], r2["unit"], r2["code"], r2["i1"], r2["vis"],
                     r2["us"], r2["e0n"], r2["e0us"]))
        print()

    secs = []
    for i, run in enumerate(parse_secs(a.logs), 1):
        r = report_secs(run, a.top, i)
        if r is not None:
            secs.append(r)

    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"ticks": reps, "seconds": secs}, fh, indent=1)
        print("json: %s" % a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
