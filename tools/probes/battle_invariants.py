#!/usr/bin/env python3
"""battle_invariants.py — офлайн-инварианты боевой трассы (PLAN_BATTLE_AI_CPP.md, 9.d1).

Read-only агрегатор BATTLE/STATEFP-строк живого прогона (LDHOOK_BATTLE_TRACE=1);
поля/sentinel'ы — `tools/ldhook/battle/trace.h`; игра не трогается. Несколько
логов — один поток; порядок строк = порядок файлов, для юнита chronological =
порядок файлов.

Юнит судится по **активным** тикам (`code != -1`): `-1` = ранний выход R1/R2,
тик не дошёл до 45eee0; `code=0` — легальный ответ `anchor == NULL`
(`battle/move.cpp` @0x45f379). rc: 0 — нет fail (в baseline-режиме — нет НОВЫХ
fail) и окно достаточно, есть проверенный объект; 1 — есть fail/новый fail;
2 — файл не читается / нет BATTLE-строк / плохие аргументы; 3 — окно
недостаточно (тики/юниты ниже минимума), судить нельзя.

Гейт переписывания — `--baseline tools/probes/battle_invariants_baseline.json`
(нативный эталон сцены `battle_169.ps1`, 26.11.2026: 1131 тик, 12 юнитов,
runaway у юнитов 4/10/12, залипание у 4/5/6): принимается прогон без НОВЫХ
провалов. Ключи юнит-привязанные (`unit:4:i1`, `unit:5:stall`) — тики между
прогонами плывут (таймаут `c5a0` — wall-clock), tick-bound ключи давали бы
ложные NEW FAIL на том же дефекте. Пересъёмка эталона — тот же прогон +
`--json tools/probes/battle_invariants_baseline.json`.

Профиль (`--profile`, план 9.d2): лаг = **спин до wall-clock таймаута** `45c5a0`
(`code=3`) — ~4500 итераций × полный скан `map1`, выход только потому, что
время кончилось. `vis` (входы `45dd70`) — прямая метрика работы тика, `us`
(QPC) — его длительность. `latency` ловит симптом (длинный тик), `spin` —
механизм (отказ по таймауту после ≥1000 визитов): спин бьёт лаг и на быстрой
машине, поэтому он отдельная проверка.
"""
import json
import sys
from collections import Counter, OrderedDict

BATTLE_FIELDS = [
    "tick", "unit", "code", "u170", "al", "out",
    "c1", "x1", "y1", "i1", "fx", "fy", "lx", "ly", "c2", "x2", "y2", "i2",
    "g", "h10k", "cost", "trk", "bd2", "s1", "s2", "vis", "m0", "m1", "m2",
    "us", "ai"]

SENT = "ffffffff"                     # sentinel float-битов m0..m2 и bd2 (%08x)
CODE_OK = {-1, 0, 1, 2, 3, 4}         # -1 = тик не дошёл до 45eee0; 0 = anchor NULL
AL_OK, OUT_OK, U170_OK = {0, 1}, {-1, 0, 1}, {0, 1, 2, 3, 4}
# Цель/путь acquired: 1 — узел совпал, 2 — цель сместилась, 4 — сработал
# дистанционный гейт 45c880 (кандидат найден, якорь сброшен). Код 2 в нативе
# не наблюдается, но контракт 45eee0 его допускает; 0 — anchor==NULL (путь не
# найден) и в это множество не входит — его ловит check_no_path.
SELECT_CODES = {1, 2, 4}
NO_PATH_CODE = 0                      # 45eee0: anchor == NULL
DETAIL_LIMIT = 5

USAGE = ("usage: battle_invariants.py <log.txt> [<log2.txt> ...] "
         "[--json out.json] [--baseline ref.json] [--profile] [--top N] "
         "[--min-ticks N] [--min-units N] [--min-moving N] "
         "[--max-iterations N] [--max-tick-us N] [--spin-vis N] "
         "[--unit-ticks K] [--stall-ticks N] [--allow-idle]")

