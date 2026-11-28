# TASK — battle trace H2: fix field extraction (Phase 2 gate)

Handoff for another agent. Repo: `LD_REPO` (local clone of this repository), branch `perf-battle`.
Start from commit `2e7fcb9` (pushed). Plan: `docs/PLAN_BATTLE_AI_CPP.md` decisions 1.7 and 2.0.
A/B method reference (scratch, not in git): `<scratch>/re/battle_ai_ab_method.md` §1/§2/§4.

## State

Item 2.7 (trace line, hooks H1/H2/H4/H7, `LDHOOK_SEED` pinning) is implemented and installs:
`install_trace_sites: hooks installed (trace ON)`. Live A/B runs on the scratch tree
`<scratch>/ldhook/tree` (baseline EXE md5 `45e93d46…`) complete all 33 DSL
steps with **no crash**. `BATTLE` lines are produced (1022 in native run A, 55 in port run B).

## Defect 1 — H2 reads wrong data (blocking)

Observed (verbatim tail, run A):
`s1=1254533158 s2=45182184 ai=00000000 us=1044` and `code=-1 out=-1 g=-1 h10k=-1`
(first ticks: `s1=-1 s2=-1 … m0=19b44e58 m1=19b4aa50 m2=19b4adb8`).
Expected: `s1` = `[this+0x70]`, `s2` = `[this+0x7c]` (small map sizes), `ai` = BattleAI pointer,
`m0..m2` = `[unit+0xa4..0xac]` float bits.

The H2 hook (`tools/ldhook/battle/trace.cpp`, `h2_epilogue_hook`) currently recovers `unit`
from the saved `EBP` slot of its own `pushad` frame (`[esp+8]`) and assumes `EBP == unit`.
Two independent facts say this assumption is wrong or at least fragile:
- the values above are self-consistent garbage (a pointer pair, not map sizes), so `EBP`
  resolves to something that is not the unit;
- the listing (`<scratch>/re/game_dis.txt`, file offset = VA − 0x400000)
  shows `pop ebp` at `0x44d07d`, i.e. `EBP` is merely a callee-saved register restored at the
  epilogue — nothing guarantees it still holds `unit` at `0x44d074`.

**Recommended fix:** stop inferring `unit` at the epilogue. H1 (`0x44bdf5`) already sees
`ECX = unit`; store it in a per-tick global at `battle_trace_tick_begin` and read it in H2.
Then take `this = *(void**)((char*)unit + 0x138)` (as in ab_method §1) and read all fields
through it. Verify the `pushad` saved-slot offsets empirically if you keep any of them
(`[esp+12]` was measured to be the pre-`pushad` ESP; the `AL` slot and the `param_3` slot
`[esp+0x164]` must be re-checked the same way, e.g. by printing the raw values of two slots
that are known from the listing: `AL` at epilogue and `[esp+0x164]` = `param_3`).

`us=1` on the first tick and `us=1044` later look plausible; keep them.

## Defect 2 — port run is far shorter than native (may be real)

Run B (`LDHOOK_SITE6=1`) produces only 55 `BATTLE` lines vs 1022 in run A, with `vis=1` on
every tick and `m0=m1=m2=c61c4000` (the `-9990.0f` sentinel of `45e1d0`, "no movement").
This is consistent with the known Phase-2 gap (`s6_map_update_neighbors` removed; candidate
metrics are written only by Phase 3/native `dd00` map1 insertion), but it may also be an
artifact of the broken H2 reading. Re-measure after Defect 1; if the tick counts still differ
by an order of magnitude, report it as a separate finding with the port call counters
(`site6` windows, `calls=…`) — do not try to "fix" the port inside this task.

## Gate to pass

```
python tools/probes/trace_diff.py <A_hdlog> <B_hdlog> --tail=30 --fields=s1,s2,vis   # rc 0
```
plus `python tools/check_state.py --quick` rc 0 and no crash in either run.

## Procedure

1. Read `tools/ldhook/battle/trace.cpp` (H1/H2/H4/H7), `tools/ldhook/ldhook.cpp`
   (`install_trace_sites`, `write_jmp`), `docs/PLAN_BATTLE_AI_CPP.md` 1.7/2.0.
2. Edit, then `PYTHONDONTWRITEBYTECODE=1 python tools/ldhook/build.py` (rc 0),
   `python tools/ldhook/check_sites.py` (rc 0, baseline `installable`).
3. Deploy `build/ldhook/ldhook.pyd` into `<scratch>/ldhook/tree` and run
   `tools/probes/battle_test.ps1` twice on the same save, same params:
   - A: `LDHOOK_SITE6=0 LDHOOK_BATTLE_TRACE=1 LDHOOK_LOG=1 LDHOOK_SEED=1234567890`
   - B: same with `LDHOOK_SITE6=1`
   Copy each `hdlog.txt` aside; kill the game (`taskkill /F /IM LETHALDREAMS.EXE`) right
   after each run.
4. Iterate until the gate passes. Max 3 fix attempts, then stop and report.

## Hard rules

- The installed game `config[game.root]` is read-only; all runs use the scratch tree.
- Never delete or move project files; never `rm -rf`, never force-push, no `git reset --hard`.
- One fix = one commit, message ≤2 lines, then `git push origin perf-battle`.
- Do not edit `docs/**` except this status line; the orchestrator records decisions.
- Previous logs for comparison: `<scratch>/ldhook/logs/h2fix/{A,B}_hdlog.txt`.
