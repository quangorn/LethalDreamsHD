#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сводка лога perf_frida9_frames.js (кадры через wglSwapBuffers).

  python3 tools/perf/perf_frames_summary.py <frames.jsonl> [<frames2.jsonl> ...]

По каждому файлу: fps (медиана/минимум), время кадра (медиана/p95/макс),
доля 2-секундных окон с кадром >50 мс и >100 мс, суммарное «время в лагах»
(кадры длиннее 50 мс, в секундах на окно).
"""
import json
import sys


def load(path):
    rows = []
    with open(path, encoding="utf-8") as fh:
        for ln in fh:
            ln = ln.strip()
            if not ln:
                continue
            try:
                o = json.loads(ln)
            except ValueError:
                continue
            if o.get("stats"):
                rows.append(o["stats"])
    return rows


def pct(xs, q):
    if not xs:
        return 0
    xs = sorted(xs)
    i = min(len(xs) - 1, int(round(q * (len(xs) - 1))))
    return xs[i]


def report(path):
    rows = load(path)
    if not rows:
        print("%s: нет данных" % path)
        return
    fps = [r["fps"] for r in rows]
    avg = [r["avg_ms"] for r in rows]
    mx = [r["max_ms"] for r in rows]
    over50 = sum(r["over50"] for r in rows)
    over100 = sum(r["over100"] for r in rows)
    frames = sum(r["frames"] for r in rows)
    windows = len(rows)
    bad50 = sum(1 for r in rows if r["over50"] > 0)
    bad100 = sum(1 for r in rows if r["over100"] > 0)
    # время в кадрах >50 мс: оцениваем как over50 * (макс.кадр - 50)/1000? нет —
    # точнее: over50 кадров минимум по 50 мс => нижняя граница 0.05*over50 с.
    est_lag = over50 * 0.05
    print("== %s" % path)
    print("  окон 2с: %d, кадров: %d, окно с лагом(>50мс): %d (%.0f%%), >100мс: %d (%.0f%%)"
          % (windows, frames, bad50, 100.0 * bad50 / windows,
             bad100, 100.0 * bad100 / windows))
    print("  fps: медиана %.1f, p10 %.1f, мин %.1f"
          % (pct(fps, 0.5), pct(fps, 0.1), min(fps)))
    print("  кадр avg_ms: медиана %d, p95 %d, макс %d"
          % (pct(avg, 0.5), pct(avg, 0.95), max(avg)))
    print("  худший кадр в окне: медиана %d, p95 %d, макс %d мс"
          % (pct(mx, 0.5), pct(mx, 0.95), max(mx)))
    print("  кадров >50мс: %d (>=%.1f с), >100мс: %d" % (over50, est_lag, over100))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__.strip())
    for p in sys.argv[1:]:
        report(p)
        print()


if __name__ == "__main__":
    main()