NUMOPTS = {"--min-ticks": "min_ticks", "--min-units": "min_units",
           "--min-moving": "min_moving", "--max-iterations": "max_iter",
           "--unit-ticks": "unit_ticks", "--stall-ticks": "stall_ticks",
           "--max-tick-us": "max_tick_us", "--spin-vis": "spin_vis",
           "--top": "top"}


def to_int(s):
    try:
        return int(s)
    except (TypeError, ValueError):
        return None


def tokens(rest):
    """BATTLE/STATEFP-хвост строки -> {field: raw}; только ключ=значение."""
    return {k: v for k, v in (tok.split("=", 1) for tok in rest.split()
                              if "=" in tok)}


def parse(files):
    """Один поток BATTLE/STATEFP-записей из всех файлов (порядок файлов)."""
    battle, statefp = [], []
    for path in files:
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    i = line.find(" BATTLE ")
                    if i >= 0:
                        battle.append(tokens(line[i + len(" BATTLE "):]))
                        continue
                    j = line.find(" STATEFP ")
                    if j >= 0:
                        statefp.append(tokens(line[j + len(" STATEFP "):]))
        except OSError as exc:
            print("unreadable: %s (%s)" % (path, exc))
            raise SystemExit(2)
    return battle, statefp


def point(rec):
    """Точка, отчитанная в тике; None = точка не отчитана.

    pos-тройка unit+0xa4..0xac (m0,m1,m2) — float-биты; m1 в записях
    0x00000000 (float 0.0, легальное значение, не sentinel). При c2==1
    (вызов e0a0 №2) точкой считается x2/y2.
    """
    parts = [rec.get(k) for k in ("m0", "m1", "m2")]
    if any(p is not None and p != SENT for p in parts):
        return ("pos",) + tuple(parts)
    if rec.get("c2") == "1":
        return ("x2y2", rec.get("x2"), rec.get("y2"))
    return None


def active(rs):
    """Тики, дошедшие до 45eee0 (code != -1)."""
    return [r for r in rs if to_int(r.get("code")) != -1]


def by_unit(battle):
    """unit -> список записей в порядке файлов."""
    per = OrderedDict()
    for r in battle:
        u = r.get("unit")
        if u is not None:
            per.setdefault(u, []).append(r)
    return per


class Check:
    """Счётчики проверок; fail_keys — ключи для baseline-диффа.

    Ключ построчных проверок: `tick:unit:field`. Ключ пер-юнитных проверок:
    `unit:field` без тика — номер тика между прогонами легально сдвигается,
    и с тиком один и тот же залипший юнит в rewrite дал бы ложный NEW FAIL.
    """

    def __init__(self, name):
        self.name, self.passed, self.skipped, self.failed = name, 0, 0, 0
        self.details, self.fail_keys, self.reasons = [], [], OrderedDict()

    def p(self):
        self.passed += 1

    def skip(self, reason):
        self.skipped += 1
        self.reasons[reason] = self.reasons.get(reason, 0) + 1

    def fail(self, detail, key):
        self.failed += 1
        self.fail_keys.append(key)
        if len(self.details) < DETAIL_LIMIT:
            self.details.append(detail)


def check_window(count, minimum, name, fmt):
    c = Check(name)
    if count >= minimum:
        c.passed = count
        return c, False
    c.skip(fmt % (count, minimum))
    return c, True


def check_domains(battle, opts):
    domains = [("code", CODE_OK), ("al", AL_OK), ("out", OUT_OK),
               ("u170", U170_OK)]
    c = Check("domains")
    for r in battle:
        missing = [k for k, _ in domains if k not in r]
        if missing:
            c.skip("tick=%s unit=%s fields missing: %s"
                   % (r.get("tick"), r.get("unit"), ",".join(missing)))
            continue
        t, u = r.get("tick"), r.get("unit")
        bad = [k for k, ok in domains if to_int(r[k]) not in ok]
        for k in bad:
            c.fail("tick=%s unit=%s %s=%s" % (t, u, k, r[k]),
                   "%s:%s:%s" % (t, u, k))
        if not bad:
            c.p()
    return c


