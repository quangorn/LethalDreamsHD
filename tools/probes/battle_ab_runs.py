#!/usr/bin/env python3
"""battle_ab_runs.py — измерительный стенд живого A/B (план 9.m1).

Одиночные прогоны несравнимы: одна и та же `battle_169.ps1` на нативе давала 6, 29
и 36 тиков >20 ms, а runaway — 6 и 118. Стенд гоняет варианты **перемежаемыми**
группами (round-robin: arm1 run1, arm2 run1, arm1 run2), разбирает каждый лог
9.d1 (`battle_invariants`) и считает агрегат по варианту.

Запуск (первый `--arm` — эталон сравнения):

  python3 tools/probes/battle_ab_runs.py --runs 5 --tag ab1 \\
    --arm native:LDHOOK_SITE6=0 \\
    --arm e0a0:LDHOOK_SITE6=1,LDHOOK_SITE6_EEE0=0 \\
    --arm both:LDHOOK_SITE6=1 [--shots DIR] [--out JSON] [--tree DIR]
    [--baseline JSON] [--timeout SEC]

Правило приёмки объявлено до прогона и не подстраивается под результат: вариант
«не хуже» ⇔ (а) ни один прогон не даёт НОВЫХ провалов в `domains`/`engagement`/
`no_path`; (б) медиана µs на активный тик не выше эталонной; (в) медиана
`latency` на 1000 активных тиков не выше эталонной. Иначе правка отклоняется,
даже если «на глаз» стало лучше.

Метрики считаются по **активным** тикам (`code != -1`, `battle_invariants.active`);
`latency`/`spin`/`runaway` — число провалов соответствующей проверки
`battle_invariants` на прогон. Лог прогона — `<tree>/hdlog.txt`, копируется в
`<out dir>/<tag>_<arm>_<n>.txt` до следующего прогона (раннер перезатераёт
`hdlog.txt`). Дерево прогона — scratch-дерево ldhook (личных путей нет); игра к
началу обязана быть остановлена, после стенда она гасится всегда.
"""
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(os.path.dirname(_HERE))
sys.path.insert(0, _ROOT)
sys.path.insert(0, _HERE)
import paths  # noqa: E402
import battle_invariants as bi  # noqa: E402

PS1 = os.path.join(_HERE, "battle_169.ps1")
BASELINE = os.path.join(_HERE, "battle_invariants_baseline.json")
PYD_ARTIFACT = os.path.join(_ROOT, "build", "ldhook", "ldhook.pyd")
# Env, которые стенд ставит в каждом прогоне независимо от варианта.
ALWAYS_ENV = {"LDHOOK_LOG": "1", "LDHOOK_BATTLE_TRACE": "1"}
GUARD_CHECKS = ("domains", "engagement", "no_path")
GAME = "LETHALDREAMS.EXE"
EXIT_RE = re.compile(r"EXITED (?:step=\d+|menu).*code=0x([0-9A-Fa-f]+)")
USAGE = ("usage: battle_ab_runs.py --arm NAME:ENV=VAL,ENV=VAL [--arm ...] "
         "[--runs N] [--tag TAG] [--shots DIR] [--out JSON] [--tree DIR] "
         "[--baseline JSON] [--timeout SEC]")

# Дефолты проверок берём у battle_invariants (parse_args на вымышленном логе),
# чтобы не плодить константы: --max-tick-us 20000, --spin-vis 1000, --max-iter 200.
OPTS = bi.parse_args(["probe.log"])[1]


def fmt(v):
    return "NA" if v is None else ("%.1f" % v)


def md5(path):
    with open(path, "rb") as fh:
        return hashlib.md5(fh.read()).hexdigest()


def game_running():
    p = subprocess.run(["tasklist", "/FI", "ImageName eq %s" % GAME],
                       capture_output=True)
    return b"LETHALDREAMS" in p.stdout


def kill_game():
    subprocess.run(["taskkill", "/F", "/IM", GAME], capture_output=True)


