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
 *   code/trk/bd2  — H3 @0x44cbc5: код, возвращённый 45eee0, [this+0x30],
 *                   [this+0x34] (this = ESI = &unit+0x138);
 *   c1,x1,y1,i1,fx,fy,lx,ly — вызов e0a0 №1 (filter=1), c2,x2,y2,i2 — №2
 *                   (filter=2): H5 (эпилоги `83 C4 34 C2 10 00`) + H6
 *                   (хвост цикла 0x45e163); g/h10k/cost — узел последнего
 *                   вызова (H5: +0x18/+0x20/+0x24);
 *   u170/al/out/m0..m2 — эпилог 44bdd0 (H2 @0x44d074);
 *   s1/s2         — [BattleAI+0x70]/[BattleAI+0x7c]; BattleAI = [unit+0x138];
 *   vis           — число входов FUN_0045dd70 за тик (H7 @0x45dd70 + порт
 *                   s6_ai_update, battle/target.cpp);
 *   us/ai         — длительность тика (QPC) и адрес BattleAI; НЕ сравниваются.
 *
 * STATEFP (только при LDHOOK_BATTLE_TRACE=1) — по строке на вход e0a0 (H4):
 *
 *   STATEFP tick=%ld unit=%d call=%ld filter=%d s1=%ld s2=%ld
 *           h1=%08x h2=%08x vis=%ld trap=%d
 *
 * s1/s2 — размеры map1 ([ai+0x70]) и map2 ([ai+0x7c]); h1 — FNV-1a 32 over
 * map1 in-order (pos.x, pos.y, filter, g +0x18, dist2 +0x1c, h10000 +0x20,
 * metric/cost +0x24); h2 — FNV-1a 32 over map2 in-order occupancy keys;
 * vis — счётчик входов 45dd70/s6_ai_update в текущем тике. FNV-1a: basis
 * 0x811c9dc5, prime 0x01000193, int32 подаётся 4 байтами little-endian.
 * Указатели в хэш не входят; trap=1 означает обрыв разыменования наблюдателя.
 *
 * Носители H5/H6 (инвариант 5, ab_method §2): в native-режиме — трамплины
 * site5 (`s5_epi_hook`/`s5_tail_hook`, battle/target.cpp); в режиме порта
 * (site6) — сам `s6_target_search`. Отдельных детуров 0x45e0a0/0x45e163
 * наблюдатели не ставят. Идентификация «вызов №1 / №2» — счётчик H4
 * (`battle_trace_e0a0_enter`), сброс на входе тика (H1).
 *
 * SENTINEL'ы: неприменимое поле = -1 (bd2 — unsigned: 0xFFFFFFFF; float-биты
 * m0..m2 — 0xFFFFFFFF; bd2 читается на H3, поэтому пишется реально).
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

/* H3 (0x44cbc5): EAX = код, возвращённый FUN_0045eee0; ai = ESI =
 * &unit+0x138 (BattleAI): [ai+0x30] → trk, [ai+0x34] → bd2. */
void battle_trace_code(int code, void *ai);

/* H4: вход FUN_0045e0a0 (site5 entry / site6 detour) — сброс счётчиков вызова
 * и выбор слота 1/2 (this сохраняется для H5: [this+0x38] → c1/c2). */
void battle_trace_e0a0_enter(void *this_, int filter);

/* H5: выход FUN_0045e0a0 (все эпилоги) — возвращённый узел: pos (x,y,
 * LDPos +0x0c/+0x10), g (+0x18), h10k (+0x20), cost (+0x24). node = NULL
 * (пустой скан) → поля узла остаются sentinel'ами. */
void battle_trace_e0a0_exit(void *node);

/* H6: хвост цикла 0x45e163 (или rescan порта) — EAX = кандидат следующей
 * итерации: счёт латчей (i1/i2), первый/последний непустой кандидат
 * ([obj+0x00] → LDPos +0x0c/+0x10) → fx/fy, lx/ly. */
void battle_trace_e0a0_latch(void *cand);

/* R1..R3: LDHOOK_SEED (uint32, decimal или 0x-hex). 1 = задан и разобран. */
int  battle_seed_get(unsigned long *out);

/* Naked-трамплины хуков (определения — battle/trace.cpp). */
void h1_tick_hook(void);
void h2_epilogue_hook(void);
void h3_code_hook(void);
void h7_visit_hook(void);

#endif /* BATTLE_TRACE_H */