def judged(u, rs, opts, c):
    """True = юнит судим по активным тикам; иначе SKIP."""
    n = len(active(rs))
    if n >= opts["unit_ticks"]:
        return True
    c.skip("unit=%s active ticks=%d < unit-ticks=%d"
           % (u, n, opts["unit_ticks"]))
    return False


def check_selected(per, opts, summary):
    c = Check("target_selected")
    for u, rs in per.items():
        if not judged(u, rs, opts, c):
            continue
        act = active(rs)
        codes = sorted({to_int(r.get("code")) for r in act} - {None})
        summary[u]["selected"] = any(to_int(r.get("code")) in SELECT_CODES
                                     for r in act)
        if summary[u]["selected"]:
            c.p()
        else:
            c.fail("unit=%s active ticks=%d never selected a target, codes=%s"
                   % (u, len(act), codes), "unit:%s:selected" % u)
    return c


def check_no_path(per, opts, summary):
    """«Путь не найден»: все активные тики юнита вернули code=0 (anchor NULL).

    Отдельно от target_selected: code=4 (дистанционный гейт) — тоже «кандидат
    найден», поэтому юнит с чередованием 0 и 4 не слепой, а нестабильный;
    полностью нулевой юнит — слепой (симптом «не видит врага рядом»).
    """
    c = Check("no_path")
    for u, rs in per.items():
        if not judged(u, rs, opts, c):
            continue
        codes = [to_int(r.get("code")) for r in active(rs)]
        summary[u]["no_path"] = all(v == NO_PATH_CODE for v in codes)
        if summary[u]["no_path"]:
            c.fail("unit=%s active ticks=%d code=0 on every tick "
                   "(anchor NULL => path never found)" % (u, len(codes)),
                   "unit:%s:no_path" % u)
        else:
            c.p()
    return c


def check_movement(per, opts, summary):
    c = Check("movement")
    judged_n = 0
    moving = 0
    for u, rs in per.items():
        if not judged(u, rs, opts, c):
            continue
        judged_n += 1
        act = active(rs)
        pts = set(q for q in (point(r) for r in act) if q is not None)
        summary[u]["points"] = len(pts)
        summary[u]["moving"] = len(pts) >= 2
        if summary[u]["moving"]:
            moving += 1
            c.p()
        else:
            c.fail("unit=%s active ticks=%d distinct target points=%d (need 2)"
                   % (u, len(act), len(pts)), "unit:%s:points" % u)
    if judged_n and moving < opts["min_moving"]:
        c.fail("aggregate: moving units=%d < min-moving=%d (judged=%d)"
               % (moving, opts["min_moving"], judged_n),
               "aggregate:moving:%d" % judged_n)
    if judged_n >= opts["min_units"] and moving == 0:
        c.fail("aggregate: %d units judged, none moved" % judged_n,
               "aggregate:none_moved:%d" % judged_n)
    return c


def check_runaway(battle, opts):
    """Runaway латчей e0a0 (`i1`/`i2`) — прямая сигнатура лагов; -1 не считается."""
    c = Check("runaway")
    for r in battle:
        t, u = r.get("tick"), r.get("unit")
        vals = {k: to_int(r.get(k)) for k in ("i1", "i2")}
        over = [k for k, v in vals.items()
                if v is not None and v > opts["max_iter"]]
        missing = [k for k, v in vals.items() if v is None]
        for k in over:
            # Ключ привязан к юниту, не к тику: живой прогон не воспроизводим
            # (таймаут c5a0 — wall-clock, тики плывут), поэтому tick-bound ключ
            # давал бы ложные NEW FAIL на том же самом дефекте.
            c.fail("tick=%s unit=%s %s=%s limit=%d"
                   % (t, u, k, r[k], opts["max_iter"]), "unit:%s:%s" % (u, k))
        if not over:
            if missing:
                c.skip("tick=%s unit=%s i1/i2 missing" % (t, u))
            else:
                c.p()
    return c


