/* battle/battle.h — объявления слоёв боевого AI (PLAN_BATTLE_AI_CPP.md).
 * Слои: сетка (grid.cpp), pathfinding (path.cpp), поиск цели (target.cpp),
 * движение (move.cpp, Фаза 4), тик (tick.cpp, Фаза 5), trace-каркас.
 * Имена s6_* — исторические (код перенесён 1:1 из ldhook.cpp); переименование
 * по дизасму — Фазы 2–5.
 */
#ifndef BATTLE_H
#define BATTLE_H

#include "structs.h"

/* ---- path.cpp: std::map-ассистенты и обход дерева пути ---- */

TreeNode *s6_tree_next(TreeNode *node);                 /* порт FUN_0047f800 */
TreeNode *s6_map_lower_bound(TreeNode *head, unsigned int key);   /* FUN_00434bc0 */
PathState *s6_pfs_ctor(void);                           /* FUN_0045c2e0 (аллоцирует 0x58) */

/* FUN_0045c390 — знаковый max(a,b). */
int s6_imax(int a, int b);

GridCell *s6_path_tree_search(BattleAI *self, GridCell *p1, int p2, int p3,
                              float *p4, int *p5, int *p6, int p7); /* FUN_0045c3a0 — возврат кандидата */
/* FUN_0045cb40 (RET 8, 2 стек-арга pos/&spec; arg2 не читается). Рамка cb40
 * base = dd70 base − 88 (re/esp_scan.py); cb40 слот k ↔ dd70 slot (k − 88),
 * alias'а слотов dd70 нет. loop_base L[0x1c] = −y_radius пишется @0x45cbe9
 * до чтения @0x45cc86; min_slot L[0x24] = −x_radius пишется @0x45cbca до
 * чтения @0x45cd1f; stale-read'ов нет — оба моделируются internally по
 * ESP-леджеру. */
void *s6_path_search(BattleAI *self, PathState *spec);
/* A/B-диагностика cb40: 7 счётчиков исходов (first,sign,bound,grid,recur,gate,res). */
void s6_pfs_snapshot(long *out7);

/* ---- grid.cpp: доступ к сетке (FUN_00456ed0 / FUN_004570d0 / FUN_00456a70) ---- */
GridCell *s6_grid_cell(GridLevel *level, int index);       /* FUN_00456ed0 */
GridCell *s6_grid_lookup(GridLevel *grid, int x, int y);   /* FUN_004570d0 (getter) */
GridCell *s6_chain_last(GridCell *node);                   /* FUN_00456a70 */

/* FUN_00456b20 (п. 2.5): масштабная запись позиции уровня сетки. Receiver — LDPos*
 * (level = [pos+0x1c]); out[2] — float-координаты записи. EAX на выходе = out,
 * читается вызывающим (@0x45c62d/0x45c668). */
float *s6_grid_scale_compute(LDPos *pos, float *out);

/* ---- target.cpp: поиск цели FUN_0045e0a0 и его callees ---- */
int s6_distance2(void *a, void *b);                     /* FUN_0045c480 */
LDObject *s6_scan_map(void *this_, int filter);         /* FUN_0045ce80 */
int s6_check_distance(void *this_, void *cand);         /* FUN_0045c880 */
int s6_check_timeout(void *this_, void *arg);           /* FUN_0045c5a0 */
int s6_ai_update(void *this_, void *cand, int arg2);    /* FUN_0045dd70 */

/* П. 2.5: гейт эвристики (FUN_0045c690) и эвристика (FUN_0045c5e0). Slot —
 * base spec-окна dd70 (PathState-форма, L[0x3c]); эвристика пишет в него
 * float (@0x45c658 FSTP m32; запись мёртвая — dd70 перезаписывает L[0x3c]).
 * Модель слота — PathState-раскладка: операнды +0x0c/+0x10/+0x14 (dd70
 * L[0x48]/L[0x4c]/L[0x50]) пишутся native store-sites dd70 (п. 2.6:
 * spec[0x0c]=0 @0x45df52, spec[0x10]=cur->[0x10] @0x45df13/0x45dfe7,
 * spec[0x14]=0 @0x45df5a). Возврат гейта — результат FUN_0044bbf0 как есть
 * (не 0/1), 0 — только на гейтах. Разыменования идут под __try s6_ai_update
 * (SEH у оригиналов нет). */
int s6_heuristic_gate(void *this_, int *slot);           /* FUN_0045c690 */
int s6_heuristic(void *this_, int *slot);                /* FUN_0045c5e0 */

/* FUN_0045e0a0 — точка входа офлайн-харнесса (tools/probes/battle_replay.cpp). */
void *__cdecl s6_target_search(void *this_, void *arg0, void *arg1,
                               void *arg2, void *arg3);

/* ---- tick.cpp: trace-каркас боевого тика (LDHOOK_BATTLE_TRACE) ---- */
void battle_trace_init(void);
void battle_trace_tick(long tick_no, void *target, int search_code,
                       int move_x, int move_y,
                       long g, long h, long cost,
                       long nodes, LONGLONG us);

#endif /* BATTLE_H */