def parse_arm(raw):
    """NAME:LDHOOK_SITE6=1,LDHOOK_SITE6_EEE0=0 -> (name, {env: val})."""
    if ":" not in raw:
        return None, "arm %r: нет ':' (формат NAME:ENV=VAL,...)" % raw
    name, body = raw.split(":", 1)
    env = {}
    for item in [x.strip() for x in body.split(",") if x.strip()]:
        if "=" not in item:
            return None, "arm %s: параметр %r без '=' (ENV=VAL)" % (name, item)
        k, v = item.split("=", 1)
        env[k.strip()] = v.strip()
    return (name, env), None


def parse_args(argv):
    arms, opts = [], {}
    i = 0
    while i < len(argv):
        a = argv[i]
        if not a.startswith("--"):
            return None, "unknown argument %r" % a
        key, val = (a.split("=", 1) + [None])[:2]
        if val is None:
            i += 1
            if i >= len(argv):
                return None, "%s требует значение" % key
            val = argv[i]
        if key == "--arm":
            arm, err = parse_arm(val)
            if arm is None:
                return None, err
            if arm[0] in [a[0] for a in arms]:
                return None, "повторяющееся имя варианта: %s" % arm[0]
            arms.append(arm)
        elif key in ("--runs", "--tag", "--shots", "--out", "--tree",
                     "--baseline", "--timeout"):
            opts[key[2:]] = val
        else:
            return None, "unknown flag %s" % key
        i += 1
    if len(arms) < 2:
        return None, "нужно минимум два --arm (первый = эталон сравнения)"
    return arms, opts


def p95(vals):
    """p95 как элемент sorted-списка по индексу round(0.95*(n-1))."""
    if not vals:
        return None
    s = sorted(vals)
    return s[min(len(s) - 1, int(round(0.95 * (len(s) - 1))))]


def run_checks(battle, statefp):
    """Сборка проверок — тот же список, что battle_invariants.main()."""
    if not battle:
        return []
    per = bi.by_unit(battle)
    summary = bi.summarize(per)
    notes = []
    checks = []
    c, _ = bi.check_window(len(battle), OPTS["min_ticks"], "ticks",
                           "window: BATTLE lines=%d < min-ticks=%d")
    checks.append(c)
    c, _ = bi.check_window(len(per), OPTS["min_units"], "units",
                           "window: units=%d < min-units=%d")
    checks.append(c)
    checks.append(bi.check_domains(battle, OPTS))
    checks.append(bi.check_selected(per, OPTS, summary))
    checks.append(bi.check_no_path(per, OPTS, summary))
    checks.append(bi.check_movement(per, OPTS, summary))
    checks.append(bi.check_runaway(battle, OPTS))
    checks.append(bi.check_latency(battle, OPTS))
    checks.append(bi.check_spin(battle, OPTS))
    checks.append(bi.check_stall(per, OPTS, summary))
    c, _ = bi.check_statefp(statefp)
    checks.append(c)
    checks.append(bi.check_engagement(per, OPTS, notes))
    checks.append(bi.check_exit(battle))
    return checks