def check_latency(battle, opts):
    """Симптом лага: тик длиннее `--max-tick-us` µs (QPC). Ключ юнит-привязанный."""
    c = Check("latency")
    vals = [to_int(r.get("us")) for r in battle]
    if all(v is None or v < 0 for v in vals):
        c.skip("no us field (trace without QPC)")
        return c
    for r, v in zip(battle, vals):
        t, u = r.get("tick"), r.get("unit")
        if v is None or v < 0:
            c.skip("tick=%s unit=%s us missing/sentinel" % (t, u))
            continue
        if v > opts["max_tick_us"]:
            c.fail("tick=%s unit=%s us=%d vis=%s i1=%s i2=%s code=%s limit=%d"
                   % (t, u, v, r.get("vis"), r.get("i1"), r.get("i2"),
                      r.get("code"), opts["max_tick_us"]),
                   "unit:%s:latency" % u)
        else:
            c.p()
    return c


def check_spin(battle, opts):
    """Механизм лага: code=3 (таймаут `45c5a0`) после >= `--spin-vis` визитов.

    Это и есть генератор лага: спин по ~4500 итерациям × полный скан `map1`.
    Отдельная проверка, потому что на быстрой машине `us` может уложиться в
    порог, а спин (число визитов) остаётся тем же дефектом.
    """
    c = Check("spin")
    for r in battle:
        t, u = r.get("tick"), r.get("unit")
        vis = to_int(r.get("vis"))
        code = to_int(r.get("code"))
        if vis is None:
            c.skip("tick=%s unit=%s vis missing" % (t, u))
        elif code == 3 and vis >= opts["spin_vis"]:
            c.fail("tick=%s unit=%s code=3 vis=%d i1=%s (spin to timeout 45c5a0)"
                   % (t, u, vis, r.get("i1")), "unit:%s:spin" % u)
        else:
            c.p()
    return c


def check_stall(per, opts, summary):
    """Залипание: longest run тиков, где точка движения не изменилась.

    Считается по активным тикам; тик без отчитанной точки = не двигается.
    Калибровка — `baseline_native_2026-11-26.txt`: 189/163/95 у залипших
    (units 5/6/4), ≤20 у здоровых.
    """
    c = Check("stall")
    for u, rs in per.items():
        if not judged(u, rs, opts, c):
            continue
        run = 0
        best = 0
        last = None
        for r in active(rs):
            q = point(r)
            if q is None or q == last:
                run += 1
                best = max(best, run)
            else:
                run = 0
            if q is not None:
                last = q
        summary[u]["stall_max"] = best
        if best > opts["stall_ticks"]:
            c.fail("unit=%s longest non-moving run=%d active ticks > stall-ticks=%d"
                   % (u, best, opts["stall_ticks"]), "unit:%s:stall" % u)
        else:
            c.p()
    return c


def check_statefp(statefp):
    c = Check("statefp")
    if not statefp:
        c.skip("no STATEFP lines (trace without H4)")
        return c, None
    s1 = [to_int(r.get("s1")) for r in statefp if to_int(r.get("s1")) is not None]
    s2 = [to_int(r.get("s2")) for r in statefp if to_int(r.get("s2")) is not None]
    for r in statefp:
        trap = to_int(r.get("trap"))
        t, u, call = r.get("tick"), r.get("unit"), r.get("call")
        if trap == 1:
            c.fail("tick=%s unit=%s call=%s trap=1 (observer deref trap)"
                   % (t, u, call), "%s:%s:trap" % (t, u))
        elif trap is None:
            c.skip("tick=%s unit=%s call=%s trap missing" % (t, u, call))
        else:
            c.p()
    note = None
    if s1 and s2:
        nd = lambda v: all(b >= a for a, b in zip(v, v[1:]))
        note = ("map sizes: s1 min=%d max=%d non-decreasing=%s, "
                "s2 min=%d max=%d non-decreasing=%s"
                % (min(s1), max(s1), nd(s1), min(s2), max(s2), nd(s2)))
    return c, note


