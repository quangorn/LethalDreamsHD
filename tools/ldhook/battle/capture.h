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
 * v5 добавляет одну запись `grid` ПЕРЕД первой `cap`: статическая сетка
 * pathfinding'а (уровни, размеры, cells/rows, ключи ячеек, статусы tree_lookup)
 * и внешние входы эвристики (world-флаги, vtable-значения). Захват делается
 * один раз: сетка статична на бой; dyn-occupancy уже покрыта `occ`/`occ_nodes`.
 * `res.timer` — точные значения timeGetTime для каждого вызова c5a0 (внешние
 * входы предиката таймаута). v1..v4 не содержат этих записей.
 *
 * v3 добавляет к cap: `ai_words` — сырой скалярный блок BattleAI +0x00..+0x7f
 * (32 dword; dd70/e0a0 читают эти поля), и `occ_nodes` — для каждого ключа
 * map2 либо null, либо PathState* значения (w[22] dword, ptr[11] identity
 * LDPos*, координаты parent/nbr). v4 добавляет к res: `ai_words_after` — тот же
 * скалярный блок, снятый ПОСЛЕ вызова (симметрично cap.ai_words); v1/v2/v3-файлы
 * этого ключа не содержат — харнесс пропускает сравнение.
 *
 * v7 добавляет к res.leaf: `scale` — биты float world[0x120] на входе каждого
 * cb40-вызова (цепочка self→[0]→+4→+0x120, native @0x45c42b..0x45c430), и
 * `scale_ok` — 1, если значение снято. Харнесс подаёт его world-масштабу
 * перед каждым cb40-вызовом порта; при отсутствии ключа масштаб статический.
 *
 * v9 добавляет пару записей `eee0_cap`/`eee0_res` на каждый вызов FUN_0045eee0
 * (`s6_unit_ai_tick`, хук H3 @0x44cbc5/0x44cbd0): входные 7 dword `rec+0x00..0x18`,
 * pre-call поля `self+0x28/+0x2c/+0x30/+0x34/+0x38/+0x68/+0x6c` и `ctx_flags`,
 * а также на выходе — EAX, `self+0x2c` (grid-якорь `FUN_00458050`, +0x14),
 * `+0x30/+0x34/+0x38` и снапшоты `map1_after`/`occ_after`/`ai_words_after`.
 *
 * v10 добавляет пару `bdd0_cap`/`bdd0_res` на каждый вызов FUN_0044bdd0
 * (диспетчер боевого AI, H1 @0x44bdf5 / H2 @0x44d074): аргументы кадра
 * `param_1/param_2/param_3` ([esp+0x15c..0x164]), `unit+0x138` (BattleAI),
 * `unit+0x170`, биты гейта `unit+0x114`, а на выходе — AL, `u170`, `*(int*)param_3`
 * и `m0/m1/m2` (+0xa4..0xac). Пары связываются по независимому `seq`.
 *
 * v11 (пара bdd0_*; остальные записи — v10) добавляет в res слоты кадра
 * `frame+0x98` (`anc` — grid-якорь 458050, 0x44c7e1), `frame+0x88` (`tag` —
 * grid-record, 0x44c774), `frame+0x2c..0x34` (`tri0..tri2` — pos-тройка R14)
 * и `frame+0x38` (`a4n` — arg4 458050, 0x44c387 / B75 0x44c6ed). native и порт
 * пишут те же значения, правки тела не требуется.
 *
 * v12 (opt-in, env LDHOOK_BATTLE_CAPTURE_SNAP=1; только этот env-флаг, дефолт
 * выключен) добавляет два рода записей `v:12`:
 *   `bdd0_in`  — снапшот памяти, которую читает тело 44bdd0, на H1 ДО тела:
 *                `frame_b64` (вся область кадра 0x168 Б = локалы + param_1/2/3),
 *                `unit_b64` (0x180 Б от unit; все чтения unit в tick.cpp до
 *                +0x17f), `ai_b64` (0x80 Б ВСТРОЕННОГО BattleAI по адресу
 *                unit+0x138 без deref; `ai_ctx` — значение `*(unit+0x138)`,
 *                game_ctx), лента `band_begin/end/cap/dirty` (BattleAI+0x5c/
 *                0x60/0x64/0x7c ⇒ unit+0x194/0x198/0x19c/0x1b4) + `band_count`,
 *                плюс v10-поля p1/p2/p3/u170/gate114;
 *   `bdd0_leaf` — окно вызовов 10 логических листьев в порядке вызова внутри
 *                тика (`order` 0-based): va/ecx/args/ret. Точки — те же
 *                `native::`-обёртки (новых детуров нет). ВАЖНО: это окно, а не
 *                чистое тело — в него попадают и те же листья, которые зовут
 *                move.cpp:392 (`grid_anchor`) и target.cpp:817 (`grid_query`)
 *                внутри bdd0-окна; харнесс сопоставляет по паре va+order в
 *                пределах seq и не считает поток исчерпывающим для тела.
 *   `bdd0_native` — результат каждого НЕ-листового RECORD-вызова тела (125
 *                сайтов через хуки native_sites.*, `0x44cbd0` — через H3):
 *                site/va/ecx/args/ret/ret_kind (+ `w` для указателей). `order`
 *                общий с bdd0_leaf; `slot` (u+0x28/0x2c/0x38/0x40) — косвенный
 *                vtable-вызов. 77 NOOP_OK-сайтов не записываются (stub).
 * Остальные роды (`cap`, `res`, `grid`, `eee0_*`, пара `bdd0_cap/res`) и их
 * версии не меняются: v10/v11-файлы читаются прежними инструментами.
 */
