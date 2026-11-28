#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""report_sites.py — отчёт «что загрузилось» по логу ldhook (общий hdlog.txt).

Зачем: сайты (хуки в памяти) видны только из лога прогона. Этот скрипт разбирает
последний прогон и отвечает, что из нужного на месте: загрузчик `.pyd` и скан
`.text`, маркер, site1/site2/site3, site5/site6, env-гейты (tick/trace/detour/seed),
и «FIRED» первого вызова каждого хука.

Контуры (чем отличается набор нужных сайтов):
  install   — установленный cave-EXE (`exepatch12`): site1/2/3 на диске уже `E9`,
              скан сигнатур штатно их НЕ находит (это и есть «patched»); нужные
              сайты = загрузчик + site6.
  baseline  — baseline EXE (`45e93d46…`) без cave: site1/2/site3 и site6 ставятся
              хуками; они обязательны.
  auto      — контур определяется по строкам `install_siteN signature NOT found`.

Запуск:
  python3 tools/ldhook/report_sites.py "<дерево>/hdlog.txt" [--contour auto]
  python3 tools/ldhook/report_sites.py LOG --all-runs     # все прогоны лога
  python3 tools/ldhook/report_sites.py LOG --json out.json

rc: 0 — все нужные сайты на месте; 1 — есть провал (FAILED / нужный сайт не встал);
2 — лог не читается или прогонов нет.
"""
import argparse
import json
import re
import sys

HEADER_RE = re.compile(r"=== ldhook\.pyd phase 6 .*?level=(\d+) ===")
TEXT_RE = re.compile(r"\.text=0x[0-9a-f]+")
SITE_RE = re.compile(r"install_(site\d|tick_hook|trace_sites|tick_detour|seed_sites): (.*)")

# Строки-маркеры состояний сайта (ldhook.cpp, battle/target.cpp, sites_hd.cpp).
STATE_KEYS = [
    ("FAILED", "failed"),
    ("hook installed (full detour", "installed"),
    ("hooks installed (diagnostic)", "installed"),
    ("hooks installed (trace ON)", "installed"),
    ("found at 0x", "installed"),
    ("-> s6_unit_ai_tick", "installed"),
    ("-> push 0x", "installed"),
    ("site(s) pinned to seed", "installed"),
    ("H1 b0_battle_ai_dispatch", "installed"),
    ("signature NOT found", "not-found"),
    ("пропущен", "skipped"),
    ("disabled", "off"),
    ("NOT installed", "off"),
    ("hooks NOT installed", "off"),
]

FIRED_KEYS = {
    "site1": "site1_impl: first call (hook FIRED)",
    "site2": "site2_impl: first call (hook FIRED)",
    "site3": "site3_impl: first call (hook FIRED)",
    "site5": "site5: first call (hook FIRED)",
    "site6": "site6: first call (hook FIRED)",
}

# Кто обязателен в контуре: сайт -> «нужен».
NEEDED = {
    "install": {"loader": True, "marker": True, "site1": False, "site2": False,
                "site3": False, "site6": True},
    "baseline": {"loader": True, "marker": True, "site1": True, "site2": True,
                 "site3": True, "site6": True},
}


def classify(rest):
    for pat, state in STATE_KEYS:
        if pat in rest:
            return state
    return "info"


def parse_runs(lines):
    """Лог -> список прогонов; каждый: {level, events:[(kind, name, rest)]}."""
    runs = []
    cur = None
    for line in lines:
        m = HEADER_RE.search(line)
        if m:
            cur = {"level": int(m.group(1)), "text": bool(TEXT_RE.search(line)),
                   "events": [], "lines": [line]}
            runs.append(cur)
            continue
        if cur is None:
            continue
        cur["lines"].append(line)
        m = SITE_RE.search(line)
        if m:
            name = m.group(1)
            rest = m.group(2)
            cur["events"].append((name, classify(rest), rest))
            continue
        for site, needle in FIRED_KEYS.items():
            if needle in line:
                cur["events"].append(("fired:" + site, "fired", needle))
                break
        if "marker written" in line:
            cur["events"].append(("marker", "written", "marker written"))
        elif "marker FAILED" in line:
            cur["events"].append(("marker", "failed", "marker FAILED"))
    return runs


def contour_of(run):
    for name, state, rest in run["events"]:
        if name in ("site1", "site2", "site3") and state == "not-found":
            return "install"
    return "baseline"


def report(run, contour, label):
    """-> (rows, rc). rows: [site, verdict, detail]."""
    ev = {}
    for name, state, rest in run["events"]:
        ev.setdefault(name, []).append((state, rest))
    rows = []
    rc = 0

    def best(name):
        """Состояние сайта по всем его строкам прогона:
        failed > installed > первое не-info; детали конкатенируются."""
        items = ev.get(name)
        if not items:
            return None
        states = [s for s, r in items]
        if "failed" in states:
            state = "failed"
        elif "installed" in states:
            state = "installed"
        else:
            state = next((s for s in states if s != "info"), states[0])
        return (state, " | ".join(r for s, r in items))

    # загрузчик и скан .text
    if run["text"]:
        rows.append(["loader", "loaded", ".text найден, level=%d" % run["level"]])
    else:
        rows.append(["loader", "FAILED", "нет .text в заголовке прогона"])
        rc = 1

    # маркер: при уровне 0 его нет и это норма
    need_marker = NEEDED[contour]["marker"] and run["level"] >= 1
    got = best("marker")
    if got and got[0] == "written":
        rows.append(["marker", "written", "ldhook_marker.txt"])
    elif got and got[0] == "failed":
        rows.append(["marker", "FAILED", "CreateFile не прошёл"])
        rc = 1
    elif need_marker:
        rows.append(["marker", "MISSING", "уровень %d, маркер обязан быть" % run["level"]])
        rc = 1
    else:
        rows.append(["marker", "off", "уровень 0: ни лога, ни маркера"])

    # сайты
    for site in ("site1", "site2", "site3", "site5", "site6"):
        got = best(site)
        if got is None:
            rows.append([site, "no-line", "прогон ничего не писал об этом сайте"])
            if NEEDED[contour].get(site):
                rc = 1
            continue
        state, rest = got
        verdict = state
        if site in ("site1", "site2", "site3"):
            if state == "not-found":
                # в установке сайты уже E9 (cave) — штатно; в baseline — провал
                verdict = "patched(cave)" if contour == "install" else "FAILED"
            elif state == "skipped":
                verdict = "skipped"
        if site == "site6" and state == "off":
            # LDHOOK_SITE6 не задан — в поставке дефолт 1, «off» = провал
            verdict = "FAILED"
        if verdict == "failed":
            verdict = "FAILED"
        rows.append([site, verdict, rest])
        if NEEDED[contour].get(site) and verdict not in ("patched(cave)", "skipped"):
            if verdict != "installed":
                rc = 1

    # env-гейты (не обязательны: дефолт выкл)
    for gate in ("tick_hook", "trace_sites", "tick_detour", "seed_sites"):
        got = best(gate)
        if got is None:
            continue
        state, rest = got
        rows.append([gate, "on" if state == "installed" else
                     ("FAILED" if state == "failed" else "off"), rest])
        if state == "failed":
            rc = 1

    # первый вызов каждого хука
    for site in ("site1", "site2", "site3", "site5", "site6"):
        fired = ev.get("fired:" + site)
        installed = (best(site) or ("info", ""))[0] == "installed"
        if fired:
            rows.append(["fired:" + site, "FIRED", "первый вызов хука"])
        elif installed:
            rows.append(["fired:" + site, "not-yet", "хук стоит, вызова в логе нет"])

    # гейт матрицы site3 (16:9) / identity (4:3)
    for line in run["lines"]:
        if "site3_impl: first call" in line:
            rows.append(["matrix", "gate", line.split("ldhook: ", 1)[-1].strip()])
            break

    return rows, rc


def main():
    ap = argparse.ArgumentParser(prog="report_sites.py")
    ap.add_argument("log")
    ap.add_argument("--contour", default="auto", choices=("auto", "install", "baseline"))
    ap.add_argument("--all-runs", action="store_true")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    try:
        fh = open(args.log, "r", encoding="latin-1", errors="replace")
        lines = fh.read().splitlines()
        fh.close()
    except IOError as e:
        sys.stderr.write("не читается лог: %s\n" % e)
        return 2

    runs = parse_runs(lines)
    if not runs:
        sys.stderr.write("прогонов в логе нет (нет заголовка `=== ldhook.pyd phase 6`) — %s\n"
                         % args.log)
        return 2

    picked = runs if args.all_runs else [runs[-1]]
    rc_all = 0
    out = []
    for i, run in enumerate(picked):
        contour = args.contour if args.contour != "auto" else contour_of(run)
        label = "run %d/%d" % (i + 1, len(picked))
        rows, rc = report(run, contour, label)
        rc_all = rc_all if rc == 0 else rc
        out.append({"label": label, "contour": contour, "level": run["level"],
                    "rows": [{"site": r[0], "verdict": r[1], "detail": r[2]} for r in rows],
                    "rc": rc})

    for item in out:
        print("== %s  contour=%s rc=%d" % (item["label"], item["contour"], item["rc"]))
        for r in item["rows"]:
            print("  %-12s %-12s %s" % (r["site"], r["verdict"], r["detail"]))
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump(out, fh, ensure_ascii=False, indent=1)
    return rc_all


if __name__ == "__main__":
    sys.exit(main())
