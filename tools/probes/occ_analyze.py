#!/usr/bin/env python3
"""occ_analyze.py — занятость клеток мобами по OCC-трассе (LDHOOK_MOB_OCC=1).

Зачем: «застревание мобов друг в друге» = два юнита долго в одной клетке.
Лог OCC (battle/occ.cpp, строка с H1) — одна строка на выборку тика юнита:
`OCC tick unit cx cy cz px py pz t0 t1 t2 s1 s2 code`. `cx,cy` — клетка
pos-тройки game_ctx (unit+0x138 → +0xc; тот же источник, что leaf 45c6f0).

H1 даёт ровно одну OCC-строку на глобальный тик, поэтому «одинаковая клетка
в одном тике» невозможна; близость юнитов — пара в одной клетке в окне
--window глобальных тиков (SHARED pairs) и гистограмма клетка→юниты.

Аномалии:
  SHARED — клетка, в которой побывало >=2 юнитов, и пары юнитов в одной
           клетке в окне --window;
  STALL  — (cx,cy) юнита не менялась >= --gap подряд выборок (в отчёте —
           float-тройка движения px/py на конце серии);
  сводка — юнитов, строк, клеток, максимум разделяющих, стагнов, строк со
           сбоем чтения (sentinel-строки cx=cy=px=-1).

Сессии разделяются по сбросу tick (каждый запуск игры начинает с 1); ординалы
юнитов в новой сессии — новая нумерация, стагны считаются внутри сессии.

Запуск:
  python3 tools/probes/occ_analyze.py "<GameRoot>/hdlog.txt" [--every K]
      [--gap N] [--window T]
"""
import argparse
import re
import struct
import sys

OCC_RE = re.compile(
    r"OCC tick=(\d+) unit=(-?\d+) cx=(-?\d+) cy=(-?\d+) cz=(-?\d+) "
    r"px=(-?\d+) py=(-?\d+) pz=(-?\d+) t0=([0-9a-fA-F]{1,8}) "
    r"t1=([0-9a-fA-F]{1,8}) t2=([0-9a-fA-F]{1,8}) s1=(-?\d+) s2=(-?\d+) "
    r"code=(-?\d+)")


def as_float(bits):
    """int-бит float-тройки движения → float (0.0 при нулевых битах)."""
    return struct.unpack("<f", struct.pack("<i", bits & 0xFFFFFFFF))[0]


def is_fault(rec):
    """Сбой разыменования: вся ctx-тройка и тройка движения в sentinel'ах."""
    return rec["cx"] < 0 and rec["cy"] < 0 and rec["px"] < 0


def no_ctx(rec):
    """ctx-тройка не читается (sentinel cx), но строка валидна по движению:
    первый тик юнита / game_ctx+0xc == NULL. В клетки не входит."""
    return rec["cx"] < 0 and rec["cy"] < 0


def parse(paths):
    """-> список сессий {recs:[{tick,unit,cx,cy,px,py,...}]} в порядке строк."""
    sessions = []
    cur = None
    last_tick = None
    for path in paths:
        try:
            fh = open(path, "r", encoding="latin-1", errors="replace")
        except IOError as e:
            print("не читается: %s (%s)" % (path, e), file=sys.stderr)
            continue
        with fh:
            for line in fh:
                if "OCC tick=" not in line:
                    continue
                m = OCC_RE.search(line)
                if m is None:
                    continue
                g = m.groups()
                tick = int(g[0])
                if last_tick is not None and tick <= last_tick:
                    cur = None            # сброс счётчика — новый запуск игры
                last_tick = tick
                if cur is None:
                    cur = {"recs": []}
                    sessions.append(cur)
                cur["recs"].append({
                    "tick": tick, "unit": int(g[1]),
                    "cx": int(g[2]), "cy": int(g[3]), "cz": int(g[4]),
                    "px": int(g[5]), "py": int(g[6]), "pz": int(g[7]),
                    "code": int(g[13]),
                })
    return sessions


