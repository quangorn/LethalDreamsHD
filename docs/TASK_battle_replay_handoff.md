# Handoff: offline capture/replay harness for FUN_0045e0a0 (plan 1.7 §3.2)

## 1. Goal
Compare native and port `FUN_0045e0a0` on identical captured input so timing/wave nondeterminism stops masking defects. Known target: **tick 16, unit 3** — port takes one extra iteration and registers **3 extra map1 slots**. Find the first diverging `seq` with field-level detail (which branch creates the nodes), fix the port, re-verify with replay diff rc 0. Everything opt-in: capture env unset ⇒ behaviour/performance unchanged.

## 2. Current state (branch `perf-battle`, HEAD `0206dd8`, no commits)
Added: `tools/ldhook/battle/capture.{h,cpp}` (JSONL writer), `tools/probes/battle_replay.cpp` (replica + port replay), `battle_replay_build.py`, `battle_replay_diff.py`. Modified: `ldhook.cpp`, `ldhook_internal.h`, `battle/{target,path,grid,trace,battle.h}.cpp/h`, `tools/ldhook/build.py`. All uncommitted.
Works: `python3 tools/ldhook/build.py` built `ldhook.pyd` (call-site version); `battle_replay_build.py` built `build/ldhook/battle_replay.exe`; function-entry leaf hooks are in place (mask 7 captures cleanly). Capture v2 (addresses) and replay are green — see §7.

## 3. Crashes: evidence, attempts, hypothesis
Symptom: `EXITED step=22 code=0xC0000005`; capture stops at 1 line (complete `cap` seq=1 tick=3 unit=3); hdlog ends `site5: entry #1 … map1=1 map2=1` — crash inside first `e0a0`. Full runner invocation (~40 s each), results:
```
mask 1 cb40 call-site: lines 1  crash True  impl []
mask 2 heur1 call-site: lines 1  crash True  impl ['cap_impl: heur value=0']
mask 4 heur2 call-site: lines 249 crash False
mask 8 timeout call-site: lines 1  crash True  impl ['cap_impl: timeout value=135069697']
mask 0 no leaf hooks:  lines 369 crash False
```
Tried and failed: wrong EAX restore order in trampolines (fixed); cb40 arg offsets grabbing the return address (fixed with `g_cap_a1/a2`); ECX clobber by diagnostic C++ call before `call [g_fn_*]` (fixed with `mov ecx,[esp+4]`) — heur1/timeout still crash while identical heur2 works. A ctypes `DEBUG_PROCESS` catcher (`D:/tmp/cygwin/opencode/ld/catch_crash.py`) segfaulted under 64-bit Python; no crash address. Don't repeat.
Best hypothesis: patching those **call sites** with `E9` is unsafe (context/SEH), not the logging. The current unbuilt function-entry approach (overwrite return address on stack, replay stolen prologue bytes, jmp original; `cap_ret_*` logs EAX then jumps to saved return address) is the fix attempt. Stolen lengths: cb40 9 B, c690 6 B, c5a0 10 B (includes `call [0x4ec27c]`); cb40/c5a0 prologues are not unique in `.text` — locate by VA and verify bytes, not `find_in_text`.

## 4. Remaining work
1. ~~Reconcile `ldhook.cpp`/`ldhook_internal.h` with `cap_fn_*`~~ — done; mask-7 capture is clean.
2. ~~Crash triage~~ — done (function-entry hooks).
3. ~~Harness check~~ — done; see §7 (rc 0).
4. ~~Report first diverging `seq`~~ — done; divergence was harness-side, fixed in the harness.
5. ~~Fix port defect~~ — none found; `battle/*.cpp` logic untouched.
6. ~~Add short 3-command section to `tools/ldhook/README.md`.~~ — done, см. §7.
7. ~~Chained (self-feeding) replay, plan 3.7'~~ — done, см. §7.

## 5. Commands and paths
```
python3 tools/ldhook/build.py
python3 tools/probes/battle_replay_build.py
cp build/ldhook/ldhook.pyd D:/tmp/cygwin/opencode/ld/ldhook/tree/ldhook.pyd
# PowerShell (env inherited by runner and game):
$env:LD_GAME="D:/tmp/cygwin/opencode/ld/ldhook/tree"; $env:LD_SHOTS="D:/tmp/cygwin/opencode/ld/battle_shots"
$env:LDHOOK_SITE6="0"; $env:LDHOOK_BATTLE_TRACE="0"; $env:LDHOOK_LOG="1"
$env:LDHOOK_BATTLE_CAPTURE="D:/tmp/cygwin/opencode/ld/replay_capture_addr.jsonl"; $env:LDHOOK_BATTLE_CAPTURE_MAX="400"
# add $env:LDHOOK_BATTLE_CAPTURE_LEAF="0" to capture without leaves
powershell -NoProfile -ExecutionPolicy Bypass -File tools/probes/battle_169.ps1 -Tag replay_cap
build/ldhook/battle_replay.exe D:/tmp/cygwin/opencode/ld/replay_capture_addr.jsonl D:/tmp/cygwin/opencode/ld/replay_port.jsonl
python3 tools/probes/battle_replay_diff.py D:/tmp/cygwin/opencode/ld/replay_capture_addr.jsonl D:/tmp/cygwin/opencode/ld/replay_port.jsonl
# stderr-лог порта: LD_REPLAY_TRACE=1 (info) / =2 (info+debug, шумно)
```
Runner log: `D:/tmp/cygwin/opencode/ld/ldhook/tree/hdlog.txt`; scratch EXE baseline `45e93d46…`, loader already in `ui.pyc`. Format v2: `cap` + `res` per call; leaf streams (`path`/`ptr`/`heur`/`timeout`) in the `res` record; `args.cmp_addr` + per-node `pos_addr` = LDPos identity; grid = sizes only (pathfinding stubbed). Diff rc: 0 equal, 1 diverge, 2 input error, 3 prefix unmet.