def read_log(path):
    """Копия лога -> (строки, BATTLE-записи, STATEFP-записи). Нечитаемый лог = пусто."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.read().replace("\r\n", "\n").replace("\r", "\n").split("\n")
    except OSError:
        return [], [], []
    try:
        battle, statefp = bi.parse([path])
    except SystemExit:
        battle, statefp = [], []
    return lines, battle, statefp


def tail_flags(lines):
    """EXITED-строка, финальные END/KILL-маркеры, признак краша."""
    exit_line, exit_code = None, None
    for ln in lines:
        mm = EXIT_RE.search(ln)
        if mm:
            exit_line = ln.strip()
            exit_code = int(mm.group(1), 16)
    has_end = any(ln.startswith("END") for ln in lines)
    has_kill = any(ln.startswith("KILL") for ln in lines)
    crashed = (exit_code not in (None, 0)) or not (has_end and has_kill)
    return exit_line, exit_code, has_end, has_kill, crashed


def run_one(arm, n, tag, tree, shots, outdir, timeout, base):
    """Один прогон: powershell-раннер, копия hdlog.txt, разбор 9.d1."""
    name = arm[0]
    log_tag = "%s_%s_%d" % (tag, name, n)
    env = dict(os.environ)
    env.update(arm[1])
    env.update(ALWAYS_ENV)
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", paths.win_path(PS1), "-Tag", log_tag,
           "-GameRoot", tree, "-ShotsDir", shots]
    t0 = time.time()
    timed_out = False
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT)
    try:
        p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        p.kill()
        p.communicate()
        kill_game()
    dur = time.time() - t0
    src = os.path.join(tree, "hdlog.txt")
    dst = os.path.join(outdir, "%s.txt" % log_tag)
    copied = os.path.isfile(src)
    if copied:
        shutil.copyfile(src, dst)
    lines, battle, statefp = read_log(dst) if copied else ([], [], [])

    act = bi.active(battle)
    us = [v for v in (bi.to_int(r.get("us")) for r in act)
          if v is not None and v >= 0]
    checks = run_checks(battle, statefp)
    counts = {c.name: c.failed for c in checks}
    n_act = len(act)
    # rate на 1000 активных тиков; нет активных тиков / нет проверки -> None
    rates = {}
    for key in ("latency", "spin", "runaway"):
        v = counts.get(key)
        rates[key + "_per1000"] = (v * 1000.0 / n_act) if v is not None and n_act else None

    new_fails = {c.name: sorted(set(c.fail_keys) - base.get(c.name, set()))
                for c in checks if set(c.fail_keys) - base.get(c.name, set())}
    fixed = {c.name: sorted(base.get(c.name, set()) - set(c.fail_keys))
            for c in checks if base.get(c.name, set()) - set(c.fail_keys)}
    exit_line, exit_code, has_end, has_kill, crashed = tail_flags(lines)
    return {"run": log_tag, "arm": name, "n": n, "dur_s": round(dur, 1),
            "timed_out": timed_out, "ps_rc": p.returncode,
            "log_copied": copied, "log": dst, "env": arm[1],
            "battle_lines": len(battle), "active": n_act,
            "us_sum_active": sum(us), "us_n": len(us),
            "us_median": statistics.median(us) if us else None,
            "us_p95": p95(us),
            "us_per_active": (sum(us) / len(us)) if us else None,
            "latency": counts.get("latency"), "spin": counts.get("spin"),
            "runaway": counts.get("runaway"),
            "latency_per1000": rates["latency_per1000"],
            "spin_per1000": rates["spin_per1000"],
            "runaway_per1000": rates["runaway_per1000"],
            "checks": counts, "new_fails": new_fails, "fixed": fixed,
            "exit_line": exit_line, "exit_code": exit_code,
            "has_end": has_end, "has_kill": has_kill, "crashed": crashed}


def aggregate(runs):
    """Агрегат по варианту: медианы по прогонам, дошедшим до боя."""
    reached = [r for r in runs if r["battle_lines"] > 0]
    agg = {"runs": len(runs), "reached": len(reached),
           "crashed": sum(1 for r in runs if r["crashed"]),
           "hangs": sum(1 for r in runs if r["timed_out"]),
           "no_battle": sum(1 for r in runs if r["battle_lines"] == 0)}
    for key in ("us_per_active", "latency_per1000", "spin_per1000",
               "runaway_per1000"):
        vals = [r[key] for r in reached if r[key] is not None]
        agg["med_" + key] = statistics.median(vals) if vals else None
        agg["n_med_" + key] = len(vals)
    names = set()
    for r in runs:
        names.update(c for c, keys in r["new_fails"].items() if keys)
    agg["new_fail_runs"] = {c: sum(1 for r in runs if r["new_fails"].get(c))
                            for c in sorted(names)}
    agg["new_guard_runs"] = sum(1 for r in runs
                                if any(r["new_fails"].get(c) for c in GUARD_CHECKS))
    return agg


def verdict(name, agg, ref, ref_agg):
    """Правило 9.m1: clauses (a)(b)(c) и общий вердикт против эталона."""
    clauses = [("a", "no NEW fails in %s" % "/".join(GUARD_CHECKS),
                agg["new_guard_runs"] == 0,
                "runs with NEW guard fails=%d/%d" % (agg["new_guard_runs"], agg["runs"]))]
    if name == ref:
        return clauses, "REFERENCE"
    for key, letter, label in (("us_per_active", "b",
                                "median us per active tick"),
                               ("latency_per1000", "c",
                                "median latency per 1000 active ticks")):
        got, want = agg["med_" + key], ref_agg["med_" + key]
        ok = got is not None and want is not None and got <= want
        clauses.append((letter, label, ok,
                        "%s vs %s (reference %s, median over %d runs)"
                        % (fmt(got), fmt(want), ref, agg["n_med_" + key])))
    return clauses, ("PASS" if all(c[2] for c in clauses) else "FAIL")


def main():
    arms, opts = parse_args(sys.argv[1:])
    if arms is None:
        print(USAGE)
        print("error: %s" % opts)
        return 2
    runs_n = int(opts.get("runs", 5))
    tag = opts.get("tag", "ab")
    base_dir = paths.cache_dir("battle_trace")
    shots = opts.get("shots") or os.path.join(base_dir, "%s_shots" % tag)
    out = opts.get("out") or os.path.join(base_dir, "%s_ab.json" % tag)
    outdir = os.path.dirname(os.path.abspath(out))
    tree = opts.get("tree") or (paths.scratch_dir("ldhook") + "/tree")
    baseline = opts.get("baseline") or BASELINE
    timeout = int(opts.get("timeout", 240))

    if not os.path.isfile(paths.win_path(PS1)):
        print("нет раннера: %s" % PS1)
        return 2
    if not os.path.isdir(tree) or not os.path.isfile(os.path.join(tree, "LETHALDREAMS.EXE")):
        print("scratch-дерево прогона не готово: %s\nсначала: python3 tools/ldhook/spike.py --baseline"
              % tree)
        return 2
    base = bi.load_baseline(baseline)
    if base is None:
        return 2
    os.makedirs(shots, exist_ok=True)
    os.makedirs(outdir, exist_ok=True)

    if game_running():
        print("игра уже запущена (%s): стенд гоняет только с остановленной игрой\n"
              "уберите её: taskkill /F /IM %s" % (GAME, GAME))
        return 2
    tree_pyd, art_pyd = os.path.join(tree, "ldhook.pyd"), PYD_ARTIFACT
    if os.path.isfile(tree_pyd) and os.path.isfile(art_pyd):
        a, b = md5(tree_pyd), md5(art_pyd)
        print("pyd tree=%s artifact=%s %s" % (a, b, "MATCH" if a == b else "MISMATCH"))
        if a != b:
            print("warning: дерево прогоняет не тот .pyd, что собран")

    ref = arms[0][0]
    order = [(arm, n) for n in range(1, runs_n + 1) for arm in arms]
    print("tag=%s runs=%d tree=%s shots=%s out=%s timeout=%ds" %
          (tag, runs_n, tree, shots, out, timeout))
    print("arms: %s  (reference=%s)" % (
        "; ".join("%s: %s" % (a[0], ",".join("%s=%s" % kv for kv in sorted(a[1].items())))
                  for a in arms), ref))

    all_runs, streak, aborted = [], 0, None
    t_start = time.time()
    for arm, n in order:
        r = run_one(arm, n, tag, tree, shots, outdir, timeout, base)
        all_runs.append(r)
        print("run %s dur=%.1f BATTLE=%d active=%d us_sum=%d us_med=%s us_p95=%s "
              "lat=%s spin=%s run=%s exit=%s crashed=%s%s"
              % (r["run"], r["dur_s"], r["battle_lines"], r["active"],
                 r["us_sum_active"], fmt(r["us_median"]), fmt(r["us_p95"]),
                 r["latency"], r["spin"], r["runaway"], r["exit_code"],
                 r["crashed"], " TIMED OUT" if r["timed_out"] else ""),
              flush=True)
        if arm[0] == ref:
            streak = streak + 1 if r["battle_lines"] == 0 else 0
            if streak >= 2:
                aborted = ("два подряд прогона эталона %s без BATTLE-строк — стенд остановлен" % ref)
                print("ABORT: %s" % aborted)
                break
        kill_game()
    kill_game()  # игра обязана остаться убитой

    per_arm = {a[0]: [r for r in all_runs if r["arm"] == a[0]] for a in arms}
    aggs = {a[0]: aggregate(per_arm[a[0]]) for a in arms}
    ref_agg = aggs[ref]

    print("")
    print("per-run:")
    hdr = "  %-16s %-7s %5s %6s %6s %9s %7s %7s %4s %4s %4s %7s %7s %7s %4s %s"
    print(hdr % ("run", "arm", "dur", "BATTLE", "active", "us_sum", "us_med",
                 "us_p95", "lat", "spin", "run", "lat/1k", "spin/1k", "run/1k",
                 "NEW", "note"))
    for r in all_runs:
        note = []
        if r["timed_out"]:
            note.append("HANG")
        if r["exit_code"] not in (None, 0):
            note.append("exit=0x%X" % r["exit_code"])
        elif r["crashed"]:
            note.append("crashed")
        print(hdr % (r["run"], r["arm"], "%.1f" % r["dur_s"], r["battle_lines"],
                     r["active"], r["us_sum_active"], fmt(r["us_median"]),
                     fmt(r["us_p95"]), r["latency"], r["spin"], r["runaway"],
                     fmt(r["latency_per1000"]), fmt(r["spin_per1000"]),
                     fmt(r["runaway_per1000"]), len(r["new_fails"]),
                     " ".join(note)))

    print("")
    print("aggregate (median over runs that reached battle):")
    ahdr = "  %-7s %4s %5s %5s %8s %8s %8s %8s %s"
    print(ahdr % ("arm", "runs", "reach", "crash", "us/act", "lat/1k", "spin/1k",
                  "run/1k", "runs with NEW fails (per check)"))
    for a in arms:
        g = aggs[a[0]]
        nf = ",".join("%s=%d" % (c, n) for c, n in sorted(g["new_fail_runs"].items()))
        print(ahdr % (a[0], g["runs"], g["reached"], g["crashed"],
                      fmt(g["med_us_per_active"]), fmt(g["med_latency_per1000"]),
                      fmt(g["med_spin_per1000"]), fmt(g["med_runaway_per1000"]),
                      nf or "none"))

    print("")
    print("rule (объявлено до прогона, пороги не подстраиваются):")
    print("  arm «не хуже» ⇔ (а) ни одного прогона с НОВЫМИ провалами в "
          "domains/engagement/no_path; (б) медиана µs на активный тик <= эталонной; "
          "(в) медиана latency на 1000 активных тиков <= эталонной")
    for a in arms:
        g = aggs[a[0]]
        clauses, res = verdict(a[0], g, ref, ref_agg)
        print("verdict %s vs %s: %s" % (a[0], ref, res))
        for letter, label, ok, detail in clauses:
            print("  (%s) %s [%s] %s" % (letter, label,
                                         "PASS" if ok else "FAIL", detail))

    doc = {"tag": tag, "tree": tree, "shots": shots,
           "baseline": paths.norm(baseline), "reference": ref,
           "arms": {a[0]: a[1] for a in arms}, "opts": OPTS,
           "runs": all_runs, "aggregates": aggs,
           "verdicts": {a[0]: {"reference": a[0] == ref,
                               "verdict": verdict(a[0], aggs[a[0]], ref, ref_agg)[1],
                               "clauses": [{"letter": c[0], "label": c[1],
                                            "pass": c[2], "detail": c[3]}
                                           for c in verdict(a[0], aggs[a[0]], ref, ref_agg)[0]]}
                         for a in arms},
           "aborted": aborted, "wall_s": round(time.time() - t_start, 1)}
    try:
        with open(out, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1)
    except OSError as exc:
        print("json write failed: %s (%s)" % (out, exc))
        return 2
    kill_game()
    print("")
    print("json: %s" % out)
    print("wall=%.1f s; игра остановлена" % (time.time() - t_start))
    return 0


if __name__ == "__main__":
    sys.exit(main())