#ifndef BATTLE_CAPTURE_H
#define BATTLE_CAPTURE_H

#include <windows.h>

#define BATTLE_CAPTURE_ENV     "LDHOOK_BATTLE_CAPTURE"
#define BATTLE_CAPTURE_MAX_ENV "LDHOOK_BATTLE_CAPTURE_MAX"
/* v12: снапшот входа `bdd0_in` + поток листьев `bdd0_leaf` (дефолт выключен,
 * обычные прогоны остаются малыми). */
#define BATTLE_CAPTURE_SNAP_ENV "LDHOOK_BATTLE_CAPTURE_SNAP"
/* Слой e0a0 (cap/res/grid/eee0_cap+res/листья cb40-c690-c5a0) пишет карты
 * целиком и растёт с занятостью сетки — для захвата слоя 44bdd0 он не нужен.
 * LDHOOK_BATTLE_CAPTURE_E0=0 выключает его и держит запись плоской. */
#define BATTLE_CAPTURE_E0_ENV "LDHOOK_BATTLE_CAPTURE_E0"

#define CAP_LEAF_PATH    0   /* FUN_0045cb40 — результат поиска пути */
#define CAP_LEAF_HEUR    1   /* FUN_0045c690 — эвристика */
#define CAP_LEAF_TIMEOUT 2   /* FUN_0045c5a0 — предикат таймаута */

void battle_capture_init(void);
int  battle_capture_enabled(void);
/* v12: LDHOOK_BATTLE_CAPTURE_SNAP=1 — включать снапшот и хуки call-сайтов. */
int  battle_capture_snap_enabled(void);

/* Адреса оригиналов листьев (устанавливаются install_capture_sites). */
void battle_capture_set_leaf_fns(void *cb40, void *heur, void *timeout);

/* Точки захвата, вызываемые из trace H1 и site5 (native-режим). */
void battle_capture_tick_begin(void *unit, int tick, int unit_id);
void battle_capture_e0a0_enter(void *this_, int filter, void *cmp,
                               void *timeout_bits, void *out_code);
void battle_capture_latch(void *cand);
void battle_capture_e0a0_exit(void *node);

/* v9: Per-call захват FUN_0045eee0 (s6_unit_ai_tick) через H3 (@0x44cbc5). */
void battle_capture_eee0_enter(void *self, void *rec);
void battle_capture_eee0_exit(int code, void *self);

/* v10/v11: Per-call захват FUN_0044bdd0 (диспетчер боевого AI) — H1/H2.
 * esp — ESP кадра 44bdd0 на входе хука (E0-0x158); аргументы на +0x15c..,
 * v11-слоты читаются от того же base (frame+0x98/0x88/0x2c..0x34/0x38). */
void battle_capture_bdd0_enter(void *self, void *esp);
void battle_capture_bdd0_exit(int al, void *esp);

/* v12: leaf-поток тела 44bdd0. Вызывается из `native::`-обёрток 10 логических
 * листьев (45c6f0, 45c370, 45af50, 45c730, 45b150, 45b3e0, 403e80, 458050,
 * 45e1d0, 44bbf0) после их нативного вызова; вне тика 44bdd0 и без
 * LDHOOK_BATTLE_CAPTURE_SNAP=1 — немедленный выход (поведение вызова не
 * меняется). va — VA листа, ecx — его приёмник (ECX), args/nargs — стек-
 * аргументы (float-листья передают биты), ret — EAX/AL/ST(0)-биты, которые
 * читает тело.
 *
 * v12 (аддитивно): `w`/`nw` — dword'ы, которые лист записал ЧЕРЕЗ указатель
 * (45c6f0 — out-тройка, 45e1d0 — возвращённая тройка), чтобы офлайн-харнесс
 * воспроизвёл побочную запись, не читая игровую память. NULL/0 — ключа нет. */
void battle_capture_bdd0_leaf(int va, const void *ecx, const int *args,
                              int nargs, int ret, const int *w, int nw);

/* v12: запись листа с явными seq/order (хуки call-сайтов: order выдаётся на
 * входе листа, seq — из кадра 44bdd0 на входе; trap=1 — запись неполная). */
void battle_capture_bdd0_leaf_rec(int site, int va, const void *ecx,
                                  const int *args, int nargs, int ret,
                                  const int *w, int nw, int seq, int order,
                                  int trap);

/* v12: запись результата НЕ-листового нативного вызова тела (`bdd0_native`).
 * Источник — хуки call-сайтов native_sites.*; kind — NS_K_*, slot — NS_SLOT_*
 * (0 = прямой сайт, иначе vtable-слот), w/nw — dword'ы, прочитанные через
 * возвращённый указатель. order общий с bdd0_leaf (единый поток вызовов). */
void battle_capture_bdd0_native_rec(int site, int target, int slot, int kind,
                                    int ecx, const int *args, int nargs,
                                    int ret, const int *w, int nw,
                                    const int *ww, int nww,
                                    int seq, int order, int trap);

/* v12: сайт не захукался (5-байтовая проверка/резолв) — маркер в захвате. */
void battle_capture_native_skipped(int site, int target, const char *why);

/* Регистрация результата листа; value — EAX оригинала, words — 5 dword
 * возвращённого объекта (только для CAP_LEAF_PATH, value != 0). */
void battle_capture_leaf(int kind, int value, const int *words);

/* Naked-трамплины листьев: хук на входе функции + перехват возврата
 * (определения — battle/capture.cpp). */
void cap_fn_cb40_hook(void);
void cap_fn_heur_hook(void);
void cap_fn_timeout_hook(void);

#endif /* BATTLE_CAPTURE_H */
