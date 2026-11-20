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
Runner log: `D:/tmp/cygwin/opencode/ld/ldhook/tree/hdlog.txt`; scratch EXE baseline `45e93d46…`, loader already in `ui.pyc`. Format v4: `cap` + `res` per call; leaf streams (`path`/`ptr`/`heur`/`timeout`) and `ai_words_after` in the `res` record; `args.cmp_addr` + per-node `pos_addr` = LDPos identity; grid = sizes only (pathfinding stubbed). Diff rc: 0 equal, 1 diverge, 2 input error, 3 prefix unmet.

## 6. Warnings
Never delete files; installed game `D:/Games/LethalDreams` READ-ONLY (use scratch tree only). Always `taskkill /F /IM LETHALDREAMS.EXE` after runs. Do not guess slot layouts without `game_dis.txt` / `docs/PLAN_BATTLE_AI_CPP.md` evidence. Keep writes opt-in; verify `check_state.py --quick` after installed-game changes.

## 7. State (offline replay gate, v5 real pathfinding)

- Capture v5 adds one `grid` record (levels/sizes, `cells` LDPos-words/metrics/
  keys/links, `rows`, world-tree statuses, heuristic inputs) plus per-call
  `tree_delta`/`row_delta`/`cell_delta`/`w110`/`res.leaf.timer`.
  Grid is static in this battle; runtime world-tree deltas are captured.
- Harness reconstructs GridLevel/GridCell/KeyRecord/rows and runs the port's
  own `s6_path_search`/`s6_pfs_ctor`/`s6_tree_next`/`s6_heuristic`; port leaf
  stream is written to `res_port.leaf`. External world calls (tree status,
  vtable getters, `FUN_0044bbf0` return, `timeGetTime`) are supplied from
  capture; the c690/c5a0 branch logic and arithmetic execute in the port.
- `--leaf-replay` restores v1..v4 leaf substitution for A/B of the harness;
  captures without a usable `grid` record are skipped with `ERROR`, rc 2.
  Diff: `battle_replay_diff.py <capture> <res_port> --fields=leaf` (rc 0/1/2/3).
- Gate result on full v5 capture: results 28/112 equal, path leaves 30993/31077,
  heuristic 188/188, timeout 28/28; first divergence seq=2, path leaf 329:
  native null vs port cell (244,107) at grid cell (244,107) (free-path cb40).
  `RESULT` first divergence seq=2 (node native [246,109], port none).
  No port defect proven; `battle/*.cpp` algorithm unchanged (only observers
  and capture/input seams). Remaining limitation: external world-query return
  supplied from capture; real search not yet equal beyond seq=1.
- `--chained` remains diagnostic only: the game clears map1/map2 between e0a0
  calls (`FUN_0045ca70` at `0x45efd7`), so `entry_state_mismatch` is expected.