def analyze(sessions, gap, window):
    """-> (shared, pairs, stalls, summary); всё внутри сессии."""
    shared = {}                          # (cx,cy) -> set(units)
    pairs = []
    stalls = []
    n_lines = 0
    n_fault = 0
    n_noctx = 0
    units_seen = set()
    cells_seen = set()

    for s in sessions:
        by_cell = {}                     # (cx,cy) -> [(tick, unit)]
        per_unit = {}
        for r in s["recs"]:
            n_lines += 1
            if is_fault(r):
                n_fault += 1
                continue
            if no_ctx(r):
                n_noctx += 1
                continue
            cell = (r["cx"], r["cz"])   # pos-тройка = (x, y=0, z); клетка = (x,z)
            shared.setdefault(cell, set()).add(r["unit"])
            cells_seen.add(cell)
            units_seen.add(r["unit"])
            by_cell.setdefault(cell, []).append((r["tick"], r["unit"]))
            per_unit.setdefault(r["unit"], []).append(r)

        for cell, seq in sorted(by_cell.items()):
            seq.sort()
            for i in range(len(seq) - 1):
                (t1, u1) = seq[i]
                (t2, u2) = seq[i + 1]
                if u1 != u2 and (t2 - t1) <= window:
                    pairs.append((u1, u2, t1, t2, cell[0], cell[1], t2 - t1))

        # STALL: по последовательности выборок юнита (в порядке тиков).
        for unit in sorted(per_unit):
            seq = sorted(per_unit[unit], key=lambda r: r["tick"])
            run = 1
            for i in range(1, len(seq) + 1):
                if i < len(seq) and seq[i]["cx"] == seq[i - 1]["cx"] and \
                        seq[i]["cz"] == seq[i - 1]["cz"]:
                    run += 1
                else:
                    if run >= gap:
                        last = seq[i - 1]
                        first = seq[i - run]
                        stalls.append((unit, first["tick"], last["tick"],
                                       as_float(last["cx"]),
                                       as_float(last["cz"]),
                                       as_float(last["px"]),
                                       as_float(last["pz"])))
                    run = 1

    summary = {
        "sessions": len(sessions), "occ": n_lines, "units": len(units_seen),
        "cells": len(cells_seen),
        "shared_cells": sum(1 for u in shared.values() if len(u) > 1),
        "max_share": max((len(u) for u in shared.values()), default=0),
        "pairs": len(pairs), "stalls": len(stalls), "fault": n_fault,
        "no_ctx": n_noctx,
    }
    return shared, pairs, stalls, summary


def main():
    ap = argparse.ArgumentParser(
        description="Разбор OCC-строк (LDHOOK_MOB_OCC): общие клетки и стагны.")
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--every", type=int, default=10,
                    help="период выборки K (LDHOOK_MOB_OCC_EVERY), для справки")
    ap.add_argument("--gap", type=int, default=6,
                    help="подряд выборок без смены клетки для STALL")
    ap.add_argument("--window", type=int, default=100,
                    help="окно глобальных тиков для пары в одной клетке")
    a = ap.parse_args()

    shared, pairs, stalls, sm = analyze(parse(a.logs),
                                        a.gap if a.gap > 0 else 1,
                                        a.window if a.window > 0 else 1)
    if sm["occ"] == 0:
        print("OCC lines: 0")
        return 0
    print("OCC lines: %d  sessions=%d units=%d cells=%d every=%d gap=%d "
          "window=%d fault=%d no_ctx=%d" % (sm["occ"], sm["sessions"],
                                            sm["units"], sm["cells"], a.every,
                                            a.gap, a.window, sm["fault"],
                                            sm["no_ctx"]))

    hist = sorted(((len(u), c) for c, u in shared.items()),
                  key=lambda kv: (-kv[0], kv[1][0], kv[1][1]))
    n_shared = 0
    print("\n== SHARED (позиция, в которой побывало >=2 юнитов)")
    for cnt, cell in hist:
        if cnt < 2:
            break
        n_shared += 1
        print("   (%.3f,%.3f): %d юнита %s" % (as_float(cell[0]),
                                               as_float(cell[1]), cnt,
                                          sorted(shared[cell])))
    print("   shared cells: %d / %d" % (n_shared, sm["cells"]))

    print("\n== SHARED pairs (unitA unitB tickA tickB x z dt; окно <=%d тиков):"
          % a.window)
    for (u1, u2, t1, t2, cx, cz, dt) in sorted(pairs)[:200]:
        print("   %d %d %d %d %.3f %.3f %d" % (u1, u2, t1, t2, as_float(cx),
                                               as_float(cz), dt))
    if not pairs:
        print("   none")

    print("\n== STALL (>= %d подряд выборок в одной позиции; "
          "unit tick_first tick_last x z px pz):" % a.gap)
    for st in sorted(stalls, key=lambda s: -(s[2] - s[1]))[:100]:
        print("   %d %d %d %d %d %.3f %.3f" % (st[0], st[1], st[2], st[3],
                                               st[4], st[5], st[6]))
    if not stalls:
        print("   none")

    print("\n== итог: units=%d occ=%d cells=%d shared_cells=%d max_share=%d "
          "pairs=%d stalls=%d fault=%d"
          % (sm["units"], sm["occ"], sm["cells"], sm["shared_cells"],
             sm["max_share"], sm["pairs"], sm["stalls"], sm["fault"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