def summarize(per):
    """Пер-юнитная сводка по активным тикам (заполняется и для SKIP-юнитов)."""
    out = {}
    for u, rs in per.items():
        act = active(rs)
        pts = set(q for q in (point(r) for r in act) if q is not None)
        out[u] = {"ticks": len(rs), "active": len(act), "points": len(pts),
                  "selected": any(to_int(r.get("code")) in SELECT_CODES
                                   for r in act),
                  "moving": len(pts) >= 2, "stall_max": 0}
    return out


def check_engagement(per, opts, notes):
    """Юнит тикает, но никогда не доходит до 45eee0 (все тики code=-1)."""
    c = Check("engagement")
    for u, rs in per.items():
        if len(rs) < opts["unit_ticks"]:
            c.skip("unit=%s ticks=%d < unit-ticks=%d"
                   % (u, len(rs), opts["unit_ticks"]))
            continue
        n = len(active(rs))
        if n > 0:
            c.p()
        elif opts["allow_idle"]:
            notes.append("unit=%s ticks=%d, no active tick (code=-1 all)"
                         % (u, len(rs)))
            c.skip("unit=%s idle (all code=-1), allowed by --allow-idle" % u)
        else:
            c.fail("unit=%s ticks=%d but never reaches unit-AI (no active tick)"
                   % (u, len(rs)), "unit:%s:idle" % u)
    return c


