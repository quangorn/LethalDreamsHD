/* battle/battle.h — объявления слоёв боевого AI (PLAN_BATTLE_AI_CPP.md).
 * Слои: сетка (grid.cpp), pathfinding (path.cpp), поиск цели (target.cpp),
 * юнит-AI (move.cpp, Фаза 4), тик (tick.cpp, Фаза 5), trace-каркас.
 * Имена s6_* взяты из ldhook.cpp (перенос кода); переименование
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

/* FUN_00457e20 — поиск ячейки сетки по ключу: RET 0x10, ECX = GridLevel*,
 * arg2 не читается (re/leaf_457e20.md). Возврат — GridCell* или NULL.
 * Вызывается только из s6_grid_anchor_lookup (сайт 0x45811a). */
GridCell *s6_grid_key_lookup(GridLevel *grid, float arg1, int arg2, float arg3, int key);

/* FUN_00456b60 — голова цепочки со статусом 2: ECX = GridCell*, стек-аргумент
 * world, RET 4; возврат — узел или NULL (re/leaf_456b60.md). Вызывается только
 * из s6_grid_anchor_lookup (сайт 0x458142). */
GridCell *s6_grid_chain_head(GridCell *node, void *world);

/* FUN_00458050 — grid-якорь (re/leaf_458050.md): RET 0x18, ECX = GridLevel*.
 * x (S+4) и y_coord (S+0xc) — координаты: индекс = trunc((coord − scale)/
 * scale_mul) истинным FDIV. y_metric (S+8) в индексы НЕ входит: только
 * |min_metric − y_metric| в скане ветки node == 0 (0x4581b9) и мёртвый
 * block[4] вызова 0x457e20. y_coord — живой block[8] этого вызова (по нему
 * 0x457e20 считает свой y-индекс). node — узел дерева (0 = «дерева нет»:
 * скан цепочки +0x24 по минимуму |min_metric − y_metric|), flag — out-байт
 * («якорь найден через tree+key»; в ветке node == 0 не пишется), world —
 * ctx для 0x449160. EAX = GridCell* / 0. */
GridCell *s6_grid_anchor_lookup(GridLevel *grid, float x, float y_metric,
                                float y_coord, void *node, unsigned char *flag,
                                void *world);

/* ---- target.cpp: поиск цели FUN_0045e0a0 и его callees ---- */
int s6_distance2(void *a, void *b);                     /* FUN_0045c480 */
LDObject *s6_scan_map(void *this_, int filter);         /* FUN_0045ce80 */
int s6_check_distance(void *this_, void *cand);         /* FUN_0045c880 */
int s6_check_timeout(void *this_, void *arg);           /* FUN_0045c5a0 */
unsigned long s6_time_ms(void);                         /* timeGetTime (IAT 0x4ec27c) — таймер c5a0/c570 */
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

/* FUN_0045c4b0 — copy полей PathState-окна по маске (бит i → поле +4i);
 * нативный порядок (src, dst, mask), порт — (dst, src, mask). */
void s6_copy_fields(int dst, int src_, unsigned int mask);

/* FUN_0045e0a0 — точка входа офлайн-харнесса (tools/probes/battle_replay.cpp). */
void *__cdecl s6_target_search(void *this_, void *arg0, void *arg1,
                               void *arg2, void *arg3);

/* FUN_0045b150 — float-гейт ленты (re/leaf_45b150.md, байты сверены с
 * baseline; pos у концов РАЗНЫЕ — CONTRADICTIONS #21): минимум d² от точки
 * (px, py) до полилинии ленты. Элемент 24 Б: +0x00/+0x08 — координаты узла,
 * +0x14 — LDPos*; узел переводится в координаты через 0x456b20
 * (s6_grid_scale_compute). BEST init 100.0f — sentinel «ни один отрезок не
 * подошёл». __thiscall: ECX = лента (BattleAI+0x3c), 3 стек-аргумента
 * (arg2 не читается), `ret 0xc`, результат — ST(0) = float32-биты BEST.
 * Сайты тела тика — 0x44c4cb (B60) и 0x44c5e0 (B67). */
float s6_band_interp_gate(const void *band, float px, int dead_arg, float py);

/* ---- move.cpp: тик юнит-AI FUN_0045eee0 (s6_unit_ai_tick, Фаза 4) ---- */
int s6_unit_ai_tick(BattleAI *self, void *rec_arg);       /* FUN_0045eee0 — порт 4.1 */

/* ---- tick.cpp: верхний уровень FUN_0044bdd0 (Фаза 5.1b) ---- */
/* env-гейт LDHOOK_SITE5 (без учёта полноты тела — для установщика H1/H2). */
int b0_env_on(void);
/* Гейт порта: LDHOOK_SITE5=1 И полное тело (B0_BODY_COMPLETE). */
int b0_port_enabled(void);
/* Вход порта из H1 (0x44bdf5): unit = ECX, frame = ESP кадра (S−0x158),
 * возврат — AL (0 на всех UNPORTED-путях R1/R2). */
int b0_battle_ai_dispatch(void *unit, unsigned char *frame);

/* ---- trace.cpp: trace-каркас боевого тика (LDHOOK_BATTLE_TRACE) ---- */
void battle_trace_init(void);

#endif /* BATTLE_H */
