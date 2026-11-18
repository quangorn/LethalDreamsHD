# perf_analyze2.py — summarize perf_frida3 throttle experiment log.
# Usage: python3 perf_analyze2.py <log.jsonl>
import json, sys, os

if len(sys.argv) < 2:
    print("usage: python3 perf_analyze2.py <log.jsonl>")
    sys.exit(1)
path = sys.argv[1]
rows = []
bts = []
with open(path, encoding="utf-8") as f:
    for ln in f:
        ln = ln.strip()
        if not ln:
            continue
        try:
            j = json.loads(ln)
        except Exception:
            continue
        if "stats" in j:
            rows.append(j)
        elif j.get("type") == "bt" or "bt" in ln:
            bts.append(j)

print(f"windows: {len(rows)}")
tot_calls = tot_skip = tot_ms = tot_f45_ms = 0
print(f"{'t':>5} {'calls':>6} {'skip':>5} {'bdd_ms':>7} {'f45_calls':>9} {'f45_ms':>7} {'f45_avg':>7} {'f45_max':>7}  hist")
for j in rows:
    s = j["stats"]
    c = s.get("calls", 0); sk = s.get("skipped", 0); ms = s.get("totalMs", 0)
    fc = s.get("f45_calls", 0); fm = s.get("f45_ms", 0); fx = s.get("f45_max", 0)
    h = s.get("hist", {})
    tot_calls += c; tot_skip += sk; tot_ms += ms; tot_f45_ms += fm
    avg = (fm / fc) if fc else 0
    hs = " ".join(f"{k}:{v}" for k, v in sorted(h.items()))
    print(f"{j['t']:>5} {c:>6} {sk:>5} {ms:>7} {fc:>9} {fm:>7} {avg:>7.1f} {fx:>7}  {hs}")

print()
print(f"TOTALS: calls={tot_calls} skipped={tot_skip} ({100.0*tot_skip/max(1,tot_calls):.0f}%)  bdd_ms={tot_ms}  f45_ms={tot_f45_ms}")
print(f"bdd avg per real call: {tot_ms/max(1,tot_calls-tot_skip):.2f} ms")

for b in bts:
    fr = b.get("frames", [])
    print(f"--- backtrace #{b.get('n')}:")
    for x in fr:
        print("   ", x)