def profile_block(battle, per, opts):
    """Профиль (план 9.d2): таблица по `us`/`vis`, top-N тиков, доля верхнего 1%.

    `vis` — число входов `45dd70` за тик (прямая метрика работы тика),
    `us` — QPC-длительность; `us/vis` = µs на визит. На rc не влияет.
    """
    def num(r, k):
        v = to_int(r.get(k))
        return v if v is not None and v >= 0 else 0

    rows = []
    for u, rs in per.items():
        us = [num(r, "us") for r in rs]
        vis = [num(r, "vis") for r in rs]
        codes = Counter(to_int(r.get("code")) for r in rs)
        rows.append({"unit": u, "ticks": len(rs),
                     "active": sum(1 for r in rs if to_int(r.get("code")) != -1),
                     "us_sum": sum(us), "us_mean": round(sum(us) / len(us), 1),
                     "us_max": max(us), "vis_sum": sum(vis),
                     "vis_mean": round(sum(vis) / len(vis), 1),
                     "vis_max": max(vis),
                     "codes": {c: codes[c] for c in sorted(codes)},
                     "max_i1": max((to_int(r.get("i1")) for r in rs),
                                   default=-1)})
    rows.sort(key=lambda r: -r["us_sum"])
    top = sorted(battle, key=lambda r: -num(r, "us"))[:max(1, opts["top"])]
    total = sum(num(r, "us") for r in battle)
    n1 = max(1, len(battle) // 100)
    top1 = sorted((num(r, "us") for r in battle), reverse=True)[:n1]
    ratios = sorted(num(r, "us") / max(1, num(r, "vis")) for r in battle)
    med = ratios[len(ratios) // 2] if ratios else 0.0
    tsum, tvis = sum(num(r, "us") for r in top), sum(num(r, "vis") for r in top)
    pct = round(100.0 * sum(top1) / total, 1) if total else 0.0
    note = ("top 1%% of ticks (%d) = %.1f%% of total us %d; top-%d ticks "
            "%.1f us/visit vs median-all %.1f"
            % (n1, pct, total, len(top),
               (tsum / tvis) if tvis else 0.0, med))
    return {"units": rows,
            "top": [{"tick": r.get("tick"), "unit": r.get("unit"),
                     "us": num(r, "us"), "vis": num(r, "vis"),
                     "i1": to_int(r.get("i1")), "i2": to_int(r.get("i2")),
                     "code": to_int(r.get("code"))} for r in top],
            "top_pct_us": pct, "top_ticks": n1, "total_us": total,
            "note": note}


def print_profile(prof):
    hdr = "  %4s %5s %6s %8s %8s %7s %8s %8s %7s %15s %6s"
    row = "  %4s %5d %6d %8d %8s %7d %8d %8s %7d %15s %6d"
    print("profile:")
    print(hdr % ("unit", "ticks", "active", "us_sum", "us_mean", "us_max",
                 "vis_sum", "vis_mean", "vis_max", "codes", "max_i1"))
    for r in prof["units"]:
        codes = ",".join("%s:%d" % (c, n) for c, n in r["codes"].items())
        print(row % (r["unit"], r["ticks"], r["active"], r["us_sum"],
                     r["us_mean"], r["us_max"], r["vis_sum"], r["vis_mean"],
                     r["vis_max"], codes, r["max_i1"]))
    print("top %d ticks by us:" % len(prof["top"]))
    for t in prof["top"]:
        print("  tick=%s unit=%s us=%d vis=%d i1=%s i2=%s code=%s"
              % (t["tick"], t["unit"], t["us"], t["vis"], t["i1"], t["i2"], t["code"]))
    print("note: %s" % prof["note"])


def check_exit(battle):
    """Хвост трассы цел: последняя BATTLE-строка полная, её тик = максимум."""
    c = Check("exit")
    missing = [k for k in BATTLE_FIELDS if k not in battle[-1]]
    ticks = [t for t in (to_int(r.get("tick")) for r in battle) if t is not None]
    if missing:
        c.skip("last BATTLE line truncated (missing %s)" % ",".join(missing))
    elif not ticks:
        c.skip("no tick field in BATTLE lines")
    elif max(ticks) in ticks:
        c.p()
    else:
        c.skip("max tick %s absent from parsed lines" % max(ticks))
    return c


def parse_args(argv):
    files = []
    opts = {"min_ticks": 100, "min_units": 2, "min_moving": 1,
            "max_iter": 200, "unit_ticks": 20, "stall_ticks": 60,
            "max_tick_us": 20000, "spin_vis": 1000, "top": 10,
            "allow_idle": False, "profile": False,
            "json": None, "baseline": None}
    i = 0
    while i < len(argv):
        a = argv[i]
        if not a.startswith("--"):
            files.append(a)
            i += 1
            continue
        name, val = (a.split("=", 1) + [None])[:2] if "=" in a else (a, None)
        if name in ("--allow-idle", "--profile"):
            opts[name[2:]] = True
            i += 1
            continue
        if name in ("--json", "--baseline") or name in NUMOPTS:
            if val is None:
                i += 1
                if i >= len(argv):
                    print(USAGE)
                    return None
                val = argv[i]
            if name in ("--json", "--baseline"):
                opts[name[2:]] = val
            else:
                n = to_int(val)
                if n is None or n < 0:
                    print("bad value for %s: %s" % (name, val))
                    return None
                opts[NUMOPTS[name]] = n
            i += 1
            continue
        print("unknown flag: %s" % name)
        print(USAGE)
        return None
    if not files:
        print(USAGE)
        return None
    return files, opts


def print_section(header, items, fmt):
    print(header)
    if not items:
        print("  none")
    for it in items:
        print(fmt % it)


def load_baseline(path):
    """baseline --json -> {check: set(fail_keys)}."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            ref = json.load(f)
    except (OSError, ValueError) as exc:
        print("baseline unreadable: %s (%s)" % (path, exc))
        return None
    return {c.get("name"): set(c.get("fail_keys", []))
            for c in ref.get("checks", [])}


def main(argv):
    parsed = parse_args(argv)
    if parsed is None:
        return 2
    files, opts = parsed
    battle, statefp = parse(files)
    if not battle:
        print("no BATTLE lines in %d file(s)" % len(files))
        return 2

    per = by_unit(battle)
    summary = summarize(per)
    notes = []

    checks = []
    c, ins_ticks = check_window(len(battle), opts["min_ticks"], "ticks",
                                "window: BATTLE lines=%d < min-ticks=%d")
    checks.append(c)
    c, ins_units = check_window(len(per), opts["min_units"], "units",
                                "window: units=%d < min-units=%d")
    checks.append(c)
    checks.append(check_domains(battle, opts))
    checks.append(check_selected(per, opts, summary))
    checks.append(check_no_path(per, opts, summary))
    checks.append(check_movement(per, opts, summary))
    checks.append(check_runaway(battle, opts))
    checks.append(check_latency(battle, opts))
    checks.append(check_spin(battle, opts))
    checks.append(check_stall(per, opts, summary))
    c, fp_note = check_statefp(statefp)
    checks.append(c)
    checks.append(check_engagement(per, opts, notes))
    checks.append(check_exit(battle))

    fails = sum(c.failed for c in checks)
    insufficient = ins_ticks or ins_units
    has_pass = any(c.passed > 0 for c in checks)

    base = load_baseline(opts["baseline"]) if opts["baseline"] else None
    if opts["baseline"] and base is None:
        return 2
    if base is not None:
        new = [(c.name, k) for c in checks
               for k in sorted(set(c.fail_keys) - base.get(c.name, set()))]
        fixed = [(c.name, k) for c in checks
                 for k in sorted(base.get(c.name, set()) - set(c.fail_keys))]
        rc = 1 if new else (3 if (insufficient or not has_pass) else 0)
    else:
        new, fixed = [], []
        rc = 1 if fails else (3 if (insufficient or not has_pass) else 0)

    ticks = [t for t in (to_int(r.get("tick")) for r in battle) if t is not None]
    run = {"first_tick": min(ticks) if ticks else None,
           "last_tick": max(ticks) if ticks else None, "lines": len(battle)}

    print("files=%d BATTLE lines=%d STATEFP lines=%d units=%d tick %s..%s"
          % (len(files), len(battle), len(statefp), len(per),
             run["first_tick"], run["last_tick"]))
    for c in checks:
        print("CHECK %s pass=%d skip=%d fail=%d"
              % (c.name, c.passed, c.skipped, c.failed))
    for note in ([fp_note] if fp_note else []) + notes:
        print("note: %s" % note)

    prof = profile_block(battle, per, opts) if opts["profile"] else None
    if prof:
        print_profile(prof)

    reasons = OrderedDict()
    for c in checks:
        for r, n in c.reasons.items():
            reasons[r] = reasons.get(r, 0) + n
    print_section("skips:", list(reasons.items()), "  %s x%d")
    print_section("fails:", [(c.name, d) for c in checks for d in c.details],
                  "  %s: %s")

    if base is not None:
        print("baseline: %s" % opts["baseline"])
        print_section("new fails:", [(n, k) for n, k in new[:DETAIL_LIMIT * 4]],
                      "  NEW FAIL %s %s")
        print_section("fixed:", [(n, k) for n, k in fixed[:DETAIL_LIMIT * 4]],
                      "  FIXED %s %s")

    print("rc=%d (%s)" % (rc, {0: "invariants hold", 1: "fail present",
                               2: "unreadable/no lines/bad args",
                               3: "window insufficient, cannot judge"}[rc]))

    if opts["json"]:
        payload = {"rc": rc,
                   "checks": [{"name": c.name, "pass": c.passed,
                               "skip": c.skipped, "fail": c.failed,
                               "fail_keys": sorted(set(c.fail_keys)),
                               "details": c.details} for c in checks],
                   "units": summary, "run": run}
        if prof is not None:
            payload["profile"] = prof
        try:
            with open(opts["json"], "w", encoding="utf-8") as f:
                json.dump(payload, f, indent=1)
        except OSError as exc:
            print("json write failed: %s (%s)" % (opts["json"], exc))
            return 2
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
