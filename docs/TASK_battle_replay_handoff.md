# Handoff: offline capture/replay harness for FUN_0045e0a0 (plan 1.7 §3.2)

## 1. Goal
Compare native and port `FUN_0045e0a0` on identical captured input so timing/wave nondeterminism stops masking defects. Known target: **tick 16, unit 3** — port takes one extra iteration and registers **3 extra map1 slots**. Find the first diverging `seq` with field-level detail (which branch creates the nodes), fix the port, re-verify with replay diff rc 0. Everything opt-in: capture env unset ⇒ behaviour/performance unchanged.

## 2. Current state (branch `perf-battle`, HEAD `8b61041`, no commits)
Added: `tools/ldhook/battle/capture.{h,cpp}` (JSONL writer), `tools/probes/battle_replay.cpp` (replica + port replay), `battle_replay_build.py`, `battle_replay_diff.py`. Modified: `ldhook.cpp`, `ldhook_internal.h`, `battle/{target,path,grid,trace,battle.h}.cpp/h`, `tools/ldhook/build.py`. All uncommitted.
Works: `python3 tools/ldhook/build.py` built `ldhook.pyd` (call-site version, md5 `71f172be…`); `battle_replay_build.py` built `build/ldhook/battle_replay.exe`; harness on synthetic capture produced `res_port`; diff tool synthetic test rc 1 with correct `DIVERGE … field=iters`; bad JSON ⇒ rc 2 `ERROR: bad capture: …`. Native capture with **leaf hooks off** gave **369 JSONL records** and a clean game exit.
**Tree currently does not build**: mid-refactor to function-entry hooks. `capture.h` declares `cap_fn_cb40_hook/cap_fn_heur_hook/cap_fn_timeout_hook`, but `ldhook.cpp:install_capture_sites()` still calls old names and `ldhook_internal.h` still has old `CAP_*_SIG` call-site signatures. Reconcile, and remove throttled `cap_impl:*` debug logs.

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
1. Reconcile `ldhook.cpp`/`ldhook_internal.h` with `cap_fn_*`; rebuild `.pyd`, copy to scratch, run capture with all leaf hooks.
2. If crashing, enable one function-level hook at a time; verify stolen bytes against `game_dis.txt`.
3. Harness check: mask-0 capture ⇒ clear underflow; full capture ⇒ populated `res_port`, `leaf_underflow=0`.
4. Report first diverging `seq` (field, native vs port, branch making 3 nodes at tick 16/unit 3).
5. Fix port defect (≤3 attempts), rebuild, re-run replay diff only; **success = rc 0**.
6. Add short 3-command section to `tools/ldhook/README.md`.

## 5. Commands and paths
```
python3 tools/ldhook/build.py
python3 tools/probes/battle_replay_build.py
cp build/ldhook/ldhook.pyd D:/tmp/cygwin/opencode/ld/ldhook/tree/ldhook.pyd
# PowerShell (env inherited by runner and game):
$env:LD_GAME="D:/tmp/cygwin/opencode/ld/ldhook/tree"; $env:LD_SHOTS="D:/tmp/cygwin/opencode/ld/battle_shots"
$env:LDHOOK_SITE6="0"; $env:LDHOOK_BATTLE_TRACE="0"; $env:LDHOOK_LOG="1"
$env:LDHOOK_BATTLE_CAPTURE="D:/tmp/cygwin/opencode/ld/replay_capture.jsonl"; $env:LDHOOK_BATTLE_CAPTURE_MAX="400"
# add $env:LDHOOK_BATTLE_CAPTURE_LEAF="0" to capture without leaves
powershell -NoProfile -ExecutionPolicy Bypass -File tools/probes/battle_169.ps1 -Tag replay_cap
build/ldhook/battle_replay.exe D:/tmp/cygwin/opencode/ld/replay_capture.jsonl D:/tmp/cygwin/opencode/ld/replay_port.jsonl
python3 tools/probes/battle_replay_diff.py D:/tmp/cygwin/opencode/ld/replay_capture.jsonl D:/tmp/cygwin/opencode/ld/replay_port.jsonl
```
Runner log: `D:/tmp/cygwin/opencode/ld/ldhook/tree/hdlog.txt`; scratch EXE baseline `45e93d46…`, loader already in `ui.pyc`. Format: `cap` + `res` per call; leaf streams (`path/heur/timeout`) in the `res` record (doc deviation); grid = sizes only (pathfinding stubbed). Diff rc: 0 equal, 1 diverge, 2 input error, 3 prefix unmet.

## 6. Warnings
Never delete files; installed game `D:/Games/LethalDreams` READ-ONLY (use scratch tree only). Always `taskkill /F /IM LETHALDREAMS.EXE` after runs. Do not guess slot layouts without `game_dis.txt` / `docs/PLAN_BATTLE_AI_CPP.md` evidence. Keep writes opt-in; verify `check_state.py --quick` after installed-game changes.

## 7. State after 20.11.2026 round (function-entry hooks)
- Build green; leaf hooks at function entry (`CAP_VA_*`, prologue memcmp, `cap_ret_*`). Capture mask 7: 253 calls, no crash (was 0xC0000005 at the first call).
- Capture fixes: latch/exit read `Candidate->pos` (+0) instead of +0xc; parent/nbr via `PathState->pos`; dword bits written decimal (hex broke JSON); timeout recorded as the AL flag (`test al,al` @0x45e153).
- Harness: one canonical LDPos per cell (identity `pos==cmp`), `nbr`/`parent` NULL instead of (-1,-1) stubs, `register_occupied_slot` inserts the same object into map1 (key 0) and map2, truncated trailing line tolerated.
- Diff: first divergence `seq=1 field=iters` (native 116 vs port 71). Port drains all 493 path leaves by iteration 71, so leaf streams desync from call 1 — map1/occupancy reconstruction is not 1:1. `node`/`code` match seq 1..158; first `node` divergence `seq=159`.
- Open: reproduce `FUN_0045d810` insert order (key 0) into map1 so leaf consumption matches, then re-run diff for the tick-16 case.
