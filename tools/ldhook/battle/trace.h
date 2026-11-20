/* battle/trace.h — трассировка боевого тика (LDHOOK_BATTLE_TRACE).
 *
 * Флаг: env `LDHOOK_BATTLE_TRACE=1` (разбор при initldhook, battle_trace_init()).
 * Одна строка на вызов FUN_0044bdd0 (тик одного юнита) — формат зафиксирован
 * планом п. 1.7 и re/battle_ai_ab_method.md §4:
 *
 *   BATTLE tick=%ld unit=%d code=%d u170=%d al=%d out=%d
 *          c1=%d x1=%d y1=%d i1=%ld fx=%d fy=%d lx=%d ly=%d
 *          c2=%d x2=%d y2=%d i2=%ld g=%ld h10k=%ld cost=%ld
 *          trk=%d bd2=%u s1=%ld s2=%ld vis=%ld
 *          m0=%08x m1=%08x m2=%08x us=%I64d ai=%08x
 *
 * Строка пишется на эпилоге 0x44d074 (единственный RET тела). Поля:
 *   tick/unit     — счётчик вызовов 44bdd0 и ordinal юнита (H1 @0x44bdf5);
 *   code/u170/al/out/m0..m2 — эпилог 44bdd0 (H2 @0x44d074);
 *   s1/s2         — [BattleAI+0x70]/[BattleAI+0x7c]; BattleAI = [unit+0x138];
 *   vis           — число входов FUN_0045dd70 за тик (H7 @0x45dd70 + порт
 *                   s6_ai_update, battle/target.cpp);
 *   us/ai         — длительность тика (QPC) и адрес BattleAI; НЕ сравниваются.
 *
 * SENTINEL'ы (решение 2.0 плана): поля, точки хуков которых появятся в
 * следующих фазах, пишутся -1 (для float-бит m0..m2 — 0xFFFFFFFF):
 *   code, c1, x1, y1, i1, fx, fy, lx, ly, c2, x2, y2, i2,
 *   g, h10k, cost, trk, bd2 (bd2 — unsigned, sentinel 0xFFFFFFFF).
 * Гейт Фазы 2 сравнивает только s1, s2, vis.
 *
 * Трап любого разыменования наблюдателя → sentinel'ы + trap (игра не тронута).
 */
#ifndef BATTLE_TRACE_H
#define BATTLE_TRACE_H

#include <windows.h>

#define BATTLE_TRACE_ENV "LDHOOK_BATTLE_TRACE"
#define BATTLE_SEED_ENV  "LDHOOK_SEED"

/* Разбор env при init; ставит/false g_enabled. */
void battle_trace_init(void);
int  battle_trace_enabled(void);

/* H1 (0x44bdf5): ECX=unit — tick++, ordinal юнита, сброс аккумуляторов,
 * отметка времени начала тика. */
void battle_trace_tick_begin(void *unit);

/* H2 (0x44d074): AL, esp кадра (для [esp+0x164] = param_3) —
 * читает поля через g_tick_unit и пишет строку; вызывается ДО воспроизведения
 * украденных байт. */
void battle_trace_tick_end(int al, void *esp);

/* H7 (0x45dd70) и вход порта s6_ai_update: один счётчик vis на тик. */
void battle_trace_visit(void);

/* H4: вход FUN_0045e0a0 (site5 entry / site6 detour) — сброс счётчиков
 * вызова (i1/i2 и endpoint'ов; до подключения их хуков поля = sentinel). */
void battle_trace_e0a0_enter(int filter);

/* R1..R3: LDHOOK_SEED (uint32, decimal или 0x-hex). 1 = задан и разобран. */
int  battle_seed_get(unsigned long *out);

/* Naked-трамплины хуков (определения — battle/trace.cpp). */
void h1_tick_hook(void);
void h2_epilogue_hook(void);
void h7_visit_hook(void);

#endif /* BATTLE_TRACE_H */
