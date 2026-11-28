/* battle/occ.h — наблюдатель занятости клеток мобов (LDHOOK_MOB_OCC).
 *
 * env `LDHOOK_MOB_OCC=1` (или ведущий t/T) — разбор в mob_occ_init() при
 * initldhook. Дефолт выключен: наблюдатель не читает память игры и не печатает.
 * env `LDHOOK_MOB_OCC_EVERY=<K>` — период выборки на юнит, дефолт 10; K<=0 = 1.
 * Первый тик юнита выбирается всегда.
 *
 * Вызов — из H1 (battle_trace_tick_begin, battle/trace.cpp) на входе тика,
 * до сброса полей тика: code = код H3 (0x44cbc5) ПРЕДЫДУЩЕГО тика цикла.
 *
 * Одна строка на выбранный тик:
 *
 *   OCC tick=%ld unit=%d cx=%d cy=%d cz=%d px=%d py=%d pz=%d
 *           t0=%08x t1=%08x t2=%08x s1=%ld s2=%ld code=%d
 *
 *   cx,cy,cz — три dword по цепочке *(unit+0x138) (game_ctx) → +0xc (pos-тройка),
 *              тот же источник, что leaf 45c6f0 и capture v15 `ctx_pos`;
 *   px,py,pz — тройка движения unit+0x68..0x70 (int-биты);
 *   t0..t2   — float-биты цели unit+0xa4..0xac (m0..m2 в BATTLE);
 *   s1,s2    — размеры map1 ([ai+0x70]) и map2 ([ai+0x7c]), ai = unit+0x138;
 *   code     — код H3 текущего (предыдущего) тика; -1 если не записан.
 *
 * Все разыменования под __try; сбой чтения — вся строка sentinel'ами
 * (-1, t0..t2 = 0xFFFFFFFF). Наблюдатель только читает игровую память.
 */
#ifndef BATTLE_OCC_H
#define BATTLE_OCC_H

#include <windows.h>

#define MOB_OCC_ENV        "LDHOOK_MOB_OCC"
#define MOB_OCC_EVERY_ENV  "LDHOOK_MOB_OCC_EVERY"

/* Разбор env при init; иначе наблюдатель полностью инертен. */
void mob_occ_init(void);
int  mob_occ_enabled(void);

/* Один выбранный тик юнита (unit_id — ordinal H1, > 0; tick — номер тика). */
void mob_occ_unit_tick(void *unit, int unit_id, long tick);

#endif /* BATTLE_OCC_H */