## 6. Warnings
Never delete files; installed game `D:/Games/LethalDreams` READ-ONLY (use scratch tree only). Always `taskkill /F /IM LETHALDREAMS.EXE` after runs. Do not guess slot layouts without `game_dis.txt` / `docs/PLAN_BATTLE_AI_CPP.md` evidence. Keep writes opt-in; verify `check_state.py --quick` after installed-game changes.

## 7. State after 20.11.2026, round 2 (faithful map1/map2 replay)
- Root cause of the `iters` desync was harness-side only: `target.cpp` passes `&self->map2` (= `aiBuf+0x74`, the std::map object embedded in BattleAI, `lea 0x74(%edi),%ecx` @0x45dec8) while the harness kept `RepMap2` elsewhere, so every lookup saw an empty map; occupancy is **exact-key** (`0x434bc0` + check @0x45ded2), not lower_bound-range; map1 order is by the node key = `LDPos*` (dd00 inserts `&obj->pos`; 0x45d810 compares `[node+0xc]`), not insertion order; the gate `cur->pos == cmp` is pointer identity, so coordinates are not enough.
- Capture v2 adds `args.cmp_addr`, `pos_addr` per map1 node, `leaf.ptr` (cb40 `LDPos*`), `iter_cap`/`iter_overflow`; `CAP_MAX_ITER=65536` (old 4096 truncated the long tick-16 call at 4096 latches). Map1 in-order == `pos_addr` order in all 515 dumps (evidence for the key).
- Harness: identity and map1 order from captured addresses, map2 resolved to the Rep at `aiBuf+0x74`, exact-key occupancy; `LD_REPLAY_TRACE=1/2` prints the port log to stderr.
- Validation: `D:/tmp/cygwin/opencode/ld/replay_capture_addr.jsonl` (258 calls) → `replay_port_addr2.jsonl`; `battle_replay_diff.py` **rc 0**, all 257 complete calls equal on node/code/iters/iter_seq/map1_after/occ_after. No port defect; `battle/*.cpp` logic unchanged.
- tick 16 in this run is unit 4 (seq=14) and matches; the reported "seq=1, iters 116 vs 71" reproduces only with address-less captures — harness artefact, not a port defect.
- Robustness: bad JSON/missing `ptr`/`res` without `cap`/unreadable file ⇒ `ERROR: …`, rc 2, no crash; truncated trailing line ignored.
- Open: initial map2 node values are not captured (harness uses NULL → "free path" for those keys; no exposure in this capture); README 3-command section — done (see below).

### 3.7' chained self-feeding replay (20.11.2026)
- `battle_replay.cpp`: added `--chained` (default unchanged). Replica is built once from the first complete `cap`, then `rep_rebind()` feeds the port's own map1/map2/identity state into the next call; per-call args/scalars/leaf stream still come from the capture. In-process compare of `node/code/iters/iter_seq` + `map1_after`/`occ_after`/`ai_words_after`; distinct `MUTATE` class when result matches but after-state does not, with node `pos`/`pos_addr`/filter/g/d2/h10k/cost delta and port branch (`dd70` branch 1 → `0x45dd00 register_occupied_slot`). `CHAIN:` summary = calls/chain_length/survived/entry_state_mismatch/first_div_seq/class; rc 0 equal, 1 diverge, 2 input error.
- Full-battle native capture `D:/tmp/cygwin/opencode/ld/fullbattle_capture.jsonl` (210 calls, ticks 3..269, `LDHOOK_BATTLE_CAPTURE_MAX=2000`, `LDHOOK_SEED=1234567890`, one `key:32` at entry via `battle_43.ps1`, game killed). Non-chained: `battle_replay_diff.py` rc 0, 210/210 equal. Chained: rc 1, **chain_length=210, survived=1**, first divergence **seq=2 class=RESULT** (node native `[260,103]` vs port `null`; after-state map1 native 67 / port 6: +1 −62, occ +1 −62), no `MUTATE` case.
- The divergence is NOT a port mutation defect: `entry_state_mismatch=209/209`, first at seq=2 — native `cap.map1` = 1 node while carried port state = 6 (`+6 −1`, 5 of 6 from seq-1 branch-1 registrations). The game clears the candidate/occupied maps between e0a0 calls, so a literal self-feeding chain cannot be a faithful Phase 3 gate; **no port fix landed** (no proven defect). Per-call replay remains the valid gate.
- Limitation: harness stubs pathfinding (grid = sizes only) and replays captured leaf values; the chain validates `dd70`/`e0a0`/`c690` bookkeeping and the port's own map insertions, not the grid search itself. Rerun: `build/ldhook/battle_replay.exe [--chained] D:/tmp/cygwin/opencode/ld/fullbattle_capture.jsonl <out.jsonl>` (build via `python3 tools/probes/battle_replay_build.py`).
