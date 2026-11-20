/* battle/capture.h — офлайн-захват входа/выхода FUN_0045e0a0 (п. 1.7 §3.2).
 *
 * Флаг: env LDHOOK_BATTLE_CAPTURE=<путь> (разбор в battle_capture_init()).
 * Запись — JSON Lines: `cap` на входе e0a0 и `res` на выходе. Всё opt-in:
 * без флага модуль не пишет файл, хуки листьев не ставятся, счётчики не
 * incremented (вызовы — no-op после проверки g_enabled).
 *
 * Дополнительный env LDHOOK_BATTLE_CAPTURE_MAX=<n> — предел числа пар
 * cap/res в файле (по умолчанию 400), чтобы прогон не разрастался.
 *
 * Идентичность — по позиции (x,y); float — битовыми паттернами; указатели
 * в файл не пишутся (кроме «есть/out_ptr», pos_addr/cmp_addr и identity
 * указателей значения map2 — они нужны харнессу только для подмены).
 *
 * v3 добавляет к cap: `ai_words` — сырой скалярный блок BattleAI +0x00..+0x7f
 * (32 dword; dd70/e0a0 читают эти поля), и `occ_nodes` — для каждого ключа
 * map2 либо null, либо PathState* значения (w[22] dword, ptr[11] identity
 * LDPos*, координаты parent/nbr). v1/v2 записи харнессом читаются как раньше.
 */
#ifndef BATTLE_CAPTURE_H
#define BATTLE_CAPTURE_H

#include <windows.h>

#define BATTLE_CAPTURE_ENV     "LDHOOK_BATTLE_CAPTURE"
#define BATTLE_CAPTURE_MAX_ENV "LDHOOK_BATTLE_CAPTURE_MAX"

#define CAP_LEAF_PATH    0   /* FUN_0045cb40 — результат поиска пути */
#define CAP_LEAF_HEUR    1   /* FUN_0045c690 — эвристика */
#define CAP_LEAF_TIMEOUT 2   /* FUN_0045c5a0 — предикат таймаута */

void battle_capture_init(void);
int  battle_capture_enabled(void);

/* Адреса оригиналов листьев (устанавливаются install_capture_sites). */
void battle_capture_set_leaf_fns(void *cb40, void *heur, void *timeout);

/* Точки захвата, вызываемые из trace H1 и site5 (native-режим). */
void battle_capture_tick_begin(void *unit, int tick, int unit_id);
void battle_capture_e0a0_enter(void *this_, int filter, void *cmp,
                               void *timeout_bits, void *out_code);
void battle_capture_latch(void *cand);
void battle_capture_e0a0_exit(void *node);

/* Регистрация результата листа; value — EAX оригинала, words — 5 dword
 * возвращённого объекта (только для CAP_LEAF_PATH, value != 0). */
void battle_capture_leaf(int kind, int value, const int *words);

/* Naked-трамплины листьев: хук на входе функции + перехват возврата
 * (определения — battle/capture.cpp). */
void cap_fn_cb40_hook(void);
void cap_fn_heur_hook(void);
void cap_fn_timeout_hook(void);

#endif /* BATTLE_CAPTURE_H */
