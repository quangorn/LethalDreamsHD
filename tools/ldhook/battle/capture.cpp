/* battle/capture.cpp — офлайн-захват FUN_0045e0a0 (battle_ai_ab_method.md §3.2).
 *
 * Пишет JSON Lines: `cap` (вход вызова: аргументы, scalar-поля BattleAI,
 * полный map1, map2-ключи, описание сетки) и `res` (выход: узел, код,
 * итерации, iter_seq из H6, map1_after, occ_after + поток результатов
 * нативных листьев cb40/c690/c5a0). Чтение игровой памяти — под __try/__except.
 *
 * Листья перехватываются на ВХОДЕ их функций (E9, 5 Б; пролог воспроизводит
 * трамплин, возврат перехватывается cap_ret_*): EAX оригинала логируется в
 * поток `leaf` текущего вызова. Харнесс подменяет эти вызовы stub'ами,
 * возвращающими захваченные значения в том же порядке (grid/pathfinding-сетка
 * не реконструируется — это открытый вопрос Фазы 2.1).
 *
 * Всё opt-in: без LDHOOK_BATTLE_CAPTURE файл не открывается, хуки листьев
 * не ставятся, impl-функции возвращаются сразу.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "capture.h"
#include "leaf_sites.h"
#include "native.h"
#include "native_sites.h"

#define CAP_MAX_CALLS   400        /* дефолтный предел пар cap/res */
#define CAP_MAX_NODES   20000      /* предохранитель обхода map1/map2 */
#define CAP_MAX_LEAF    32768      /* предохранитель потока листьев */
#define CAP_MAX_ITER    65536      /* предохранитель iter_seq (H6-латч) */
#define CAP_WORDS       5          /* dword'ы возвращённого cb40-объекта */
#define CAP_GATE_WORDS  4          /* v6: out, thr, min(L[0x10]), max(L[0x18]) */
#define CAP_AI_WORDS    32         /* сырой скалярный блок BattleAI +0x00..+0x7f */
#define CAP_PS_WORDS    22         /* PathState 0x58 / 4 (значение узла map2) */
#define CAP_MAX_GRID_CELLS 200000  /* потолок статической сетки (v5) */
#define CAP_MAX_TREE_DELTA 50000   /* потолок дельт world+0x104 за вызов */
#define CAP_MAX_CELL_DELTA 20000   /* потолок дельт ячеек за вызов */
#define CAP_VERSION     10         /* версия cap/res; v1..v9 — старые захваты */
#define CAP_BDD0_VER    11         /* v11: пара bdd0_cap/bdd0_res (остальные — v10) */
#define CAP_CHAIN_MAX   8          /* v8: узлов цепочки c3a0 на один cb40-вызов */
#define CAP_CHAIN_WORDS 7          /* idx,min,max,key,next,chain,status */
/* v10: кадр FUN_0044bdd0 (оба хука стоят на ESP = E0-0x158; аргументы
 * вызывающего лежат выше возвратного адреса). */
#define CAP_BDD0_P1     0x15c      /* [esp] = param_1 (гейт: AL = байт) */
#define CAP_BDD0_P2     0x160      /* [esp] = param_2 (scratch) */
#define CAP_BDD0_P3     0x164      /* [esp] = param_3 (указатель out-кода) */
/* v11: слоты кадра 44bdd0 (тот же base, что и CAP_BDD0_P*). native и b0-порт
 * пишут их одинаково: anc = grid-якорь 458050 (0x44c7e1; при NULL — ноль),
 * tag = grid-record (0x44c774 = 0xeb), tri0..2 = pos-тройка R14 (0x44c8f4..
 * 0x44c8ff), a4n = arg4 458050: player_get-указатель (B48 0x44c387) или 0
 * (B75 0x44c6ed). */
#define CAP_BDD0_ANC    0x98
#define CAP_BDD0_TAG    0x88
#define CAP_BDD0_TRI0   0x2c
#define CAP_BDD0_TRI1   0x30
#define CAP_BDD0_TRI2   0x34
#define CAP_BDD0_A4     0x38
/* v12: снапшот входа (kind bdd0_in) и поток логических листьев (bdd0_leaf).
 * Версия только этих двух родов; пара bdd0_cap/res остаётся CAP_BDD0_VER (11),
 * cap/res/grid/eee0_* — CAP_VERSION (10). */
#define CAP_BDD0_SNAP_VER   18         /* v18: пара float 456b20 в bdd0_leaf
                                        * (LS_K_INT_W2_ARG1, w[0..1] = два dword по
                                        * out-arg1) на сайтах 0x45e297/0x45e2ab/
                                        * 0x45e49b/0x45e4b1 + аддитивный
                                        * "bdd0_cand" (снапшот PathState 0x58 на
                                        * выходе 45ce80) на сайтах 0x45e443/0x45e487;
                                        * v17: + 20 внутренних сайтов листа и
                                        * "bdd0_site_pf"; v16: + "band_pos" + grid */
#define CAP_BDD0_FRAME_SIZE 0x168  /* локалы 0x00..0x15b + param_1/2/3 (0x15c/0x160/0x164) */
/* unit-блок 0x180: покрывает все чтения unit из tick.cpp (максимум +0x170;
 * +0x138 — встроенный BattleAI, снимается отдельным блоком ниже). 0x180
 * сохранён как канонический размер unit-снимка (тот же блок, что cap/eee0
 * ai_words/occ). */
#define CAP_BDD0_UNIT_SIZE  0x180
#define CAP_BDD0_AI_SIZE    0x80   /* BattleAI ВСТРОЕН в unit: unit+0x138..+0x1b7 */
#define CAP_BDD0_AI_OFF     0x138  /* смещение встроенного BattleAI от unit */
/* Лента живёт в BattleAI+0x3c; поля по листингу 45af50/45b6a0/45af10:
 * +0x20 begin, +0x24 end, +0x28 capacity, +0x40 dirty (от BattleAI) ⇒
 * unit+0x194/0x198/0x19c/0x1b4 — вне unit-блока 0x180, поэтому читаются из
 * снимка встроенного BattleAI. Элемент 0x18 Б ⇒ count = (end−begin)/0x18. */
#define CAP_BDD0_BAND_BEGIN 0x5c   /* BattleAI+0x20 */
#define CAP_BDD0_BAND_END   0x60   /* BattleAI+0x24 */
#define CAP_BDD0_BAND_CAP   0x64   /* BattleAI+0x28 */
#define CAP_BDD0_BAND_DIRTY 0x7c   /* BattleAI+0x40 */
#define CAP_BDD0_BAND_ELEM  0x18
#define CAP_BDD0_BAND_MAX   64     /* потолок материализуемых элементов ленты */
#define CAP_BDD0_GS_SIZE    0x20   /* объект FUN_00442570, читаемый телом */
#define CAP_B64_MAX         (CAP_BDD0_UNIT_SIZE * 4 / 3 + 8)
#define CAP_B64_BAND_MAX    (CAP_BDD0_BAND_MAX * CAP_BDD0_BAND_ELEM * 4 / 3 + 8)
/* v16 (аддитивно): потолок узлов на одну RB-карту PathFinder за снимок
 * (cap_read_map). 512*(4+0x18+0x58+4) ≈ 60 КБ статики на обе карты. */
#define CAP_BDD0_MAP_MAX    512

/* v3: значение узла map2 (PathState*). w[] — сырые dword'ы PathState; ptr[] —
 * 11 указателей LDPos* (pos, parent, nbr[9]); ptr-поля в w[] харнесс подменяет
 * по identity. Занятый ключ без записи значения остаётся NULL. */
typedef struct {
    int key;
    int has_value;
    int value_addr;                 /* PathState* (только identity) */
    int pos_addr;                   /* LDPos* (identity map1) */
    int w[CAP_PS_WORDS];
    int ptr[11];
    int pos[2];                     /* координаты LDPos значения */
    int parent[2];
    int nbr[9][2];
} cap_occ_node;

typedef struct {
    int x, y;
} cap_pos;

typedef struct {
    cap_pos pos;
    int filter, g, d2, h10k, cost;
    cap_pos parent;
    cap_pos nbr[9];
    int occ_key;
    int pos_addr;               /* LDPos* — ключ map1 (dd00: ключ = указатель) */
} cap_node;

static int   g_enabled = 0;
static FILE *g_file = NULL;
static int   g_max_calls = CAP_MAX_CALLS;
static int   g_snap = 0;           /* v12: LDHOOK_BATTLE_CAPTURE_SNAP=1 */
static int   g_e0 = 1;             /* слой e0a0 (cap/res/grid/eee0_cap+res/листья): LDHOOK_BATTLE_CAPTURE_E0=0 выключает */
static char  g_wbuf[1 << 20];      /* буферизованная запись: 1 МиБ, flush на конце тика */
static int   g_seq = 0;            /* monotonic счётчик захваченных вызовов e0a0 */
static int   g_eee0_seq = 0;       /* v9: независимый счётчик вызовов 45eee0 */
static int   g_bdd0_seq = 0;       /* v10: независимый счётчик вызовов 44bdd0 */
static int   g_closed = 0;

/* Буферы, выделяются один раз в init (без повторных больших malloc в хуке). */
static cap_node *g_nodes = NULL;
static int      *g_occ = NULL;
static cap_occ_node *g_occ_nodes = NULL;
static unsigned int  g_ai_words[CAP_AI_WORDS];
static cap_pos  *g_iter = NULL;
static int       g_iter_cap = CAP_MAX_ITER;
static int      *g_path = NULL;
static int      *g_pptr = NULL;      /* указатель, возвращённый cb40 (identity) */
static int      *g_heur = NULL;
static int      *g_tout = NULL;
static int      *g_tms = NULL;          /* v5: timeGetTime на каждом c5a0 */
static int      *g_gate = NULL;         /* v6: CAP_GATE_WORDS на каждый cb40-leaf */
static int      *g_cap_a1 = NULL, *g_cap_a2 = NULL;  /* v6: стек-арги cb40 на входе */
/* v6: слоты финального гейта cb40, прочитанные в ret-трамплине из его
 * (already unwound, not yet overwritten) frame: L[0x10]=мин, L[0x2c],
 * L[0x14], L[0x18]=макс-пара. Вынесены в globals, чтобы C++-helper не портил
 * регистры/ESP трамплина. */
static unsigned int g_gate_l10 = 0, g_gate_l2c = 0;
static unsigned int g_gate_l14 = 0, g_gate_l18 = 0;
static unsigned int g_cb40_ret = 0;     /* EAX на входе в ret-трамплин (результат cb40) */
/* v7: world[0x120] на входе текущего cb40 (биты float) и флаг снятия; снятие —
 * по цепочке native 0x45c42b..0x45c430: self→[0] (game_ctx) → +4 (world) → +0x120. */
static unsigned int g_cb40_scale = 0;
static int g_cb40_scale_ok = 0;
static int *g_leaf_scale = NULL;        /* v7: биты на каждый cb40-leaf */
static int *g_leaf_scale_ok = NULL;     /* v7: 1 — значение снято */
/* v8: состояние цепочки c3a0 на ВХОДЕ каждого cb40-вызова: [count, entries…].
 * Entry = [idx,min(+14),max(+18),key,next(+24),chain(+28),tree_status].
 * Снимается в трамплине cb40, поэтому видит мутации ячеек внутри e0a0,
 * которых нет в статическом grid/cell_delta уровня e0a0. */
static int *g_chain_leaf = NULL;
static int  g_chain_overflow = 0;

/* v5: статическая сетка — один снимок на бой. Массивы выделяются в init. */
static int *g_gkey = NULL, *g_gstatus = NULL, *g_gmin = NULL;
static int *g_gmax = NULL, *g_gnext = NULL, *g_gchain = NULL;
static int *g_grows = NULL;
static int *g_gw0 = NULL, *g_gw1 = NULL, *g_gw2 = NULL;
static int *g_gw3 = NULL, *g_gw4 = NULL;   /* ячейка +0x00..+0x10 (LDPos-вид) */
static int *g_tree_prev = NULL;            /* v5: предыдущий снимок world-дерева */
static int *g_td_idx = NULL, *g_td_val = NULL;
static int  g_grid_n = 0, g_tree_overflow = 0, g_cell_overflow = 0;
static int *g_prev_fields = NULL;          /* n*11: w0..w4,min,max,key,next,chain */
static int *g_cd_idx = NULL, *g_cd_val = NULL;  /* дельты ячеек */
static int *g_prev_rows = NULL;                /* снимок rows[x][y] */
static int  g_grid_written = 0;
static volatile unsigned long g_timer_ms = 0;   /* из трамплина c5a0 */

static void     *g_this = NULL;
/* v12: регистрация sink хуков call-сайтов (определение ниже). */
static void cap_leaf_sink_register(void);static void     *g_out_code = NULL;
static int       g_filter = 0;
static cap_pos   g_cmp = { -1, -1 };
static int       g_cmp_addr = 0;     /* LDPos* цели — identity-гейт e0a0 */
static unsigned int g_timeout_bits = 0;
static int       g_tick = -1, g_unit = -1;
static int       g_iter_n = 0, g_node_n = 0, g_occ_n = 0;
static int       g_path_n = 0, g_heur_n = 0, g_tout_n = 0;
static int       g_leaf_overflow = 0;
static int       g_map_trap = 0;

/* v9: состояние per-call захвата FUN_0045eee0 (s6_unit_ai_tick). */
static int       g_in_eee0 = 0;
static int       g_cur_eee0_seq = 0;
/* v12: 0x44cbd0 (`call 45eee0`) is inside the H3 hook, so the bdd0_native
 * record is synthesized here with the same per-seq order as a site hook. */
static int       g_eee0_native_valid = 0;
static int       g_eee0_native_order = -1;
static int       g_eee0_native_seq = 0;
static int       g_eee0_native_ecx = 0;
static int       g_eee0_native_arg = 0;
static int       g_eee0_b9_valid = 0;
static int       g_eee0_b9_null = 0;
static int       g_eee0_b9_w[CAP_WORDS] = {0, 0, 0, 0, 0};
static int       g_eee0_b9_addr = 0;
static unsigned int g_eee0_b9_scale = 0;
static int       g_eee0_b9_scale_ok = 0;
static int       g_eee0_b9_gate[CAP_GATE_WORDS] = {0, 0, 0, 0};
static int       g_eee0_b9_chain[1 + CAP_CHAIN_MAX * CAP_CHAIN_WORDS];

/* v10: состояние per-call захвата FUN_0044bdd0 (диспетчер боевого AI).
 * 44bdd0 ре-входим (4-й вызывающий 0x44d596 + vtable-вызовы), поэтому пара
 * cap/res живёт в LIFO-стеке: H1 кладёт {seq,self}, H2 снимает вершину и
 * пишет res с ЕЁ seq/self. Переполнение — вызов не захватывается вовсе
 * ( пары cap/res нет), чтобы не портить связывание. */
#define BDD0_STACK_MAX 32
typedef struct {
    int  seq;
    void *self;
    int  call_order;               /* v12: общий 0-based счётчик bdd0_leaf +
                                    * bdd0_native этого вызова (порядок вызова) */
} bdd0_frame;

static bdd0_frame g_bdd0_stack[BDD0_STACK_MAX];
static int    g_bdd0_sp = 0;       /* 0 — стек пуст */

/* v12: буферы снапшота (один набор: запись на входе синхронная, без вложенности
 * между блоками) — без malloc в горячем пути. */
static unsigned char g_snap_frame[CAP_BDD0_FRAME_SIZE];
static unsigned char g_snap_unit[CAP_BDD0_UNIT_SIZE];
static unsigned char g_snap_ai[CAP_BDD0_AI_SIZE];
/* v12 (аддитивно): аддитивные блоки, без которых офлайн-харнесс
 * (tools/probes/bdd0_harness.cpp) не может материализовать разыменования
 * снимка — см. tools/probes/bdd0/arena.cpp, таблица k_fixups:
 *   band_b64 — элементы ленты (band_count*0x18, до CAP_BDD0_BAND_MAX штук);
 *   gs_b64   — 0x20 Б объекта FUN_00442570 (accessor вызывается один раз на
 *              H1 под __try; тело читает его 4+ раз за тик);
 *   e0/e0_44/gctx0 — сырые указатели цепочки unit+0xe0 → [+0x44] → [+0x238]
 *              и первый dword game_ctx (`*(u32)*(unit+0x138)`). */
static unsigned char g_snap_band[CAP_BDD0_BAND_MAX * CAP_BDD0_BAND_ELEM];
static unsigned char g_snap_gs[CAP_BDD0_GS_SIZE];
static char          g_b64[CAP_B64_MAX];
static char          g_b64_big[CAP_B64_BAND_MAX];
/* v16 (аддитивно): геометрия ленты (band_pos) — параллельные массивы на
 * CAP_BDD0_BAND_MAX элементов; заполняет cap_read_band_pos, пишет
 * cap_write_band_pos. sa/sl/sm — сырые биты float-тройки уровня. */
static int          g_band_pos_ok[CAP_BDD0_BAND_MAX];
static int          g_band_pos_x[CAP_BDD0_BAND_MAX];
static int          g_band_pos_y[CAP_BDD0_BAND_MAX];
static unsigned int g_band_pos_sa[CAP_BDD0_BAND_MAX];
static unsigned int g_band_pos_sl[CAP_BDD0_BAND_MAX];
static unsigned int g_band_pos_sm[CAP_BDD0_BAND_MAX];
/* v16 (аддитивно): содержимое одной RB-карты PathFinder (map1 head +0x6c,
 * map2 head +0x78). Заполняется cap_read_map, пишется cap_write_map; массивы
 * переиспользуются между картами и между pre/post (fill → write → refill), без
 * malloc в горячем пути. Узел committed только после чтения 0x18 узла и 0x58
 * PathState — сбой оставляет последний полный узел, без half-init записи. */
static unsigned int  g_map_addr[CAP_BDD0_MAP_MAX];
static unsigned char g_map_node[CAP_BDD0_MAP_MAX][0x18];
static unsigned char g_map_ps[CAP_BDD0_MAP_MAX][0x58];
static int           g_map_ps_ok[CAP_BDD0_MAP_MAX];
static int           g_map_cnt = 0;
static int           g_map_trunc = 0;
static int           g_map_fault = 0;

/* Адреса оригинальных листьев (устанавливаются из install_capture_sites). */
static void *g_fn_cb40 = NULL;
static void *g_fn_heur = NULL;
static void *g_fn_timeout = NULL;

void battle_capture_init(void)
{
    const char *path = getenv(BATTLE_CAPTURE_ENV);
    const char *mx = getenv(BATTLE_CAPTURE_MAX_ENV);
    char *end = NULL;
    long v;

    if (path == NULL || *path == '\0') {
        return;
    }
    if (mx != NULL && *mx != '\0') {
        v = strtol(mx, &end, 10);
        if (end != mx && v >= 1 && v <= 1000000) {
            g_max_calls = (int)v;
        }
    }
    g_file = fopen(path, "wb");
    if (g_file == NULL) {
        ld_info("battle_capture: fopen(%s) FAILED — capture OFF", path);
        return;
    }
    /* Буферизация 1 МиБ: записи по 1-3 МБ без буфера (WriteFile на каждый
     * fprintf) тормозили игру; flush — на конце тика (bdd0/eee0 exit),
     * поэтому краш теряет не больше последнего тика. */
    setvbuf(g_file, g_wbuf, _IOFBF, sizeof(g_wbuf));
    g_nodes = (cap_node *)malloc((size_t)CAP_MAX_NODES * sizeof(cap_node));
    g_occ = (int *)malloc((size_t)CAP_MAX_NODES * sizeof(int));
    g_occ_nodes = (cap_occ_node *)malloc((size_t)CAP_MAX_NODES *
                                        sizeof(cap_occ_node));
    g_iter = (cap_pos *)malloc((size_t)g_iter_cap * sizeof(cap_pos));
    g_path = (int *)malloc((size_t)CAP_MAX_LEAF * CAP_WORDS * sizeof(int));
    g_pptr = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_heur = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_tout = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_tms = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_gate = (int *)malloc((size_t)CAP_MAX_LEAF * CAP_GATE_WORDS * sizeof(int));
    g_leaf_scale = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_leaf_scale_ok = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_chain_leaf = (int *)malloc((size_t)CAP_MAX_LEAF *
                                 (1 + CAP_CHAIN_MAX * CAP_CHAIN_WORDS) * sizeof(int));
    g_gkey = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gstatus = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gmin = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gmax = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gnext = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gchain = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_grows = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gw0 = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gw1 = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gw2 = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gw3 = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_gw4 = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_tree_prev = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    g_td_idx = (int *)malloc((size_t)CAP_MAX_TREE_DELTA * sizeof(int));
    g_td_val = (int *)malloc((size_t)CAP_MAX_TREE_DELTA * sizeof(int));
    g_prev_fields = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * 11 * sizeof(int));
    g_cd_idx = (int *)malloc((size_t)CAP_MAX_CELL_DELTA * sizeof(int));
    g_cd_val = (int *)malloc((size_t)CAP_MAX_CELL_DELTA * 11 * sizeof(int));
    g_prev_rows = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * sizeof(int));
    if (!g_nodes || !g_occ || !g_occ_nodes || !g_iter || !g_path || !g_pptr ||
        !g_heur || !g_tout || !g_tms || !g_gate || !g_leaf_scale ||
        !g_leaf_scale_ok || !g_chain_leaf || !g_gkey || !g_gstatus || !g_gmin ||
        !g_gmax || !g_gnext || !g_gchain || !g_grows ||
        !g_gw0 || !g_gw1 || !g_gw2 || !g_gw3 || !g_gw4 ||
        !g_tree_prev || !g_td_idx || !g_td_val ||
        !g_prev_fields || !g_cd_idx || !g_cd_val || !g_prev_rows) {
        free(g_nodes); free(g_occ); free(g_occ_nodes); free(g_iter);
        free(g_path); free(g_pptr); free(g_heur); free(g_tout); free(g_tms);
        free(g_gate); free(g_leaf_scale); free(g_leaf_scale_ok); free(g_chain_leaf);
        free(g_gkey); free(g_gstatus); free(g_gmin); free(g_gmax);
        free(g_gnext); free(g_gchain); free(g_grows);
        free(g_gw0); free(g_gw1); free(g_gw2); free(g_gw3); free(g_gw4);
        free(g_tree_prev); free(g_td_idx); free(g_td_val);
        free(g_prev_fields); free(g_cd_idx); free(g_cd_val);
        free(g_prev_rows);
        fclose(g_file); g_file = NULL;
        ld_info("battle_capture: out of memory — capture OFF");
        return;
    }
    g_enabled = 1;
    {
        const char *sn = getenv(BATTLE_CAPTURE_SNAP_ENV);
        if (sn != NULL && *sn != '\0' && atoi(sn) != 0) {
            g_snap = 1;
        }
        sn = getenv(BATTLE_CAPTURE_E0_ENV);
        if (sn != NULL && *sn != '\0' && atoi(sn) == 0) {
            g_e0 = 0;   /* слой e0a0 не пишется (cap/res/grid/eee0_*) */
        }
    }
    /* Потоки листьев и нативов тела 44bdd0 (bdd0_leaf/bdd0_native) — это слой
     * снапшота, не e0a0: регистрируются всегда, иначе с E0=0 харнессу нечего
     * сверять. Слой e0a0 глушится только в своих точках записи (g_e0). */
    cap_leaf_sink_register();
    ld_info("battle_capture: %s=%s (max=%d snap=%d e0=%d)", BATTLE_CAPTURE_ENV,
            path, g_max_calls, g_snap, g_e0);
}

int battle_capture_enabled(void)
{
    return g_enabled;
}

/* ------------------------- чтение игровых структур ---------------------- */

static int cap_read_pos(void *p, cap_pos *out)
{
    __try {
        if (p == NULL) {
            out->x = -1; out->y = -1;
            return 0;
        }
        out->x = *(const int *)((const unsigned char *)p + 0x0c);
        out->y = *(const int *)((const unsigned char *)p + 0x10);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->x = -1; out->y = -1;
        return 0;
    }
}

static int cap_occ_key(const cap_pos *p)
{
    return (int)(((unsigned short)p->y << 16) | (unsigned short)p->x);
}

/* Candidate/PathState: координаты не в самом объекте, а в его +0x00 → LDPos
 * (`pos`), координаты LDPos +0x0c/+0x10 (structs.h). */
static int cap_read_cand_pos(void *cand, cap_pos *out)
{
    void *pos = NULL;

    __try {
        pos = (cand != NULL) ? *(void **)cand : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pos = NULL;
    }
    return cap_read_pos(pos, out);
}

/* PathState* → его LDPos* (родитель/соседи хранят PathState, координаты — в
 * PathState.pos, structs.h: +0x04 parent, +0x34..0x54 nbr[9]). */
static int cap_read_ps_pos(PathState *ps, cap_pos *out)
{
    LDPos *p = NULL;

    __try {
        p = (ps != NULL) ? ps->pos : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p = NULL;
    }
    return cap_read_pos(p, out);
}

static int cap_read_node(TreeNode *tn, cap_node *out)
{
    PathState *n;
    int i;

    memset(out, 0, sizeof(*out));
    out->parent.x = -1; out->parent.y = -1;
    for (i = 0; i < 9; i++) {
        out->nbr[i].x = -1;
        out->nbr[i].y = -1;
    }
    __try {
        n = (PathState *)tn->value;
        if (n == NULL) {
            return 0;
        }
        cap_read_pos(n->pos, &out->pos);
        out->filter = n->filter;
        out->g = n->g_cost;
        out->d2 = n->dist2;
        out->h10k = n->h10000;
        out->cost = n->metric;
        cap_read_ps_pos(n->parent, &out->parent);
        for (i = 0; i < 9; i++) {
            cap_read_ps_pos(n->nbr[i], &out->nbr[i]);
        }
        out->occ_key = cap_occ_key(&out->pos);
        out->pos_addr = (int)(size_t)(void *)n->pos;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->pos.x = -1; out->pos.y = -1;
        return 0;
    }
}

static void cap_read_map1(BattleAI *self)
{
    TreeNode *head, *cur;

    g_node_n = 0;
    g_map_trap = 0;
    __try {
        head = self->cand_head;
        if (head == NULL) {
            return;
        }
        cur = head->left;
        while (cur != NULL && cur != head && g_node_n < CAP_MAX_NODES) {
            if (cap_read_node(cur, &g_nodes[g_node_n])) {
                g_node_n++;
            }
            cur = s6_tree_next(cur);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_map_trap = 1;
    }
}

/* v4: скалярный блок BattleAI +0x00..+0x7f ПОСЛЕ вызова — симметрично
 * cap.ai_words (тот же блок, тот же снимок), чтобы харнесс сравнивал after-state. */
static void cap_read_ai_words(BattleAI *self)
{
    memset(g_ai_words, 0, sizeof(g_ai_words));
    __try {
        if (self != NULL) {
            memcpy(g_ai_words, self, sizeof(g_ai_words));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* v13 (аддитивно): голова/конец ленты BattleAI+0x3c+0x20/+0x24 — состояние,
 * которое 45eee0 перестраивает внутри тика и которое до v13 в захвате не было
 * записано (offliner восстанавливал длину из следующей записи 45af50).
 * Чтение под __try; сбой → 0/0 и ok=0. */
static void cap_read_band(BattleAI *self, unsigned int *begin,
                          unsigned int *end, int *ok)
{
    *begin = 0;
    *end = 0;
    *ok = 0;
    __try {
        if (self != NULL) {
            /* self = unit (ECX вызова 44bdd0): лента — BattleAI+0x3c, а сам
             * BattleAI встроен со смещением 0x138 → unit+0x194/+0x198. */
            const unsigned char *ai =
                (const unsigned char *)self + CAP_BDD0_AI_OFF;
            *begin = *(const unsigned int *)(ai + CAP_BDD0_BAND_BEGIN);
            *end = *(const unsigned int *)(ai + CAP_BDD0_BAND_END);
            *ok = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *begin = 0;
        *end = 0;
        *ok = 0;
    }
}

static void cap_read_occ(BattleAI *self)
{
    TreeNode *head, *cur;
    cap_pos tmp;
    int k;

    g_occ_n = 0;
    __try {
        head = self->occ_head;
        cur = (head != NULL) ? head->left : NULL;
        while (cur != NULL && cur != head && g_occ_n < CAP_MAX_NODES) {
            int i = g_occ_n;
            unsigned char *v;
            PathState *ps;

            g_occ[i] = cur->key;
            memset(&g_occ_nodes[i], 0, sizeof(g_occ_nodes[i]));
            g_occ_nodes[i].key = cur->key;
            v = (unsigned char *)cur->value;
            g_occ_nodes[i].value_addr = (int)(size_t)v;
            if (v != NULL) {
                ps = (PathState *)v;
                /* Сырые dword'ы PathState + identity указателей (ptr[]), чтобы
                 * харнесс мог подменить их на объекты своей реплики. */
                memcpy(g_occ_nodes[i].w, v, CAP_PS_WORDS * sizeof(int));
                g_occ_nodes[i].ptr[0] = (int)(size_t)ps->pos;
                g_occ_nodes[i].ptr[1] = (int)(size_t)ps->parent;
                for (k = 0; k < 9; k++) {
                    g_occ_nodes[i].ptr[2 + k] = (int)(size_t)ps->nbr[k];
                }
                cap_read_pos((void *)(size_t)g_occ_nodes[i].ptr[0], &tmp);
                g_occ_nodes[i].pos[0] = tmp.x;
                g_occ_nodes[i].pos[1] = tmp.y;
                cap_read_pos((void *)(size_t)g_occ_nodes[i].ptr[1], &tmp);
                g_occ_nodes[i].parent[0] = tmp.x;
                g_occ_nodes[i].parent[1] = tmp.y;
                for (k = 0; k < 9; k++) {
                    cap_read_pos((void *)(size_t)g_occ_nodes[i].ptr[2 + k],
                                 &tmp);
                    g_occ_nodes[i].nbr[k][0] = tmp.x;
                    g_occ_nodes[i].nbr[k][1] = tmp.y;
                }
                g_occ_nodes[i].pos_addr = g_occ_nodes[i].ptr[0];
                g_occ_nodes[i].has_value = 1;
            }
            g_occ_n++;
            cur = s6_tree_next(cur);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_map_trap = 1;
    }
}

static void cap_write_node(FILE *f, const cap_node *n)
{
    int i;

    fprintf(f, "{\"pos\":[%d,%d],\"filter\":%d,\"g\":%d,\"d2\":%d,"
             "\"h10k\":%d,\"cost\":%d,\"parent\":[%d,%d],\"nbr\":[",
            n->pos.x, n->pos.y, n->filter, n->g, n->d2, n->h10k, n->cost,
            n->parent.x, n->parent.y);
    for (i = 0; i < 9; i++) {
        fprintf(f, "%s[%d,%d]", i ? "," : "", n->nbr[i].x, n->nbr[i].y);
    }
    fprintf(f, "],\"occ_key\":%d,\"pos_addr\":%d}", n->occ_key, n->pos_addr);
}

static void cap_write_map1(FILE *f)
{
    int i;
    fputc('[', f);
    for (i = 0; i < g_node_n; i++) {
        fprintf(f, "%s", i ? "," : "");
        cap_write_node(f, &g_nodes[i]);
    }
    fputc(']', f);
}

static void cap_write_ints(FILE *f, const int *arr, int n)
{
    int i;
    fputc('[', f);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%d", i ? "," : "", arr[i]);
    }
    fputc(']', f);
}

/* v16: массив u32 (сырые биты float) — %u, без знака. */
static void cap_write_uints(FILE *f, const unsigned int *arr, int n)
{
    int i;
    fputc('[', f);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%u", i ? "," : "", arr[i]);
    }
    fputc(']', f);
}

/* v6: входы финального гейта для каждого cb40-leaf — [out,thr,min,max] (биты). */
static void cap_write_gate(FILE *f)
{
    int i, k, n = g_path_n / CAP_WORDS;
    fputc('[', f);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s[", i ? "," : "");
        for (k = 0; k < CAP_GATE_WORDS; k++) {
            fprintf(f, "%s%u", k ? "," : "",
                    (unsigned int)g_gate[i * CAP_GATE_WORDS + k]);
        }
        fputc(']', f);
    }
    fputc(']', f);
}

/* v8: снимок цепочки, которую обойдёт ПЕРВЫЙ c3a0 этого cb40 (FUN_0045c3a0):
 * node = spec->pos, спуск FUN_00456a70 по +0x28, затем переходы +0x24.
 * Поля ровно те, что читает 0x45c3e0..0x45c425: min/max +0x14/+0x18,
 * ключ [[node+0x20]], next +0x24, link +0x28 и статус tree_lookup. */
static void cap_snapshot_chain(void *self)
{
    unsigned char *cells = NULL, *world = NULL;
    int n = 0, cnt = 0;
    void *node;
    size_t li;
    int *out;

    if (g_chain_leaf == NULL) {
        return;
    }
    __try {
        unsigned char *ctx = (unsigned char *)self;
        if (ctx != NULL) {
            GridLevel *lvl;
            world = *(unsigned char **)(ctx + 4);
            lvl = *(GridLevel **)(ctx + 8);
            while (lvl != NULL && lvl->next_level != NULL) lvl = lvl->next_level;
            if (lvl != NULL) {
                cells = (unsigned char *)lvl->cells;
                n = lvl->size_x * lvl->size_y;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (cells == NULL || world == NULL || n <= 0 || n > CAP_MAX_GRID_CELLS) {
        return;
    }
    li = (size_t)(g_path_n / CAP_WORDS);   /* индекс листа pc40 до записи на выходе */
    if (li >= CAP_MAX_LEAF) {
        return;
    }
    out = &g_chain_leaf[li * (1 + CAP_CHAIN_MAX * CAP_CHAIN_WORDS)];
    out[0] = 0;
    node = (g_cap_a1 != NULL) ? *(void **)g_cap_a1 : NULL;   /* spec->pos */
    __try {
        if (node != NULL) {
            void *q = *(void **)((unsigned char *)node + 0x28);
            while (q != NULL) {
                node = q;
                q = *(void **)((unsigned char *)node + 0x28);
            }
        }
        while (node != NULL && cnt < CAP_CHAIN_MAX) {
            unsigned char *c = (unsigned char *)node;
            void *kr = *(void **)(c + 0x20);
            void *nx = *(void **)(c + 0x24);
            void *ch = *(void **)(c + 0x28);
            int *e = &out[1 + cnt * CAP_CHAIN_WORDS];
            int key = (kr != NULL) ? *(int *)kr : 0;
            e[0] = (c >= cells && c < cells + (size_t)n * S6_GRID_CELL_SIZE)
                   ? (int)(((unsigned char *)c - cells) / S6_GRID_CELL_SIZE) : -1;
            e[1] = *(int *)(c + 0x14);
            e[2] = *(int *)(c + 0x18);
            e[3] = key;
            e[4] = (nx >= cells && nx < cells + (size_t)n * S6_GRID_CELL_SIZE)
                   ? (int)(((unsigned char *)nx - cells) / S6_GRID_CELL_SIZE) : -1;
            e[5] = (ch >= cells && ch < cells + (size_t)n * S6_GRID_CELL_SIZE)
                   ? (int)(((unsigned char *)ch - cells) / S6_GRID_CELL_SIZE) : -1;
            e[6] = native::tree_lookup(world, (unsigned int)key);
            cnt++;
            node = nx;                       /* 0x45c420: node = [node+0x24] */
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    out[0] = cnt;
    if (cnt == CAP_CHAIN_MAX && node != NULL) {
        g_chain_overflow = 1;
    }
}

/* v7: масштаб world[0x120] на входе каждого cb40-вызова: биты float и флаг снятия. */
static void cap_write_scale(FILE *f, int n)
{
    int i;
    fputs(",\"scale\":", f);
    fputc('[', f);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%u", i ? "," : "", (unsigned int)g_leaf_scale[i]);
    }
    fputc(']', f);
    fputs(",\"scale_ok\":", f);
    fputc('[', f);
    for (i = 0; i < n; i++) {
        fprintf(f, "%s%d", i ? "," : "", g_leaf_scale_ok[i]);
    }
    fputc(']', f);
}

/* v8: записать снимки цепочек по cb40-вызовам (параллельно leaf.path). */
static void cap_write_chain(FILE *f, int n)
{
    int i, j, k;
    fputs(",\"chain\":[", f);
    for (i = 0; i < n; i++) {
        const int *p = &g_chain_leaf[(size_t)i * (1 + CAP_CHAIN_MAX * CAP_CHAIN_WORDS)];
        int cnt = p[0];
        fputs(i ? "],[" : "[", f);
        fprintf(f, "%d", cnt);
        for (j = 0; j < cnt; j++) {
            const int *e = &p[1 + j * CAP_CHAIN_WORDS];
            for (k = 0; k < CAP_CHAIN_WORDS; k++) {
                fprintf(f, ",%d", e[k]);
            }
        }
    }
    fputs(n ? "]]" : "]", f);
}

/* v3: полная запись значения узла map2 (NULL или PathState) + скалярный блок
 * BattleAI. v1/v2-файлы этих полей не имеют; харнесс читает их по `v`. */
static void cap_write_occ_nodes(FILE *f)
{
    int i, k;

    fputc('[', f);
    for (i = 0; i < g_occ_n; i++) {
        const cap_occ_node *o = &g_occ_nodes[i];
        fprintf(f, "%s{\"key\":%d,\"value\":", i ? "," : "", o->key);
        if (!o->has_value) {
            fputs("null", f);
        } else {
            fprintf(f, "{\"value_addr\":%d,\"pos_addr\":%d,\"pos\":[%d,%d],"
                    "\"w\":",
                    o->value_addr, o->pos_addr, o->pos[0], o->pos[1]);
            cap_write_ints(f, o->w, CAP_PS_WORDS);
            fputs(",\"ptr\":", f);
            cap_write_ints(f, o->ptr, 11);
            fprintf(f, ",\"parent\":[%d,%d],\"nbr\":[",
                    o->parent[0], o->parent[1]);
            for (k = 0; k < 9; k++) {
                fprintf(f, "%s[%d,%d]", k ? "," : "",
                        o->nbr[k][0], o->nbr[k][1]);
            }
            fputs("]}", f);
        }
        fputc('}', f);
    }
    fputc(']', f);
}

static void cap_write_ai_words(FILE *f)
{
    cap_write_ints(f, (const int *)g_ai_words, CAP_AI_WORDS);
}

static void cap_write_path(FILE *f)
{
    int i, k, null;
    fputc('[', f);
    for (i = 0; i < g_path_n; i += CAP_WORDS) {
        null = (g_path[i] == 0x7fffffff);
        fprintf(f, "%s", i ? "," : "");
        if (null) {
            fputs("null", f);
        } else {
            fputc('[', f);
            for (k = 0; k < CAP_WORDS; k++) {
                fprintf(f, "%s%d", k ? "," : "", g_path[i + k]);
            }
            fputc(']', f);
        }
    }
    fputc(']', f);
}

static void cap_write_iter(FILE *f)
{
    int i;
    fputc('[', f);
    for (i = 0; i < g_iter_n; i++) {
        if (g_iter[i].x == -1 && g_iter[i].y == -1) {
            fprintf(f, "%snull", i ? "," : "");
        } else {
            fprintf(f, "%s[%d,%d]", i ? "," : "",
                    g_iter[i].x, g_iter[i].y);
        }
    }
    fputc(']', f);
}

static void cap_write_grid(FILE *f, BattleAI *self)
{
    void *ctx;
    GridLevel *lvl;
    int level = 0;

    fputc('[', f);
    __try {
        ctx = self->game_ctx;
        lvl = (ctx != NULL)
              ? *(GridLevel **)((const unsigned char *)ctx + 8) : NULL;
        while (lvl != NULL && level < 16) {
            long long cells = (long long)lvl->size_x * (long long)lvl->size_y;
            if (cells < 0 || cells > CAP_MAX_NODES) {
                cells = 0;
            }
            fprintf(f, "%s{\"level\":%d,\"maxrow\":%d,\"maxcol\":%d,"
                     "\"cells\":%lld}",
                    level ? "," : "", level, lvl->size_x, lvl->size_y, cells);
            lvl = lvl->next_level;
            level++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    fputc(']', f);
}

/* v5: внешние (не-картовые) входы, которые читает порт path/эвристики.
 * Значения снимаются один раз — мир и vtable-объекты статичны в бою. */
typedef struct {
    unsigned int w110;        /* world[0x110] — гейт c690 */
    unsigned int vt;          /* *(int*)*(void**)self — биты 2/3 гейта c690 */
    unsigned int a1;          /* [obj1+0x244] — множитель c5e0 */
    unsigned int obj4;        /* *(float*)(obj4+0x34) — float-множитель c5e0 */
    unsigned int wscale;      /* world[0x120] — константа c3a0/cb40 */
} cap_grid_deps;

static void cap_read_grid_deps(BattleAI *self, cap_grid_deps *d)
{
    memset(d, 0, sizeof(*d));
    __try {
        unsigned char *w0 = *(unsigned char **)self;
        unsigned char *world = *(unsigned char **)(w0 + 4);
        unsigned char *o1, *o2, *o3, *o4;

        d->w110 = *(unsigned char *)(world + 0x110);
        d->vt = *(unsigned int *)w0;
        d->wscale = *(unsigned int *)(world + 0x120);
        o1 = (unsigned char *)(size_t)native::vcall0(world, 0x40);
        d->a1 = *(unsigned int *)(o1 + 0x244);
        o2 = (unsigned char *)(size_t)native::vcall0(world, 0x40);
        o3 = (unsigned char *)(size_t)(*(int *)(o2 + 0x228));
        o4 = (unsigned char *)(size_t)native::vcall0(o3, 0x48);
        d->obj4 = *(unsigned int *)(o4 + 0x34);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* v5: один снимок сетки. Порт читает (grid.cpp/path.cpp):
 *   +0x1c next_level (спуск в FUN_00456ed0), +0x2c/+0x30 size_x/size_y,
 *   +0x34 cells (шаг 0x34), +0x38 rows[x][y] = индекс,
 *   +0x14/+0x18 min/max метрики ячейки (float-биты),
 *   +0x20 key_rec->key, +0x24 next, +0x28 chain_next, +0x1c level;
 *   scale +0x20/+0x24/+0x28 и rec_x/rec_y +0x0c/+0x10 (FUN_00456b20).
 * Статус tree_lookup(world, key) снимается для каждого ключа ячейки — это
 * содержимое world+0x104, которое порт читает через FUN_00449160. */
/* 11 полей ячейки, которые читает порт (grid.cpp/path.cpp/target.cpp). */
static void cap_read_cell_fields(unsigned char *cells, int n, int *out)
{
    int i, k;
    __try {
        for (i = 0; i < n; i++) {
            unsigned char *c = cells + i * S6_GRID_CELL_SIZE;
            void *kr = *(void **)(c + 0x20);
            void *nx = *(void **)(c + 0x24);
            void *ch = *(void **)(c + 0x28);
            int *o = &out[i * 11];
            for (k = 0; k < 5; k++) o[k] = *(int *)(c + k * 4);
            o[5] = *(int *)(c + 0x14);
            o[6] = *(int *)(c + 0x18);
            o[7] = (kr != NULL) ? *(int *)kr : 0;
            o[8] = (nx >= cells && nx < cells + (size_t)n * S6_GRID_CELL_SIZE)
                   ? (int)(((unsigned char *)nx - cells) / S6_GRID_CELL_SIZE) : -1;
            o[9] = (ch >= cells && ch < cells + (size_t)n * S6_GRID_CELL_SIZE)
                   ? (int)(((unsigned char *)ch - cells) / S6_GRID_CELL_SIZE) : -1;
            o[10] = *(int *)(c + 0x1c);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* v16: общие данные листового уровня сетки. Заполняются под __try без запи —
 * один и тот же снимок читают и писатель записи, и предикат bdd0-пути. */
typedef struct {
    int ok;                     /* 0 — чтение упало (любая ошибка) */
    int nlev;                   /* число уровней (1..16) */
    int leaf;                   /* индекс листового уровня */
    int sx, sy;                 /* размеры листа */
    unsigned char *cells;       /* массив ячеек листа */
    int **rows;                 /* rows[x][y] → индекс ячейки */
    void *world;                /* game_ctx[4] */
    GridLevel *levels[16];      /* цепочка уровней сверху вниз */
} cap_grid_view;

/* Снимает листовой уровень сетки BattleAI. Только чтение, всё под __try;
 * при любой ошибке ok=0 — ровно как в error-ветке писателя. */
static void cap_grid_read(BattleAI *self, cap_grid_view *v)
{
    int nlev = 0;

    v->ok = 1;
    v->nlev = 0;
    v->leaf = 0;
    v->sx = 0;
    v->sy = 0;
    v->cells = NULL;
    v->rows = NULL;
    v->world = NULL;
    __try {
        if (self != NULL && self->game_ctx != NULL) {
            v->world = *(void **)((unsigned char *)self->game_ctx + 4);
            v->levels[0] = *(GridLevel **)((unsigned char *)self->game_ctx + 8);
            while (nlev < 16 && v->levels[nlev] != NULL) {
                if (v->levels[nlev]->next_level == NULL) break;
                v->levels[nlev + 1] = v->levels[nlev]->next_level;
                nlev++;
            }
            nlev++;
            v->leaf = nlev - 1;
            v->sx = v->levels[v->leaf]->size_x;
            v->sy = v->levels[v->leaf]->size_y;
            v->cells = (unsigned char *)v->levels[v->leaf]->cells;
            v->rows = v->levels[v->leaf]->rows;
        }
        v->nlev = nlev;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v->ok = 0;
    }
}

/* Единственное условие «сетка валидна → запись будет реальной, а не error».
 * Его используют и error-ветка cap_write_grid_record, и предикат bdd0-пути,
 * поэтому предикат и писатель не могут разойтись. */
static int cap_grid_ok(const cap_grid_view *v)
{
    return v->ok && v->sx > 0 && v->sy > 0 && v->cells != NULL &&
           v->rows != NULL && (long long)v->sx * v->sy <= CAP_MAX_GRID_CELLS;
}

static void cap_write_grid_record(BattleAI *self)
{
    cap_grid_view v;
    cap_grid_deps dep;
    int n = 0, i;

    cap_read_grid_deps(self, &dep);
    cap_grid_read(self, &v);
    if (!cap_grid_ok(&v)) {
        fprintf(g_file, "{\"v\":5,\"kind\":\"grid\",\"error\":1,"
                        "\"sx\":%d,\"sy\":%d}\n", v.sx, v.sy);
        return;
    }
    n = v.sx * v.sy;
    __try {
        for (i = 0; i < n; i++) {
            unsigned char *c = v.cells + i * S6_GRID_CELL_SIZE;
            void *kr = *(void **)(c + 0x20);
            void *nx = *(void **)(c + 0x24);
            void *ch = *(void **)(c + 0x28);
            int key = (kr != NULL) ? *(int *)kr : 0;

            /* Координаты ячейки лежат в +0x0c/+0x10 и НЕ совпадают с её
             * индексом в массиве (rows[x][y] даёт индекс). */
            g_gw0[i] = *(int *)(c + 0x00);
            g_gw1[i] = *(int *)(c + 0x04);
            g_gw2[i] = *(int *)(c + 0x08);
            g_gw3[i] = *(int *)(c + 0x0c);
            g_gw4[i] = *(int *)(c + 0x10);
            g_gkey[i] = key;
            g_gstatus[i] = native::tree_lookup(v.world, (unsigned int)key);
            g_gmin[i] = *(int *)(c + 0x14);
            g_gmax[i] = *(int *)(c + 0x18);
            /* +0x1c — уровень ячейки (FUN_00456b20 через spec->pos) */
            g_gnext[i] = (nx >= v.cells && nx < v.cells + (size_t)n * S6_GRID_CELL_SIZE)
                         ? (int)(((unsigned char *)nx - v.cells) / S6_GRID_CELL_SIZE) : -1;
            g_gchain[i] = (ch >= v.cells && ch < v.cells + (size_t)n * S6_GRID_CELL_SIZE)
                          ? (int)(((unsigned char *)ch - v.cells) / S6_GRID_CELL_SIZE) : -1;
        }
        g_grid_n = n;
        memcpy(g_tree_prev, g_gstatus, (size_t)n * sizeof(int));
        cap_read_cell_fields(v.cells, n, g_prev_fields);
        for (i = 0; i < n; i++) {
            g_prev_rows[i] = v.rows[i / v.sy][i % v.sy];
        }
        for (i = 0; i < n; i++) {
            int x = i / v.sy, y = i % v.sy;
            g_grows[i] = v.rows[x][y];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(g_file, "{\"v\":5,\"kind\":\"grid\",\"error\":1,"
                        "\"sx\":%d,\"sy\":%d,\"trap\":1}\n", v.sx, v.sy);
        return;
    }

    /* cells_base/rows_base — сырые игровые указатели листа; офлайн-харнесс
     * переводит ими cell-указатель порта в индекс ячейки. */
    fprintf(g_file, "{\"v\":%d,\"kind\":\"grid\",\"sx\":%d,\"sy\":%d,"
                    "\"cells_base\":%u,\"rows_base\":%u,"
                    "\"leaf\":%d,\"levels\":[", CAP_VERSION, v.sx, v.sy,
            (unsigned int)(size_t)v.cells, (unsigned int)(size_t)v.rows,
            v.leaf);
    for (i = 0; i < v.nlev; i++) {
        fprintf(g_file, "%s{\"i\":%d,\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d,"
                        "\"sa\":%u,\"sl\":%u,\"sm\":%u,\"next\":%d}",
                i ? "," : "", i, v.levels[i]->size_x, v.levels[i]->size_y,
                v.levels[i]->rec_x, v.levels[i]->rec_y,
                *(unsigned int *)((unsigned char *)v.levels[i] + 0x20),
                *(unsigned int *)((unsigned char *)v.levels[i] + 0x24),
                *(unsigned int *)((unsigned char *)v.levels[i] + 0x28),
                (i + 1 < v.nlev) ? i + 1 : -1);
    }
    fputs("],\"world\":{\"w110\":", g_file);
    fprintf(g_file, "%u,\"vt\":%u,\"a1\":%u,\"obj4\":%u,\"wscale\":%u},",
            dep.w110, dep.vt, dep.a1, dep.obj4, dep.wscale);
    fputs("\"rows\":", g_file);
    cap_write_ints(g_file, g_grows, n);
    fputs(",\"cells\":{\"key\":", g_file);
    cap_write_ints(g_file, g_gkey, n);
    fputs(",\"status\":", g_file);
    cap_write_ints(g_file, g_gstatus, n);
    fputs(",\"min\":", g_file);
    cap_write_ints(g_file, g_gmin, n);
    fputs(",\"max\":", g_file);
    cap_write_ints(g_file, g_gmax, n);
    fputs(",\"next\":", g_file);
    cap_write_ints(g_file, g_gnext, n);
    fputs(",\"chain\":", g_file);
    cap_write_ints(g_file, g_gchain, n);
    fputs(",\"w0\":", g_file);
    cap_write_ints(g_file, g_gw0, n);
    fputs(",\"w1\":", g_file);
    cap_write_ints(g_file, g_gw1, n);
    fputs(",\"w2\":", g_file);
    cap_write_ints(g_file, g_gw2, n);
    fputs(",\"w3\":", g_file);
    cap_write_ints(g_file, g_gw3, n);
    fputs(",\"w4\":", g_file);
    cap_write_ints(g_file, g_gw4, n);
    fputs("}}\n", g_file);
}

/* v16: ответил бы cap_write_grid_record реальной записью (а не error) для этого
 * BattleAI? Только чтение, под __try, без запи. Условие — то же cap_grid_ok,
 * что и в error-ветке писателя, поэтому предикат и писатель не расходятся.
 * Нужен bdd0-пути: первые юниты боя могут иметь game_ctx == 0, и одноразовый
 * флаг g_grid_written не должен расходоваться на них. */
static int cap_grid_record_would_write(BattleAI *self)
{
    cap_grid_view v;

    cap_grid_read(self, &v);
    return cap_grid_ok(&v);
}

/* v5: world+0x104 (tree_lookup) меняется в ходе боя — снимаем ТОЛЬКО дельты
 * статусов относительно предыдущего вызова. Ключи — индексы ячеек grid.cells. */
static int cap_world110(BattleAI *self)
{
    int v = 0;
    __try {
        unsigned char *w0 = (self != NULL) ? *(unsigned char **)self : NULL;
        unsigned char *world = (w0 != NULL) ? *(unsigned char **)(w0 + 4) : NULL;
        if (world != NULL) v = *(unsigned char *)(world + 0x110);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return v;
}

static void cap_write_tree_delta(BattleAI *self)
{
    int nd = 0, i;
    void *world = NULL;

    g_tree_overflow = 0;
    if (g_grid_n <= 0) { fputs("[]", g_file); return; }
    __try {
        unsigned char *w0 = (self != NULL) ? *(unsigned char **)self : NULL;
        world = (w0 != NULL) ? *(unsigned char **)(w0 + 4) : NULL;
        for (i = 0; i < g_grid_n; i++) {
            int st = native::tree_lookup(world, (unsigned int)g_gkey[i]);
            if (st == g_tree_prev[i]) continue;
            if (nd < CAP_MAX_TREE_DELTA) {
                g_td_idx[nd] = i;
                g_td_val[nd] = st;
                nd++;
            } else {
                g_tree_overflow = 1;
            }
            g_tree_prev[i] = st;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    fputc('[', g_file);
    for (i = 0; i < nd; i++) {
        fprintf(g_file, "%s[%d,%d]", i ? "," : "", g_td_idx[i], g_td_val[i]);
    }
    fputc(']', g_file);
}

/* v5: дельты STATIC-сетки (если движок меняет метрики/связи ячеек в бою). */
static void cap_write_cell_delta(BattleAI *self)
{
    unsigned char *cells = NULL;
    int nd = 0, i, k;

    g_cell_overflow = 0;
    if (g_grid_n <= 0) { fputs("[]", g_file); return; }
    __try {
        unsigned char *ctx = (unsigned char *)self->game_ctx;
        GridLevel *lvl = (ctx != NULL)
                         ? *(GridLevel **)(ctx + 8) : NULL;
        while (lvl != NULL && lvl->next_level != NULL) lvl = lvl->next_level;
        cells = (lvl != NULL) ? (unsigned char *)lvl->cells : NULL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (cells == NULL) { fputs("[]", g_file); return; }
    {
        static int *cur = NULL;
        if (cur == NULL) cur = (int *)malloc((size_t)CAP_MAX_GRID_CELLS * 11 * sizeof(int));
        if (cur == NULL) { fputs("[]", g_file); return; }
        cap_read_cell_fields(cells, g_grid_n, cur);
        for (i = 0; i < g_grid_n; i++) {
            int *a = &cur[i * 11], *b = &g_prev_fields[i * 11];
            int diff = 0;
            for (k = 0; k < 11; k++) if (a[k] != b[k]) { diff = 1; break; }
            if (!diff) continue;
            if (nd < CAP_MAX_CELL_DELTA) {
                g_cd_idx[nd] = i;
                memcpy(&g_cd_val[nd * 11], a, 11 * sizeof(int));
                nd++;
                memcpy(b, a, 11 * sizeof(int));
            } else {
                g_cell_overflow = 1;
            }
        }
    }
    fputc('[', g_file);
    for (i = 0; i < nd; i++) {
        fprintf(g_file, "%s[%d", i ? "," : "", g_cd_idx[i]);
        for (k = 0; k < 11; k++) {
            fprintf(g_file, ",%d", g_cd_val[i * 11 + k]);
        }
        fputc(']', g_file);
    }
    fputc(']', g_file);
}

/* v5: дельты rows[x][y] — индекс ячейки, который читает FUN_004570d0. */
static void cap_write_row_delta(BattleAI *self)
{
    int **rows = NULL, nd = 0, i;
    int sx = 0, sy = 0;

    if (g_grid_n <= 0) { fputs("[]", g_file); return; }
    __try {
        unsigned char *ctx = (unsigned char *)self->game_ctx;
        GridLevel *lvl = (ctx != NULL) ? *(GridLevel **)(ctx + 8) : NULL;
        while (lvl != NULL && lvl->next_level != NULL) lvl = lvl->next_level;
        if (lvl != NULL) {
            sx = lvl->size_x; sy = lvl->size_y; rows = lvl->rows;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (rows == NULL || sx * sy != g_grid_n) { fputs("[]", g_file); return; }
    __try {
        for (i = 0; i < g_grid_n; i++) {
            int v = rows[i / sy][i % sy];
            if (v == g_prev_rows[i]) continue;
            if (nd < CAP_MAX_CELL_DELTA) {
                g_cd_idx[nd] = i;
                g_cd_val[nd * 11] = v;
                nd++;
            }
            g_prev_rows[i] = v;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    fputc('[', g_file);
    for (i = 0; i < nd; i++) {
        fprintf(g_file, "%s[%d,%d]", i ? "," : "", g_cd_idx[i], g_cd_val[i * 11]);
    }
    fputc(']', g_file);
}

/* --------------------------- точки захвата ------------------------------ */

void battle_capture_tick_begin(void *unit, int tick, int unit_id)
{
    (void)unit;
    if (!g_enabled) {
        return;
    }
    g_tick = tick;
    g_unit = unit_id;
}

void battle_capture_e0a0_enter(void *this_, int filter, void *cmp,
                               void *timeout_bits, void *out_code)
{
    if (!g_e0) {
        return;
    }
    BattleAI *self = (BattleAI *)this_;
    unsigned int timer = 0xFFFFFFFFu, finalb = 0xFFFFFFFFu, distb = 0xFFFFFFFFu;
    unsigned int ctxflags = 0, worldscale = 0;
    cap_pos anchor = { -1, -1 };

    if (!g_enabled || g_closed) {
        return;
    }
    if (g_in_eee0 && !g_eee0_b9_valid && g_path_n == CAP_WORDS &&
        g_path[0] != 0x7fffffff) {
        int k, cnt = g_chain_leaf[0];
        for (k = 0; k < CAP_WORDS; k++) g_eee0_b9_w[k] = g_path[k];
        g_eee0_b9_addr = g_pptr[0];
        g_eee0_b9_null = 0;
        g_eee0_b9_scale = g_cb40_scale;
        g_eee0_b9_scale_ok = g_cb40_scale_ok;
        for (k = 0; k < CAP_GATE_WORDS; k++) g_eee0_b9_gate[k] = g_gate[k];
        g_eee0_b9_chain[0] = (cnt >= 0 && cnt <= CAP_CHAIN_MAX) ? cnt : 0;
        for (k = 0; k < g_eee0_b9_chain[0] * CAP_CHAIN_WORDS; k++) {
            g_eee0_b9_chain[1 + k] = g_chain_leaf[1 + k];
        }
        g_eee0_b9_valid = 1;
    }
    if (g_seq >= g_max_calls) {
        g_closed = 1;
        fclose(g_file);
        g_file = NULL;
        ld_info("battle_capture: reached max=%d — file closed", g_max_calls);
        return;
    }

    g_this = this_;
    g_out_code = out_code;
    g_filter = filter;
    g_cmp.x = -1; g_cmp.y = -1;
    g_timeout_bits = (unsigned int)(size_t)timeout_bits;
    g_iter_n = 0;
    g_path_n = 0; g_heur_n = 0; g_tout_n = 0;
    g_leaf_overflow = 0;

    cap_read_pos(cmp, &g_cmp);
    g_cmp_addr = (int)(size_t)cmp;
    memset(g_ai_words, 0, sizeof(g_ai_words));
    __try {
        if (self != NULL) {
            memcpy(g_ai_words, self, sizeof(g_ai_words));
            memcpy(&timer, &self->timer_start_sec, sizeof(timer));
            memcpy(&finalb, &self->final_threshold, sizeof(finalb));
            memcpy(&distb, &self->dist_threshold, sizeof(distb));
            cap_read_pos(self->anchor, &anchor);
            if (self->game_ctx != NULL) {
                ctxflags = *(const unsigned int *)self->game_ctx;
                worldscale = *(const unsigned int *)(
                    (const unsigned char *)self->game_ctx + 0x120);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        timer = finalb = distb = 0xFFFFFFFFu;
    }

    cap_read_map1(self);
    cap_read_occ(self);

    g_seq++;
    if (!g_grid_written) {
        g_grid_written = 1;
        cap_write_grid_record(self);   /* v5: статическая сетка + входы эвристики */
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"cap\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"e0a0\",\"args\":{\"filter\":%d,"
            "\"cmp\":[%d,%d],\"cmp_addr\":%d,\"timeout_bits\":%u,\"out_ptr\":%d},"
            "\"ai\":{\"timer_bits\":%u,\"final_bits\":%u,"
            "\"dist_bits\":%u,\"yrad\":%d,\"xrad\":%d,\"rect\":[%d,%d,%d,%d],"
            "\"anchor\":[%d,%d],\"trk\":%d,\"bd2\":%u,\"code\":%d,"
            "\"ctx_flags\":%u,\"world_scale_bits\":%u},"
            "\"w110\":%u,\"tree_delta\":",
            CAP_VERSION, g_seq, g_tick, g_unit, g_filter,
            g_cmp.x, g_cmp.y, g_cmp_addr,
            g_timeout_bits,
            out_code ? 1 : 0, timer, finalb, distb,
            self ? self->y_radius : -1, self ? self->x_radius : -1,
            self ? self->canvas_rect.left : -1, self ? self->canvas_rect.top : -1,
            self ? self->canvas_rect.right : -1, self ? self->canvas_rect.bottom : -1,
            anchor.x, anchor.y, self ? (int)self->tracking : -1,
            self ? self->distance_threshold : 0xFFFFFFFFu,
            self ? *(const int *)((const unsigned char *)self + 0x38) : -1,
            ctxflags, worldscale, cap_world110(self));
    cap_write_tree_delta(self);
    fputs(",\"cell_delta\":", g_file);
    cap_write_cell_delta(self);
    fputs(",\"row_delta\":", g_file);
    cap_write_row_delta(self);
    fputs(",\"map1\":", g_file);
    cap_write_map1(g_file);
    fputs(",\"occ\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    fputs(",\"occ_nodes\":", g_file);
    cap_write_occ_nodes(g_file);
    fputs(",\"ai_words\":", g_file);
    cap_write_ai_words(g_file);
    fputs(",\"grid\":", g_file);
    cap_write_grid(g_file, self);
    fprintf(g_file, ",\"trap\":%d,\"tree_overflow\":%d,\"cell_overflow\":%d}\n",
            g_map_trap, g_tree_overflow, g_cell_overflow);
    {
        static volatile LONG dbg = 0;
        if (ld_rate(&dbg, 3, 50)) {
            ld_info("battle_capture: cap seq=%d tick=%d unit=%d filter=%d "
                    "map1=%d occ=%d", g_seq, g_tick, g_unit, g_filter,
                    g_node_n, g_occ_n);
        }
    }
}

void battle_capture_eee0_enter(void *self, void *rec)
{
    BattleAI *ai = (BattleAI *)self;
    unsigned char *r = (unsigned char *)rec;
    int rec_w[7] = {0, 0, 0, 0, 0, 0, 0};
    int sub_w[4] = {0, 0, 0, 0};
    int grid_w[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int ctx_w[7] = {0, 0, 0, 0, 0, 0, 0};
    int s68 = 0, s6c = 0, s28 = 0, s2c = 0, s30 = -1, s38 = -1;
    unsigned int s34 = 0xFFFFFFFFu, ctx_flags = 0;
    int ctx_present = 0, rec_ok = 0, sub_ok = 0, grid_ok = 0;
    int world_134 = 0;
    cap_pos anchor = { -1, -1 };

    if (!g_enabled || g_closed) {
        g_eee0_native_valid = 0;
        return;
    }
    /* Запись bdd0_native для 0x45eee0 (сайт 0x44cbd0) принадлежит bdd0-слою:
     * порт запрашивает этот натив в R17, и без записи поток нативов неполон.
     * Поэтому она снимается и при LDHOOK_BATTLE_CAPTURE_E0=0 — флаг глушит
     * только e0a0-слой (eee0_cap/eee0_res и карты кандидатов). */
    g_eee0_native_valid = 0;
    if (g_snap && g_bdd0_sp > 0) {
        bdd0_frame *fr = &g_bdd0_stack[g_bdd0_sp - 1];
        g_eee0_native_order = fr->call_order++;
        g_eee0_native_seq = fr->seq;
        g_eee0_native_ecx = (int)(size_t)self;
        g_eee0_native_arg = (int)(size_t)rec;
        g_eee0_native_valid = 1;
    }
    if (!g_e0) {
        return;                     /* e0a0-слой выключен */
    }
    if (g_seq >= g_max_calls) {
        g_closed = 1;
        g_eee0_native_valid = 0;
        fclose(g_file);
        g_file = NULL;
        ld_info("battle_capture: reached max=%d — file closed", g_max_calls);
        return;
    }

    g_in_eee0 = 1;
    g_cur_eee0_seq = ++g_eee0_seq;
    g_eee0_b9_valid = 0;
    g_eee0_b9_null = 0;
    g_eee0_b9_addr = 0;
    g_iter_n = 0;
    g_path_n = 0; g_heur_n = 0; g_tout_n = 0;
    g_leaf_overflow = 0;

    if (rec != NULL) {
        __try {
            int k;
            for (k = 0; k < 7; k++) rec_w[k] = *(const int *)(r + k * 4);
            rec_ok = 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            rec_ok = 0;
        }
        if (rec_ok) {
            __try {
                const unsigned char *sub = *(const unsigned char *const *)(r + 0x0c);
                if (sub != NULL) {
                    int k;
                    for (k = 0; k < 4; k++) sub_w[k] = *(const int *)(sub + k * 4);
                    sub_ok = 1;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    if (ai != NULL) {
        __try {
            s68 = (int)(size_t)ai->map1;
            s6c = (int)(size_t)ai->cand_head;
            s28 = *(const int *)((const unsigned char *)ai + 0x28);
            s2c = (int)(size_t)ai->anchor;
            s30 = (int)ai->tracking;
            s34 = ai->distance_threshold;
            s38 = *(const int *)((const unsigned char *)ai + 0x38);
            cap_read_pos(ai->anchor, &anchor);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        __try {
            void *w0 = ai->game_ctx;
            if (w0 != NULL) {
                ctx_present = 1;
                ctx_flags = *(const unsigned int *)w0;
                for (int k = 0; k < 7; k++) ctx_w[k] = *(const int *)((const unsigned char *)w0 + k * 4);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (rec_ok && rec_w[4] == 0) {
            __try {
                const unsigned char *world =
                    (const unsigned char *)*(const void *const *)(r + 0x04);
                if (world != NULL) {
                    world_134 = *(const int *)(world + 0x134);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        if (rec_ok) {
            __try {
                int gptr = (rec_w[4] != 0) ? rec_w[4] : world_134;
                if (gptr != 0) {
                    int k;
                    for (k = 0; k < 8; k++) grid_w[k] = *(const int *)((const unsigned char *)(size_t)gptr + k * 4);
                    grid_ok = 1;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }

    cap_read_map1(ai);
    cap_read_occ(ai);
    cap_read_ai_words(ai);

    fprintf(g_file, "{\"v\":%d,\"kind\":\"eee0_cap\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"eee0\",\"rec\":{\"addr\":%d,\"ok\":%d,"
            "\"w\":[%d,%d,%d,%d,%d,%d,%d],\"sub_ok\":%d,"
            "\"sub\":[%d,%d,%d,%d]},"
            "\"ai\":{\"s68\":%d,\"s6c\":%d,\"s28\":%d,\"s2c\":%d,"
            "\"anchor\":[%d,%d],\"s30\":%d,\"s34\":%u,\"s38\":%d,"
            "\"ctx_present\":%d,\"ctx_flags\":%u,\"world_134\":%d,\"w110\":%d,"
            "\"ctx\":[%d,%d,%d,%d,%d,%d,%d],"
            "\"grid_ok\":%d,\"grid\":[%d,%d,%d,%d,%d,%d,%d,%d]},"
            "\"trap\":%d,\"map1\":",
            CAP_VERSION, g_cur_eee0_seq, g_tick, g_unit,
            (int)(size_t)rec, rec_ok,
            rec_w[0], rec_w[1], rec_w[2], rec_w[3], rec_w[4], rec_w[5], rec_w[6],
            sub_ok, sub_w[0], sub_w[1], sub_w[2], sub_w[3],
            s68, s6c, s28, s2c, anchor.x, anchor.y, s30, s34, s38,
            ctx_present, ctx_flags, world_134, cap_world110(ai),
            ctx_w[0], ctx_w[1], ctx_w[2], ctx_w[3], ctx_w[4], ctx_w[5], ctx_w[6],
            grid_ok, grid_w[0], grid_w[1], grid_w[2], grid_w[3],
            grid_w[4], grid_w[5], grid_w[6], grid_w[7],
            g_map_trap);
    cap_write_map1(g_file);
    fputs(",\"occ\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    fputs(",\"occ_nodes\":", g_file);
    cap_write_occ_nodes(g_file);
    fputs(",\"ai_words\":", g_file);
    cap_write_ai_words(g_file);
    fputs("}\n", g_file);

    {
        static volatile LONG dbg = 0;
        if (ld_rate(&dbg, 3, 50)) {
            ld_info("battle_capture: eee0_cap seq=%d tick=%d unit=%d "
                    "map1=%d occ=%d", g_cur_eee0_seq, g_tick, g_unit,
                    g_node_n, g_occ_n);
        }
    }
}

void battle_capture_latch(void *cand)
{
    if (!g_e0) {
        return;
    }
    if (!g_enabled || g_closed) {
        return;
    }
    if (g_iter_n >= g_iter_cap) {
        return;
    }
    cap_read_cand_pos(cand, &g_iter[g_iter_n++]);   /* NULL → (-1,-1) → null */
}

void battle_capture_eee0_exit(int code, void *self)
{
    BattleAI *ai = (BattleAI *)self;
    unsigned char *self_ptr = (unsigned char *)self;
    int s28 = 0, s2c = 0, s30 = -1, s38 = -1;
    unsigned int s34 = 0xFFFFFFFFu, ctx_flags = 0;
    int anchor_addr = 0, anchor_f14 = 0;
    cap_pos anchor = { -1, -1 };
    int i, k;

    if (!g_enabled || g_closed) {
        g_in_eee0 = 0;
        g_eee0_native_valid = 0;
        return;
    }
    /* bdd0_native для 0x45eee0 пишется ПЕРВЫМ и независимо от e0a0-слоя:
     * при LDHOOK_BATTLE_CAPTURE_E0=0 порт всё равно запрашивает этот натив в
     * R17, и без записи тик не воспроизводится (поток нативов неполон). */
    if (g_eee0_native_valid && g_snap && g_file != NULL) {
        /* 45eee0 writes BattleAI+0x00..0x38 (unit+0x138..0x170).  List from
         * the listing: 45c330 resets +0x04/+0x08/+0x0c/+0x10/+0x14/+0x18..
         * +0x27/+0x28/+0x2c/+0x30/+0x34/+0x38; 45eee0 also writes +0x00 and
         * +0x2c/+0x30/+0x34/+0x38.  Snapshot all of them post-call. */
        static const int k_eee0_offs[] = {
            0x00, 0x04, 0x08, 0x0c, 0x10, 0x14,
            0x18, 0x1c, 0x20, 0x24,
            0x28, 0x2c, 0x30, 0x34, 0x38
        };
        int a[1];
        int ww[32];
        int nww = 0;
        int wtrap = 0;
        int wi;

        a[0] = g_eee0_native_arg;
        for (wi = 0; wi < (int)(sizeof(k_eee0_offs) / sizeof(k_eee0_offs[0]));
             wi++) {
            __try {
                int v = *(const int *)(self_ptr + k_eee0_offs[wi]);
                ww[nww++] = 0x138 + k_eee0_offs[wi];
                ww[nww++] = v;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                wtrap = 1;
            }
        }
        battle_capture_bdd0_native_rec(0x44cbd0, 0x45eee0, 0, 0 /* NS_K_INT */,
                                       g_eee0_native_ecx, a, 1, code, NULL, 0,
                                       ww, nww,
                                       g_eee0_native_seq, g_eee0_native_order,
                                       wtrap);
        g_eee0_native_valid = 0;
    }
    if (!g_e0) {
        return;                     /* e0a0-слой (eee0_cap/eee0_res) выключен */
    }
    if (!g_eee0_b9_valid && g_path_n == CAP_WORDS && g_path[0] != 0x7fffffff) {
        int cnt = g_chain_leaf[0];
        for (k = 0; k < CAP_WORDS; k++) g_eee0_b9_w[k] = g_path[k];
        g_eee0_b9_addr = g_pptr[0];
        g_eee0_b9_null = 0;
        g_eee0_b9_scale = g_cb40_scale;
        g_eee0_b9_scale_ok = g_cb40_scale_ok;
        for (k = 0; k < CAP_GATE_WORDS; k++) g_eee0_b9_gate[k] = g_gate[k];
        g_eee0_b9_chain[0] = (cnt >= 0 && cnt <= CAP_CHAIN_MAX) ? cnt : 0;
        for (k = 0; k < g_eee0_b9_chain[0] * CAP_CHAIN_WORDS; k++) {
            g_eee0_b9_chain[1 + k] = g_chain_leaf[1 + k];
        }
        g_eee0_b9_valid = 1;
    }
    g_in_eee0 = 0;

    if (ai == NULL) {
        return;
    }
    __try {
        s28 = *(const int *)(self_ptr + 0x28);
        s2c = (int)(size_t)ai->anchor;
        s30 = (int)ai->tracking;
        s34 = ai->distance_threshold;
        s38 = *(const int *)(self_ptr + 0x38);
        if (ai->game_ctx != NULL) {
            ctx_flags = *(const unsigned int *)ai->game_ctx;
        }
        if (ai->anchor != NULL) {
            anchor_addr = (int)(size_t)ai->anchor;
            memcpy(&anchor_f14, (const unsigned char *)ai->anchor + 0x14, sizeof(int));
        }
        cap_read_pos(ai->anchor, &anchor);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    cap_read_map1(ai);
    cap_read_occ(ai);
    cap_read_ai_words(ai);

    fprintf(g_file, "{\"v\":%d,\"kind\":\"eee0_res\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"eee0\",\"ret\":%d,"
            "\"ai\":{\"s28\":%d,\"s2c\":%d,\"s30\":%d,\"s34\":%u,\"s38\":%d,"
            "\"ctx_flags\":%u},"
            "\"anchor\":{\"addr\":%d,\"pos\":[%d,%d],\"f14\":%d},"
            "\"map1_after\":",
            CAP_VERSION, g_cur_eee0_seq, g_tick, g_unit, code,
            s28, s2c, s30, s34, s38, ctx_flags,
            anchor_addr, anchor.x, anchor.y, anchor_f14);
    cap_write_map1(g_file);
    fputs(",\"occ_after\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    fputs(",\"occ_nodes_after\":", g_file);
    cap_write_occ_nodes(g_file);
    fputs(",\"ai_words_after\":", g_file);
    cap_write_ai_words(g_file);
    if (g_eee0_b9_valid) {
        int cnt = g_eee0_b9_chain[0];
        fputs(",\"b9_leaf\":{\"null\":0,\"w\":", g_file);
        cap_write_ints(g_file, g_eee0_b9_w, CAP_WORDS);
        fprintf(g_file, ",\"ptr\":%d,\"scale\":%u,\"scale_ok\":%d,\"gate\":",
                g_eee0_b9_addr, g_eee0_b9_scale, g_eee0_b9_scale_ok);
        cap_write_ints(g_file, g_eee0_b9_gate, CAP_GATE_WORDS);
        fputs(",\"chain\":[", g_file);
        fprintf(g_file, "%d", cnt);
        for (i = 0; i < cnt; i++) {
            const int *e = &g_eee0_b9_chain[1 + i * CAP_CHAIN_WORDS];
            for (k = 0; k < CAP_CHAIN_WORDS; k++) {
                fprintf(g_file, ",%d", e[k]);
            }
        }
        fputs("]}", g_file);
    } else {
        fputs(",\"b9_leaf\":null", g_file);
    }
    fprintf(g_file, ",\"trap\":%d}\n", g_map_trap);

    {
        static volatile LONG dbg = 0;
        if (ld_rate(&dbg, 3, 50)) {
            ld_info("battle_capture: eee0_res seq=%d tick=%d unit=%d ret=%d "
                    "map1=%d occ=%d b9=%d", g_cur_eee0_seq, g_tick, g_unit, code,
                    g_node_n, g_occ_n, g_eee0_b9_valid);
        }
    }
}

/* ------------------- v12: база64 + снапшот входа (bdd0_in) -------------- *
 * Снапшот читает ровно те области, которые тело 44bdd0 читает (сверено по
 * battle/tick.cpp: unit+0x34/0x64..0x7f/0x68..0x74/0x84/0xa4..0xac/0xe0/0xfc/
 * 0x114/0x118/0x128/0x12c/0x170 — все внутри 0x180; frame+0x00/0x10..
 * 0x18/0x1c/0x20/0x2c..0x38/0x40/0x4c/0x50/0x58/0x60..0xa0/0xa4..0xc0/0xc0/
 * 0x154/0x15c/0x160/0x164 — все внутри 0x168).
 * BattleAI ВСТРОЕН в unit по +0x138 (0x80 Б, unit+0x138..+0x1b7) — снимается
 * базой+смещением без разыменования; `*(unit+0x138)` — это game_ctx (поле
 * `ai_ctx`, не BattleAI). Поля ленты от BattleAI: +0x5c/+0x60/+0x64/+0x7c.
 * Каждое разыменование под __try; сбой → trap=1, блок пишется как null. */

static const char cap_b64_tab[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int cap_b64_encode(char *out, const unsigned char *in, int n)
{
    int i = 0, o = 0;

    while (i + 2 < n) {
        unsigned int v = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) | in[i + 2];
        out[o++] = cap_b64_tab[(v >> 18) & 63];
        out[o++] = cap_b64_tab[(v >> 12) & 63];
        out[o++] = cap_b64_tab[(v >> 6) & 63];
        out[o++] = cap_b64_tab[v & 63];
        i += 3;
    }
    if (n - i == 1) {
        unsigned int v = (unsigned int)in[i] << 16;
        out[o++] = cap_b64_tab[(v >> 18) & 63];
        out[o++] = cap_b64_tab[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (n - i == 2) {
        unsigned int v = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8);
        out[o++] = cap_b64_tab[(v >> 18) & 63];
        out[o++] = cap_b64_tab[(v >> 12) & 63];
        out[o++] = cap_b64_tab[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

static void cap_write_b64_field(FILE *f, int ok, const unsigned char *p, int n)
{
    if (!ok) {
        fputs("null", f);
        return;
    }
    cap_b64_encode(g_b64, p, n);
    fputc('"', f);
    fputs(g_b64, f);
    fputc('"', f);
}

/* v16 (аддитивно): геометрия ленты для портов 45b150/45b3e0. На каждый из n
 * захваченных элементов (те же, что несёт band_b64; n ≤ CAP_BDD0_BAND_MAX)
 * читается elem+0x14 → LDPos*, из него +0x0c/+0x10 (int-координаты записи) и
 * +0x1c → GridLevel, из которого +0x20/+0x24/+0x28 (биты float: scale_add/
 * scale_load/scale_mul). Каждый элемент под __try; NULL-указатель или сбой →
 * ok=0, остальные массивы 0 (значения не фабрикуются). Потолок
 * CAP_BDD0_BAND_MAX: элементы за ним не снимаются — загрузчик тик, у которого
 * (end−begin)/0x18 больше захваченного числа, считает немоделированным. */
static void cap_read_band_pos(unsigned int bb, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        g_band_pos_ok[i] = 0;
        g_band_pos_x[i] = 0;
        g_band_pos_y[i] = 0;
        g_band_pos_sa[i] = 0;
        g_band_pos_sl[i] = 0;
        g_band_pos_sm[i] = 0;
        __try {
            unsigned char *elem = (unsigned char *)(size_t)bb +
                                  (size_t)i * CAP_BDD0_BAND_ELEM;
            void *pos = *(void **)(elem + 0x14);
            if (pos != NULL) {
                void *lvl = *(void **)((const unsigned char *)pos + 0x1c);
                if (lvl != NULL) {
                    g_band_pos_sa[i] = *(const unsigned int *)((const unsigned char *)lvl + 0x20);
                    g_band_pos_sl[i] = *(const unsigned int *)((const unsigned char *)lvl + 0x24);
                    g_band_pos_sm[i] = *(const unsigned int *)((const unsigned char *)lvl + 0x28);
                    g_band_pos_x[i] = *(const int *)((const unsigned char *)pos + 0x0c);
                    g_band_pos_y[i] = *(const int *)((const unsigned char *)pos + 0x10);
                    g_band_pos_ok[i] = 1;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_band_pos_ok[i] = 0;
            g_band_pos_x[i] = 0;
            g_band_pos_y[i] = 0;
            g_band_pos_sa[i] = 0;
            g_band_pos_sl[i] = 0;
            g_band_pos_sm[i] = 0;
        }
    }
}

/* v16: bdd0_in "band_pos" — параллельные массивы длиной n; n==0 → null. */
static void cap_write_band_pos(FILE *f, int n)
{
    if (n <= 0) {
        fputs("null", f);
        return;
    }
    fputs("{\"ok\":", f);
    cap_write_ints(f, g_band_pos_ok, n);
    fputs(",\"x\":", f);
    cap_write_ints(f, g_band_pos_x, n);
    fputs(",\"y\":", f);
    cap_write_ints(f, g_band_pos_y, n);
    fputs(",\"sa\":", f);
    cap_write_uints(f, g_band_pos_sa, n);
    fputs(",\"sl\":", f);
    cap_write_uints(f, g_band_pos_sl, n);
    fputs(",\"sm\":", f);
    cap_write_uints(f, g_band_pos_sm, n);
    fputc('}', f);
}

static void cap_write_bdd0_in(int seq, void *self, void *esp, int p1, int p2,
                              int p3, int ai_ctx, int u170,
                              unsigned int gate114)
{
    int frame_ok = 1, unit_ok = 1, ai_ok = 1, trap = 0;
    int band_ok = 1, band_n = 0, gs_ok = 0, gctx0_ok = 0;
    int ctx_pos_ok = 0;
    int ctx_pos[3] = { 0, 0, 0 };
    unsigned int bb = 0, be = 0, bc = 0, bd = 0;
    unsigned int e0_v = 0, e044_v = 0, tree_v = 0;
    int gctx0 = 0;
    int band_count = 0;

    if (esp != NULL) {
        __try {
            memcpy(g_snap_frame, esp, CAP_BDD0_FRAME_SIZE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            frame_ok = 0;
            trap = 1;
        }
    } else {
        frame_ok = 0;
    }
    if (self != NULL) {
        __try {
            memcpy(g_snap_unit, self, CAP_BDD0_UNIT_SIZE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            unit_ok = 0;
            trap = 1;
        }
        /* BattleAI ВСТРОЕН: блок снимается по адресу unit+0x138 (без deref;
         * *(unit+0x138) — это game_ctx, пишется отдельно как ai_ctx). */
        __try {
            memcpy(g_snap_ai, (const unsigned char *)self + CAP_BDD0_AI_OFF,
                   CAP_BDD0_AI_SIZE);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ai_ok = 0;
            trap = 1;
        }
    } else {
        unit_ok = 0;
        ai_ok = 0;
    }
    if (ai_ok) {
        memcpy(&bb, g_snap_ai + CAP_BDD0_BAND_BEGIN, sizeof(bb));
        memcpy(&be, g_snap_ai + CAP_BDD0_BAND_END, sizeof(be));
        memcpy(&bc, g_snap_ai + CAP_BDD0_BAND_CAP, sizeof(bc));
        memcpy(&bd, g_snap_ai + CAP_BDD0_BAND_DIRTY, sizeof(bd));
        if (bb != 0 && be != 0) {
            band_count = (int)((int)(be - bb) / (int)CAP_BDD0_BAND_ELEM);
        }
    }
    /* v12 (аддитивно): элементы ленты — только в пределах потолка. */
    band_n = 0;
    if (band_ok && band_count > 0) {
        int n = band_count;
        if (n > CAP_BDD0_BAND_MAX) n = CAP_BDD0_BAND_MAX;
        __try {
            memcpy(g_snap_band, (const void *)bb, (unsigned)n * CAP_BDD0_BAND_ELEM);
            band_n = n;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            band_n = 0;
        }
    }
    /* v12 (аддитивно): сырые указатели цепочки unit+0xe0 → [+0x44] → [+0x238]
     * и game_ctx[0] (первый dword `*(unit+0x138)`). */
    if (self != NULL) {
        __try {
            void *e0 = *(void * const *)((const unsigned char *)self + 0xe0);
            if (e0 != NULL) {
                e0_v = (unsigned)(size_t)e0;
                void *p44 = *(void * const *)((const unsigned char *)e0 + 0x44);
                if (p44 != NULL) {
                    e044_v = (unsigned)(size_t)p44;
                    void *tr = *(void * const *)((const unsigned char *)p44 + 0x238);
                    tree_v = (unsigned)(size_t)tr;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        __try {
            const void *gctx = *(void * const *)((const unsigned char *)self + 0x138);
            if (gctx != NULL) {
                gctx0 = *(const int *)gctx;
                gctx0_ok = 1;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            gctx0_ok = 0;
        }
        /* v15 (аддитивно): pos-тройка листа 45c6f0 — независимый источник.
         * Цепочка: unit+0x138 (game_ctx, то же значение, что `ai_ctx`) → +0xc
         * (указатель на тройку) → три dword. Объект ctx лежит в куче игры и в
         * снимке его нет, поэтому до v15 харнесс реконструировал тройку из `w`
         * записи 45c6f0 (тавтология, B0_POS_RESYNC_WHY). NULL-указатель или
         * сбой чтения — ключ "ctx_pos":null (значения нет). */
        __try {
            const void *gctx = *(void * const *)((const unsigned char *)self + 0x138);
            if (gctx != NULL) {
                const void *tri = *(void * const *)((const unsigned char *)gctx + 0xc);
                if (tri != NULL) {
                    memcpy(ctx_pos, tri, sizeof(ctx_pos));
                    ctx_pos_ok = 1;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ctx_pos_ok = 0;
        }
    }
    /* v12 (аддитивно): объект состояния игры FUN_00442570 (0x20 Б). Accessor
     * вызывается под __try; тело всё равно зовёт его 4+ раз за тик. */
    gs_ok = 0;
    if (self != NULL) {
        __try {
            const void *gs = native::game_state_get(self, 0);
            if (gs != NULL) {
                memcpy(g_snap_gs, gs, CAP_BDD0_GS_SIZE);
                gs_ok = 1;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            gs_ok = 0;
        }
    }

    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_in\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"44bdd0\",\"self\":%d,\"p1\":%d,\"p2\":%d,"
            "\"p3\":%d,\"ai_ctx\":%d,\"u170\":%d,\"gate114\":%u,\"frame_b64\":",
            CAP_BDD0_SNAP_VER, seq, g_tick, g_unit, (int)(size_t)self,
            p1, p2, p3, ai_ctx, u170, gate114);
    cap_write_b64_field(g_file, frame_ok, g_snap_frame, CAP_BDD0_FRAME_SIZE);
    fputs(",\"unit_b64\":", g_file);
    cap_write_b64_field(g_file, unit_ok, g_snap_unit, CAP_BDD0_UNIT_SIZE);
    fputs(",\"ai_b64\":", g_file);
    cap_write_b64_field(g_file, ai_ok, g_snap_ai, CAP_BDD0_AI_SIZE);
    /* v12: элементы ленты (band_count*0x18 Б) — без них офлайн-харнесс не
     * материализует чтения ленты в теле (arena.cpp, view "band"). */
    fputs(",\"band_b64\":", g_file);
    if (band_n > 0) {
        cap_b64_encode(g_b64_big, g_snap_band, band_n * CAP_BDD0_BAND_ELEM);
        fputc('"', g_file);
        fputs(g_b64_big, g_file);
        fputc('"', g_file);
    } else {
        fputs("null", g_file);
    }
    /* v16 (аддитивно): геометрия ленты (band_pos) для портов 45b150/45b3e0 —
     * те же band_n элементов, что несёт band_b64, из живой памяти (bb). */
    cap_read_band_pos(bb, band_n);
    fputs(",\"band_pos\":", g_file);
    cap_write_band_pos(g_file, band_n);
    /* v12: объект состояния игры FUN_00442570 (0x20 Б; тело зовёт аксессор
     * 4+ раз за тик) — ключ `gs_b64` (иначе contract_min считается по нулям). */
    fputs(",\"gs_b64\":", g_file);
    cap_write_b64_field(g_file, gs_ok, g_snap_gs, CAP_BDD0_GS_SIZE);
    /* v12: сырая цепочка разыменований снимка — ровно те ключи, что читает
     * arena.cpp (k_fixups): unit+0xe0 → [+0x44] → [+0x238], и game_ctx[0].
     * Ключи пишутся ТОЛЬКО когда цепочка реально снята (иначе has_chain=0 и
     * handles честно называет причину, а не выдаёт нули за данные). */
    if (e0_v != 0) {
        fprintf(g_file, ",\"e0\":%u,\"e0_44\":%u,\"tree\":%u",
                e0_v, e044_v, tree_v);
    }
    if (gctx0_ok) {
        fprintf(g_file, ",\"gctx0\":%d", gctx0);
    }
    /* v15: независимая pos-тройка `[*(unit+0x138)+0xc]` (лист 45c6f0). Ключ
     * пишется всегда: null — ctx нет или чтение не удалось; харнесс тогда
     * работает по-старому (реконструкция из `w`). */
    fputs(",\"ctx_pos\":", g_file);
    if (ctx_pos_ok) {
        fprintf(g_file, "[%d,%d,%d]", ctx_pos[0], ctx_pos[1], ctx_pos[2]);
    } else {
        fputs("null", g_file);
    }
    fprintf(g_file, ",\"band\":[%u,%u],\"band_begin\":%u,\"band_end\":%u,"
            "\"band_cap\":%u,\"band_dirty\":%u,\"band_count\":%d,"
            "\"gs_ok\":%d,\"trap\":%d}\n",
            bb, be, bb, be, bc, bd, band_count, gs_ok, trap);
}

void battle_capture_bdd0_leaf_rec(int site, int va, const void *ecx,
                                  const int *args, int nargs, int ret,
                                  const int *w, int nw, int seq, int order,
                                  int trap)
{
    int k;

    if (g_file == NULL) {
        return;
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_leaf\",\"seq\":%d,\"order\":%d,"
            "\"site\":\"%x\",\"va\":\"%x\",\"ecx\":%d,\"args\":[",
            CAP_BDD0_SNAP_VER, seq, order, site, va, (int)(size_t)ecx);
    for (k = 0; k < nargs; k++) {
        if (args != NULL) {
            fprintf(g_file, "%s%d", k ? "," : "", args[k]);
        }
    }
    fprintf(g_file, "],\"ret\":%d", ret);
    /* v12: dword'ы, записанные листом через указатель (45c6f0 out-тройка,
     * 45e1d0 тройка по возврату) — харнесс подменяет ими побочную запись. */
    if (w != NULL && nw > 0) {
        fputs(",\"w\":[", g_file);
        for (k = 0; k < nw; k++) {
            fprintf(g_file, "%s%d", k ? "," : "", w[k]);
        }
        fputc(']', g_file);
    }
    fprintf(g_file, ",\"trap\":%d}\n", trap);
}

void battle_capture_bdd0_leaf(int va, const void *ecx, const int *args,
                              int nargs, int ret, const int *w, int nw)
{
    bdd0_frame *fr;
    int order;

    if (!g_snap || !g_enabled || g_closed || g_file == NULL || g_bdd0_sp <= 0) {
        return;                          /* вне тика 44bdd0 / v12 выключен */
    }
    fr = &g_bdd0_stack[g_bdd0_sp - 1];
    order = fr->call_order++;
    battle_capture_bdd0_leaf_rec(0, va, ecx, args, nargs, ret, w, nw,
                                 fr->seq, order, 0);
}

/* v12: sink хуков call-сайтов (battle/leaf_sites.cpp). Порядок `order`
 * выдаётся на ВХОДЕ листа (порядок вызова, а не возврата) — вложенные листья
 * (458050/45e1d0) не переставляют поток. Пара seq/order живёт в LIFO:
 * token = индекс записи этого вызова 44bdd0. */
static int  g_leaf_q_seq[BDD0_STACK_MAX];
static int  g_leaf_q_order[BDD0_STACK_MAX];
static int  g_leaf_q_site[BDD0_STACK_MAX];
static int  g_leaf_q_n = 0;

/* v16 (аддитивно): ограниченный вариант s6_tree_next (порт 0x47f800) для
 * capture-side обхода карты. Тело — байт-в-байт s6_tree_next (is_nil +0x15,
 * right +0x08, parent +0x04), но каждый из двух внутренних циклов (спуск к
 * левому краю, подъём по parent) ограничен CAP_BDD0_MAP_MAX (512) итерациями;
 * при превышении — NULL, что отличает кап от натурального конца обхода (тот
 * возвращает head-сентинел, не NULL). __try не ловит бесконечный цикл, а
 * capture идёт в живой игре — кап обязателен. head в логике не участвует
 * (сигнатура симметрична cap_read_map). */
static TreeNode *cap_tree_next_bounded(TreeNode *cur, TreeNode *head)
{
    TreeNode *next;
    int guard;

    (void)head;                              /* сигнатура; логика — s6_tree_next */
    if (cur->is_nil) {
        return cur;
    }
    next = cur->right;
    if (!next->is_nil) {
        guard = CAP_BDD0_MAP_MAX;
        while (!next->left->is_nil) {
            if (--guard < 0) {
                return NULL;                 /* кап: спуск к левому краю не сошёлся */
            }
            next = next->left;
        }
        return next;
    }
    next = cur->parent;
    guard = CAP_BDD0_MAP_MAX;
    while (!next->is_nil) {
        if (cur != next->right) {
            break;
        }
        if (--guard < 0) {
            return NULL;                     /* кап: подъём по parent не сошёлся */
        }
        cur = next;
        next = next->parent;
    }
    return next;
}

/* v16 (аддитивно): обход одной RB-карты PathFinder от head-сентинела. Паттерн
 * cap_read_map1: root = head->left (== head у пустой), cap_tree_next_bounded
 * (кэп-вариант s6_tree_next, порт 0x47f800), стоп на head/NULL/капе
 * CAP_BDD0_MAP_MAX. NULL = кап внутреннего цикла (не натуральный конец) → trap=1,
 * собранные узлы сохраняются. Смещения голов подтверж-
 * дены дизасмом: 0x45ca70 (clear) читает [ebx+0x6c]/[ebx+0x78], 0x45dd00
 * (вставка) — map-объекты +0x68/+0x74 (голова = объект+4), 0x45ce80 — [ecx+0x6c].
 * Узел с is_nil не считается данными (сентинел в nodes не пишется) — обход
 * стопается. Весь обход под __try: fault → trap=1 и останов, собранные узлы
 * сохраняются; узел, на котором упали, не committed (count++ после обоих read).
 * head==0 → n=0, узлы не фабрикуются. */
static void cap_read_map(unsigned int head)
{
    TreeNode *h = (TreeNode *)(size_t)head;
    TreeNode *cur;

    g_map_cnt = 0;
    g_map_trunc = 0;
    g_map_fault = 0;
    if (head == 0) {
        return;
    }
    __try {
        cur = h->left;                              /* root (== head если пусто) */
        while (cur != NULL && cur != h && g_map_cnt < CAP_BDD0_MAP_MAX) {
            unsigned int va = (unsigned int)(size_t)(void *)cur;
            unsigned char *v;
            if (cur->is_nil) {                      /* сентинел/битый — не данные */
                break;
            }
            v = (unsigned char *)cur->value;
            memcpy(g_map_node[g_map_cnt], cur, 0x18);
            g_map_addr[g_map_cnt] = va;
            if (v != NULL) {
                memcpy(g_map_ps[g_map_cnt], v, 0x58);
                g_map_ps_ok[g_map_cnt] = 1;
            } else {
                g_map_ps_ok[g_map_cnt] = 0;
            }
            g_map_cnt++;                            /* commit после обоих read */
            cur = cap_tree_next_bounded(cur, h);
            if (cur == NULL) {                      /* кап внутреннего цикла: обход не сошёлся */
                g_map_fault = 1;                    /* trap=1; собранные узлы сохранены */
                break;
            }
        }
        if (cur != NULL && cur != h && g_map_cnt >= CAP_BDD0_MAP_MAX) {
            g_map_trunc = 1;                        /* кап остановил обход */
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_map_fault = 1;                            /* узел-виновник не committed */
    }
}

/* v16: одна карта в JSON — {"head":u32,"n":int,"trunc":0/1,"trap":0/1,
 * "nodes":[{"a":u32,"n":<b64 0x18>,"ps":<b64 0x58>|null}, ...]}. */
static void cap_write_map(FILE *f, unsigned int head)
{
    int i;

    fprintf(f, "{\"head\":%u,\"n\":%d,\"trunc\":%d,\"trap\":%d,\"nodes\":[",
            head, g_map_cnt, g_map_trunc, g_map_fault);
    for (i = 0; i < g_map_cnt; i++) {
        fprintf(f, "%s{\"a\":%u,\"n\":\"", i ? "," : "", g_map_addr[i]);
        cap_b64_encode(g_b64, g_map_node[i], 0x18);
        fputs(g_b64, f);
        fputs("\",\"ps\":", f);
        if (g_map_ps_ok[i]) {
            fputc('"', f);
            cap_b64_encode(g_b64, g_map_ps[i], 0x58);
            fputs(g_b64, f);
            fputc('"', f);
        } else {
            fputs("null", f);
        }
        fputc('}', f);
    }
    fputs("]}", f);
}

/* v16 (аддитивно): состояние листа 0x45e1d0 (PathFinder/record/sub/ctx/bandrec)
 * для bdd0_leaf_pre (до вызова) и bdd0_leaf_post (после). Каждое разыменование
 * под __try; сбой помечает null только свой blob, указатели пишутся всегда
 * (нужны загрузчику для привязки). ecx==0 → цепочка pf/rec/sub/ctx обнулена;
 * bandrec = args[1] (от ecx не зависит). Без malloc, буферы — на стеке. */
static void cap_write_leaf_state(FILE *f, int ecx, const int *args, int nargs)
{
    unsigned char pf[0x78];
    unsigned char recb[0x1c];
    unsigned char subb[0x10];
    unsigned char bandb[0x24];
    unsigned int rec = 0, sub = 0, ctx = 0, bandrec = 0;
    unsigned int ctx120 = 0, ctx134 = 0;
    unsigned int head1 = 0, head2 = 0;       /* v16: головы map1 +0x6c / map2 +0x78 */
    int pf_ok = 0, rec_ok = 0, sub_ok = 0, band_ok = 0, ctx_ok = 0;

    if (args != NULL && nargs > 1) {
        bandrec = (unsigned int)args[1];
    }
    __try {
        if (ecx != 0) {
            memcpy(pf, (const void *)(size_t)ecx, 0x78);
            pf_ok = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        pf_ok = 0;
    }
    __try {
        if (ecx != 0) {
            rec = *(const unsigned int *)(size_t)ecx;
            if (rec != 0) {
                memcpy(recb, (const void *)(size_t)rec, 0x1c);
                rec_ok = 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rec_ok = 0;                      /* rec (указатель) сохраняется */
    }
    __try {
        if (rec != 0) {
            sub = *(const unsigned int *)(size_t)(rec + 0x0c);
            if (sub != 0) {
                memcpy(subb, (const void *)(size_t)sub, 0x10);
                sub_ok = 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sub_ok = 0;                      /* sub (указатель) сохраняется */
    }
    __try {
        if (rec != 0) {
            ctx = *(const unsigned int *)(size_t)(rec + 0x04);
            if (ctx != 0) {
                ctx120 = *(const unsigned int *)(size_t)(ctx + 0x120);
                ctx134 = *(const unsigned int *)(size_t)(ctx + 0x134);
                ctx_ok = 1;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctx_ok = 0;                      /* ctx (указатель) сохраняется */
    }
    __try {
        if (bandrec != 0) {
            memcpy(bandb, (const void *)(size_t)bandrec, 0x24);
            band_ok = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        band_ok = 0;
    }
    fputs(",\"pf\":", f);
    cap_write_b64_field(f, pf_ok, pf, 0x78);
    fprintf(f, ",\"rec\":%u", rec);
    fputs(",\"rec_b64\":", f);
    cap_write_b64_field(f, rec_ok, recb, 0x1c);
    fprintf(f, ",\"sub\":%u", sub);
    fputs(",\"sub_b64\":", f);
    cap_write_b64_field(f, sub_ok, subb, 0x10);
    fprintf(f, ",\"ctx\":%u", ctx);
    fputs(",\"ctx120\":", f);
    if (ctx_ok) fprintf(f, "%u", ctx120); else fputs("null", f);
    fputs(",\"ctx134\":", f);
    if (ctx_ok) fprintf(f, "%u", ctx134); else fputs("null", f);
    fprintf(f, ",\"bandrec\":%u", bandrec);
    fputs(",\"bandrec_b64\":", f);
    cap_write_b64_field(f, band_ok, bandb, 0x24);
    /* v16 (аддитивно): обе RB-карты PathFinder — map1 head +0x6c, map2 head
     * +0x78 (0x45ca70/0x45dd00/0x45ce80). head=0/n=0 при NULL-голове или
     * нечитаемом ecx; узлы не фабрикуются. Порядок ключей одинаков в pre/post. */
    __try {
        if (ecx != 0) {
            head1 = *(const unsigned int *)(size_t)(ecx + 0x6c);
            head2 = *(const unsigned int *)(size_t)(ecx + 0x78);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        head1 = 0;
        head2 = 0;
    }
    fputs(",\"maps\":{\"m1\":", f);
    cap_read_map(head1);
    cap_write_map(f, head1);
    fputs(",\"m2\":", f);
    cap_read_map(head2);
    cap_write_map(f, head2);
    fputc('}', f);
}

/* v16: bdd0_leaf_pre/bdd0_leaf_post — тот же заголовок, что bdd0_leaf (v, kind,
 * seq, order, site, va, ecx, args) + состояние (cap_write_leaf_state). site/va —
 * hex-строки (как в bdd0_leaf). Один ключевой набор в обоих родах.
 * v17: тот же writer пишет аддитивный bdd0_site_pf (сайт 0x45e320, выход 45c8c0)
 * — новое только имя kind. */
static void cap_write_bdd0_leaf_state_rec(const char *kind, int seq, int order,
                                          int site, int va, int ecx,
                                          const int *args, int nargs)
{
    int k;

    if (g_file == NULL) {
        return;
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"%s\",\"seq\":%d,\"order\":%d,"
            "\"site\":\"%x\",\"va\":\"%x\",\"ecx\":%d,\"args\":[",
            CAP_BDD0_SNAP_VER, kind, seq, order, site, va, ecx);
    for (k = 0; k < nargs; k++) {
        if (args != NULL) {
            fprintf(g_file, "%s%d", k ? "," : "", args[k]);
        }
    }
    fputc(']', g_file);
    cap_write_leaf_state(g_file, ecx, args, nargs);
    fputs("}\n", g_file);
}

/* v18 (аддитивно): bdd0_cand — состояние объекта, ВОЗВРАЩЁННОГО 45ce80
 * (PathState* или 0), снятое прямо на выходе сканирующего сайта 0x45e443 /
 * 0x45e487. Порту нужен именно этот момент: 0x58 Б PathState по адресу в
 * `ret`, а не post-состояние всего листа. Заголовок — как у bdd0_leaf; ps_b64 —
 * base64 0x58 Б, null когда указатель 0 или память нечитаема (под __try). Одна
 * запись на вызов скана. */
static void cap_write_bdd0_cand(int seq, int order, int site, int va,
                                int ecx, const int *args, int nargs, int cand)
{
    unsigned char ps[0x58];
    int ps_ok = 0, k;

    if (g_file == NULL) {
        return;
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_cand\",\"seq\":%d,\"order\":%d,"
            "\"site\":\"%x\",\"va\":\"%x\",\"ecx\":%d,\"args\":[",
            CAP_BDD0_SNAP_VER, seq, order, site, va, ecx);
    for (k = 0; k < nargs; k++) {
        if (args != NULL) {
            fprintf(g_file, "%s%d", k ? "," : "", args[k]);
        }
    }
    fprintf(g_file, "],\"cand\":%d,\"ps_b64\":", cand);
    if (cand != 0) {
        __try {
            memcpy(ps, (const void *)(size_t)(unsigned)cand, 0x58);
            ps_ok = 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ps_ok = 0;
        }
    }
    cap_write_b64_field(g_file, ps_ok, ps, 0x58);
    fputs("}\n", g_file);
}

static int cap_leaf_sink_enter(int site, int va, int ecx, const int *args,
                               int nargs)
{
    bdd0_frame *fr;

    (void)va;
    (void)ecx;
    (void)args;
    (void)nargs;
    if (!g_snap || !g_enabled || g_closed || g_file == NULL || g_bdd0_sp <= 0) {
        return -1;
    }
    if (g_leaf_q_n >= BDD0_STACK_MAX) {
        return -1;
    }
    fr = &g_bdd0_stack[g_bdd0_sp - 1];
    g_leaf_q_seq[g_leaf_q_n] = fr->seq;
    g_leaf_q_order[g_leaf_q_n] = fr->call_order++;
    g_leaf_q_site[g_leaf_q_n] = site;
    /* v16: pre-состояние только листа 0x45e1d0 (guard по va, не по сайту) —
     * тем же order/seq, что понесёт bdd0_leaf этого вызова. */
    if (va == 0x45e1d0) {
        cap_write_bdd0_leaf_state_rec("bdd0_leaf_pre", g_leaf_q_seq[g_leaf_q_n],
                                      g_leaf_q_order[g_leaf_q_n], site, va, ecx,
                                      args, nargs);
    }
    return g_leaf_q_n++;
}

static void cap_leaf_sink_exit(int token, int va, int ecx, const int *args,
                               int nargs, int ret_bits, const int *w, int nw,
                               int trap)
{
    int seq, order, site;

    if (token < 0 || token >= g_leaf_q_n) {
        return;
    }
    if (token != g_leaf_q_n - 1) {
        trap = 1;                        /* рассинхрон LIFO — помечаем запись */
    }
    g_leaf_q_n = token;
    seq = g_leaf_q_seq[token];
    order = g_leaf_q_order[token];
    site = g_leaf_q_site[token];
    battle_capture_bdd0_leaf_rec(site, va, (const void *)(size_t)ecx, args,
                                 nargs, ret_bits, w, nw, seq, order, trap);
    /* v18 (аддитивно): кандидат-снапшот только у сканирующих сайтов 0x45e443/
     * 0x45e487 — EAX 45ce80 и есть PathState*, чьё состояние нужно порту в
     * этот момент. При trap (рассинхрон LIFO/сбой) ret не достоверен — пропуск. */
    if (!trap && (site == 0x45e443 || site == 0x45e487)) {
        cap_write_bdd0_cand(seq, order, site, va, ecx, args, nargs, ret_bits);
    }
    /* v16: post-состояние сразу за bdd0_leaf, тем же seq/order/site/va и
     * ecx/args из exit; не пишется при trap (в т.ч. рассинхрон LIFO) — token
     * уже проверен выше. Только лист 0x45e1d0 (guard по va). */
    if (va == 0x45e1d0 && !trap) {
        cap_write_bdd0_leaf_state_rec("bdd0_leaf_post", seq, order, site, va,
                                      ecx, args, nargs);
    }
    /* v17: site-pf — состояние, которое лист 0x45e1d0 читает ПОСЛЕ 45c8c0, но
     * которое офлайн не выводится: 45c8c0 пишет у приёмника-объекта PathFinder
     * (тот же, что порт листа зовёт `pf`) поля +0x08..+0x27 — радиусы, порог,
     * RECT (x87 `FPTAN`), — а их читает 45c690 (@0x45e3b4, PtInRect по RECT
     * +0x18..+0x27). Сайт 0x45e320 — единственный такой (план порта
     * листа 0x45e1d0, §5); blob тот же, что у pre/post
     * листа (pf 0x78 + rec/sub/ctx/bandrec + карты). ecx берётся из LIFO на
     * входе сайта (`e->ecx` в cap_leaf_finish), а это ровно приёмник 45c8c0:
     * `mov ecx,esi` @0x45e31b перед `call` — тот же объект, что порт листа
     * зовёт `pf` (карты +0x6c/+0x78, запись rec в +0x00). Остальные 19
     * внутренних сайтов пишут состояние только через свои callee'ы (например
     * 45c330 — поля pf+0x04..+0x38, 45ca70 — карты) — его выводит порт из
     * собственного кода по этим записям, поэтому обычная leaf-запись. */
    if (site == 0x45e320 && va == 0x45c8c0 && !trap) {
        cap_write_bdd0_leaf_state_rec("bdd0_site_pf", seq, order, site, va,
                                      ecx, args, nargs);
    }
}

/* Пропущенный сайт (не `call rel32` / чужой rel32 / нет адреса) — маркер в
 * захвате: харнесс видит пробел в потоке листьев, а не молчаливое отсутствие. */
static void cap_leaf_sink_missing(int site_va, int target_va, const char *why)
{
    if (!g_enabled || g_closed || g_file == NULL) {
        return;
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"leaf_missing\",\"site\":\"%x\","
            "\"va\":\"%x\",\"why\":\"%s\"}\n",
            CAP_BDD0_SNAP_VER, site_va, target_va,
            (why != NULL) ? why : "");
}

static const leaf_sink g_leaf_sink = {
    cap_leaf_sink_enter, cap_leaf_sink_exit, cap_leaf_sink_missing
};

/* ------------------- v12: bdd0_native (не-листовые вызовы) --------------- */
static const char *ns_kind_name(int kind)
{
    switch (kind) {
    case NS_K_AL:   return "al";
    case NS_K_F32:  return "f32";
    case NS_K_VOID: return "void";
    default:        return "int";          /* INT / PTR / PTR_W3 */
    }
}

static const char *ns_slot_name(int slot)
{
    switch (slot) {
    case NS_SLOT_U28: return "u+0x28";
    case NS_SLOT_U2C: return "u+0x2c";
    case NS_SLOT_U38: return "u+0x38";
    case NS_SLOT_U40: return "u+0x40";
    default:          return NULL;
    }
}

void battle_capture_bdd0_native_rec(int site, int target, int slot, int kind,
                                    int ecx, const int *args, int nargs,
                                    int ret, const int *w, int nw,
                                    const int *ww, int nww,
                                    int seq, int order, int trap)
{
    const char *slot_name = ns_slot_name(slot);
    int k;

    if (g_file == NULL) return;
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_native\",\"seq\":%d,\"order\":%d,"
            "\"site\":\"%x\",\"va\":\"%x\",",
            CAP_BDD0_SNAP_VER, seq, order, site, target);
    if (slot_name != NULL) {
        fprintf(g_file, "\"slot\":\"%s\",", slot_name);
    }
    fprintf(g_file, "\"ecx\":%d,\"args\":[", ecx);
    for (k = 0; k < nargs; k++) {
        fprintf(g_file, "%s%d", k ? "," : "", (args != NULL) ? args[k] : 0);
    }
    fprintf(g_file, "],\"ret\":%d,\"ret_kind\":\"%s\"", ret, ns_kind_name(kind));
    if (w != NULL && nw > 0) {
        fputs(",\"w\":[", g_file);
        for (k = 0; k < nw; k++) {
            fprintf(g_file, "%s%d", k ? "," : "", w[k]);
        }
        fputc(']', g_file);
    }
    /* v12 additive: post-call state writes as [unit_off, value] pairs. */
    if (ww != NULL && nww > 0) {
        fputs(",\"ww\":[", g_file);
        for (k = 0; k < nww; k++) {
            fprintf(g_file, "%s%d", k ? "," : "", ww[k]);
        }
        fputc(']', g_file);
    }
    fprintf(g_file, ",\"trap\":%d}\n", trap);
}

void battle_capture_native_skipped(int site, int target, const char *why)
{
    if (!g_enabled || g_closed || g_file == NULL) return;
    fprintf(g_file, "{\"v\":%d,\"kind\":\"native_site_skipped\",\"site\":\"%x\","
            "\"va\":\"%x\",\"why\":\"%s\"}\n",
            CAP_BDD0_SNAP_VER, site, target, (why != NULL) ? why : "");
}

#define NS_Q_MAX BDD0_STACK_MAX
typedef struct {
    int seq, order, site, target, slot, kind, ecx, nargs;
    int args[8];
} ns_q_ent;
static ns_q_ent g_ns_q[NS_Q_MAX];
static int g_ns_q_n = 0;

static int cap_native_sink_enter(int site, int target, int slot, int kind,
                                 int ecx, const int *args, int nargs)
{
    bdd0_frame *fr;
    ns_q_ent *q;
    int i;

    if (!g_snap || !g_enabled || g_closed || g_file == NULL || g_bdd0_sp <= 0) {
        return -1;
    }
    if (g_ns_q_n >= NS_Q_MAX) return -1;
    fr = &g_bdd0_stack[g_bdd0_sp - 1];
    q = &g_ns_q[g_ns_q_n];
    q->seq = fr->seq;
    q->order = fr->call_order++;
    q->site = site;
    q->target = target;
    q->slot = slot;
    q->kind = kind;
    q->ecx = ecx;
    q->nargs = (nargs > 0 && nargs <= 8) ? nargs : 0;
    for (i = 0; i < 8; i++) q->args[i] = (args != NULL && i < q->nargs)
                                        ? args[i] : 0;
    return g_ns_q_n++;
}

/* v13 additive: нативы, пишущие THROUGH свой указательный аргумент. Ключ
 * записи = -(1+off) (отрицательный — отличает от unit-смещений 45eee0).
 * Порт replay-ит эти значения в свой out-буфер, иначе все потребители тройки
 * (grid_query @0x44c247/0x44c276) получают нули. */
static const struct { int site; int target; int arg; int n; } k_out_writes[] = {
    /* 0x44c240 → 42d9c0: пишет позицию игрока в [arg0..arg0+8] (тройка float). */
    { 0x44c240u, 0x42d9c0u, 0, 3 }
};

static void cap_native_out_writes(const ns_q_ent *q, int *ww, int *nww, int *trap)
{
    int i, k;
    for (i = 0; i < (int)(sizeof(k_out_writes) / sizeof(k_out_writes[0])); i++) {
        int p;
        if (q->site != (int)k_out_writes[i].site
            || q->target != (int)k_out_writes[i].target) {
            continue;
        }
        p = (k_out_writes[i].arg < q->nargs) ? q->args[k_out_writes[i].arg] : 0;
        if (p == 0) continue;
        for (k = 0; k < k_out_writes[i].n; k++) {
            int v = 0;
            __try {
                v = *(const int *)(p + 4 * k);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *trap = 1;
                continue;
            }
            if (*nww + 2 > 32) break;
            ww[(*nww)++] = -(1 + k);
            ww[(*nww)++] = v;
        }
    }
}

static void cap_native_sink_exit(int token, int site, int target, int slot,
                                 int kind, int ecx, const int *args, int nargs,
                                 int ret_bits, const int *w, int nw, int trap)
{
    ns_q_ent q;
    int ww[32];
    int nww = 0;

    if (token < 0 || token >= g_ns_q_n) return;
    if (token != g_ns_q_n - 1) trap = 1;      /* LIFO рассинхрон */
    q = g_ns_q[token];
    g_ns_q_n = token;
    (void)site; (void)target; (void)slot; (void)kind; (void)ecx;
    (void)args; (void)nargs;
    cap_native_out_writes(&q, ww, &nww, &trap);
    battle_capture_bdd0_native_rec(q.site, q.target, q.slot, q.kind, q.ecx,
                                   q.args, q.nargs, ret_bits, w, nw,
                                   (nww > 0) ? ww : NULL, nww,
                                   q.seq, q.order, trap);
}

static void cap_native_sink_missing(int site, int target, const char *why)
{
    battle_capture_native_skipped(site, target, why);
}

static const native_sink g_native_sink = {
    cap_native_sink_enter, cap_native_sink_exit, cap_native_sink_missing
};

static void cap_leaf_sink_register(void)
{
    leaf_sites_set_sink(&g_leaf_sink);
    native_sites_set_sink(&g_native_sink);
}

int battle_capture_snap_enabled(void)
{
    return g_snap;
}

/* ------------------- v10/v11: per-call FUN_0044bdd0 (диспетчер AI) -------- *
 * Пара `bdd0_cap` (H1 @0x44bdf5, ECX = unit) / `bdd0_res` (H2 @0x44d074)
 * на каждый вызов верхнего диспетчера боевого AI; связывание — по `seq`
 * (независимый g_bdd0_seq). Аргументы вызывающего лежат в кадре 44bdd0
 * (esp = E0-0x158 на обоих хуках), читаются по CAP_BDD0_P*. Предел — свой
 * (g_max_calls), файл закрывает только e0a0. Все разыменования под __try.
 * v11: res дополнительно снимает слоты кадра (CAP_BDD0_ANC, TAG, TRI0..TRI2,
 * A4). */
void battle_capture_bdd0_enter(void *self, void *esp)
{
    int seq;
    int p1 = -1, p2 = -1, p3 = 0;
    int ai_ctx = 0, u170 = -1;   /* ai_ctx = *(unit+0x138) — game_ctx, не BattleAI */
    unsigned int gate114 = 0;
    int trap = 0;

    if (!g_enabled || g_closed || g_file == NULL) {
        return;
    }
    if (g_bdd0_seq >= g_max_calls || g_bdd0_sp >= BDD0_STACK_MAX) {
        return;                     /* стек полон — вызова в паре не будет */
    }
    __try {
        if (esp != NULL) {
            p1 = *(const int *)((const unsigned char *)esp + CAP_BDD0_P1);
            p2 = *(const int *)((const unsigned char *)esp + CAP_BDD0_P2);
            p3 = (int)(size_t)*(void * const *)((const unsigned char *)esp +
                                                CAP_BDD0_P3);
        }
        if (self != NULL) {
            /* Первый dword встроенного BattleAI — game_ctx (поле v11-cap `ai`). */
            ai_ctx = *(const int *)((const unsigned char *)self + 0x138);
            u170 = *(const int *)((const unsigned char *)self + 0x170);
            /* +0x114 — float, который входной гейт сравнивает с [0x4ec37c]
             * (`flds 0x114(%ebp)` @0x44bdf9); пишем биты. */
            gate114 = *(const unsigned int *)((const unsigned char *)self +
                                              0x114);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        p1 = -1; p2 = -1; p3 = 0; ai_ctx = 0; u170 = -1; gate114 = 0;
    }

    seq = ++g_bdd0_seq;
    g_bdd0_stack[g_bdd0_sp].seq = seq;
    g_bdd0_stack[g_bdd0_sp].self = self;
    g_bdd0_stack[g_bdd0_sp].call_order = 0;  /* v12: общий счётчик leaf+native */
    g_bdd0_sp++;
    /* v12: новый вызов 44bdd0 — LIFO листьев прошлого вызова не должен
     * переноситься (несбалансированный вход/выход после SEH-размотки). */
    if (g_bdd0_sp == 1) {
        g_leaf_q_n = 0;
        g_ns_q_n = 0;
        leaf_sites_reset();
        native_sites_reset();
    }
    /* v12: косвенные вызовы тела идут через vtable unit'а; ставим слот-хуки
     * один раз на vtable (идемпотентно). */
    if (g_snap && self != NULL) {
        char nswhy[128];
        nswhy[0] = '\0';
        native_sites_vtable_arm(self, nswhy, (int)sizeof(nswhy));
    }
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_cap\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"44bdd0\",\"self\":%d,\"p1\":%d,\"p2\":%d,"
            "\"p3\":%d,\"ai\":%d,\"u170\":%d,\"gate114\":%u,\"trap\":%d}\n",
            CAP_BDD0_VER, seq, g_tick, g_unit, (int)(size_t)self, p1, p2, p3,
            ai_ctx, u170, gate114, trap);
    if (g_snap) {
        cap_write_bdd0_in(seq, self, esp, p1, p2, p3, ai_ctx, u170, gate114);
        /* v16 (аддитивно): полный снимок сетки (kind "grid") из bdd0-пути —
         * 458050 нельзя сверить офлайн, пока слой e0a0 выключен. BattleAI
         * встроен в unit по +0x138; g_grid_written — ровно один раз за захват
         * (как в e0a0-пути). Все чтения — под __try в cap_write_grid_record.
         * ВАЖНО: первые вызовы 44bdd0 в бою принадлежат юнитам, у которых
         * встроенный BattleAI ещё с game_ctx == 0 (сетки нет). Одноразовый флаг
         * g_grid_written нельзя расходовать на них — иначе реальная запись не
         * пишется за весь захват. Поэтому флаг ставим и пишем ТОЛЬКО когда
         * предикат cap_grid_record_would_write подтвердил валидную сетку;
         * иначе не делаем ничего (ни error-записи, ни расхода флага). */
        if (self != NULL && !g_grid_written) {
            BattleAI *ai = (BattleAI *)((const unsigned char *)self +
                                        CAP_BDD0_AI_OFF);
            if (cap_grid_record_would_write(ai)) {
                g_grid_written = 1;
                cap_write_grid_record(ai);
            }
        }
    }
}

void battle_capture_bdd0_exit(int al, void *esp)
{
    void *p3 = NULL;
    int out = -1;
    int u170 = -1;
    unsigned int m0 = 0xFFFFFFFFu, m1 = 0xFFFFFFFFu, m2 = 0xFFFFFFFFu;
    int anc = 0, a4n = 0;
    unsigned int tag = 0xFFFFFFFFu, tri0 = 0xFFFFFFFFu, tri1 = 0xFFFFFFFFu,
                 tri2 = 0xFFFFFFFFu;
    bdd0_frame fr;
    int seq, trap = 0;

    if (!g_enabled || g_closed || g_file == NULL || g_bdd0_sp <= 0) {
        return;
    }
    /* Снимаем вершину: она принадлежит текущему (внутреннему) вызову. */
    fr = g_bdd0_stack[g_bdd0_sp - 1];
    g_bdd0_sp--;
    seq = fr.seq;
    __try {
        if (esp != NULL) {
            p3 = *(void * const *)((const unsigned char *)esp + CAP_BDD0_P3);
        }
        if (p3 != NULL) {
            out = *(const int *)p3;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        out = -1;
    }
    __try {
        if (fr.self != NULL) {
            u170 = *(const int *)((const unsigned char *)fr.self + 0x170);
            m0 = *(const unsigned int *)((const unsigned char *)fr.self + 0xa4);
            m1 = *(const unsigned int *)((const unsigned char *)fr.self + 0xa8);
            m2 = *(const unsigned int *)((const unsigned char *)fr.self + 0xac);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        u170 = -1;
        m0 = m1 = m2 = 0xFFFFFFFFu;
    }
    /* v11: слоты кадра на эпилоге (эпилог их не трогает до H2): anc — итог
     * 458050, tag — grid-record, tri* — pos-тройка, a4n — arg4 458050. */
    __try {
        if (esp != NULL) {
            const unsigned char *frame = (const unsigned char *)esp;
            anc = (*(const void *const *)(frame + CAP_BDD0_ANC) != NULL) ? 1 : 0;
            a4n = (*(const void *const *)(frame + CAP_BDD0_A4) != NULL) ? 1 : 0;
            tag = *(const unsigned int *)(frame + CAP_BDD0_TAG);
            tri0 = *(const unsigned int *)(frame + CAP_BDD0_TRI0);
            tri1 = *(const unsigned int *)(frame + CAP_BDD0_TRI1);
            tri2 = *(const unsigned int *)(frame + CAP_BDD0_TRI2);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        anc = -1; a4n = -1;
        tag = tri0 = tri1 = tri2 = 0xFFFFFFFFu;
    }
    /* v13 (аддитивно): после-состояние ленты (begin,end) от того же self, что
     * и ai-снимок — 45eee0 перестроил её внутри тика, и офлайн-харнесс должен
     * восстановить её точно, а не реконструировать длину из 45af50. */
    {
        unsigned int band_begin = 0, band_end = 0;
        int band_ok = 0;
        cap_read_band((BattleAI *)fr.self, &band_begin, &band_end, &band_ok);
        fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_res\",\"seq\":%d,\"tick\":%d,"
                "\"unit\":%d,\"fn\":\"44bdd0\",\"al\":%d,\"u170\":%d,\"out\":%d,"
                "\"m0\":%u,\"m1\":%u,\"m2\":%u,\"anc\":%d,\"tag\":%u,"
                "\"tri0\":%u,\"tri1\":%u,\"tri2\":%u,\"a4n\":%d,"
                "\"band\":[%u,%u],\"trap\":%d}\n",
                CAP_BDD0_VER, seq, g_tick, g_unit, al, u170, out, m0, m1, m2,
                anc, tag, tri0, tri1, tri2, a4n, band_begin, band_end, trap);
    }
    /* flush на конце тика: буферизация 1 МиБ, краш теряет последний тик */
    fflush(g_file);
}

void battle_capture_leaf(int kind, int value, const int *words)
{
    if (!g_e0) {
        return;
    }
    if (!g_enabled || g_closed) {
        return;
    }
    if (kind == CAP_LEAF_PATH) {
        int leaf_index = g_path_n / CAP_WORDS;
        if (g_path_n + CAP_WORDS > CAP_MAX_LEAF * CAP_WORDS) {
            g_leaf_overflow = 1;
            return;
        }
        /* v7: масштаб на входе этого cb40-вызова (снят в cap_fn_cb40_hook). */
        if (leaf_index < CAP_MAX_LEAF) {
            g_leaf_scale[leaf_index] = (int)g_cb40_scale;
            g_leaf_scale_ok[leaf_index] = g_cb40_scale_ok;
        }
        /* v6: входы финального гейта этого cb40-вызова, сырые биты:
         * [0]=out_metric (spec+0x10, prologue arg1), [1]=final_threshold
         * (BattleAI+0x8), [2]=мин L[0x10], [3]=макс L[0x18]. */
        {
            int *gv = &g_gate[leaf_index * CAP_GATE_WORDS];
            gv[0] = 0; gv[1] = 0;
            gv[2] = (int)g_gate_l10;
            gv[3] = (int)g_gate_l18;
            __try {
                if (g_cap_a1 != NULL) {
                    gv[0] = *(const int *)((const unsigned char *)g_cap_a1 + 0x10);
                }
                if (g_this != NULL) {
                    gv[1] = *(const int *)((const unsigned char *)g_this + 0x8);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        if (value == 0 || words == NULL) {
            g_pptr[leaf_index] = 0;             /* NULL-маркер */
            g_path[g_path_n++] = 0x7fffffff;
            g_path_n += CAP_WORDS - 1;
            return;
        }
        g_pptr[leaf_index] = value;             /* LDPos* — ключ/identity */
        __try {
            memcpy(&g_path[g_path_n], words, CAP_WORDS * sizeof(int));
            g_path_n += CAP_WORDS;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_pptr[leaf_index] = 0;
            g_path[g_path_n++] = 0x7fffffff;
            g_path_n += CAP_WORDS - 1;
        }
    } else if (kind == CAP_LEAF_HEUR) {
        if (g_heur_n < CAP_MAX_LEAF) {
            g_heur[g_heur_n++] = value;
        } else {
            g_leaf_overflow = 1;
        }
    } else if (kind == CAP_LEAF_TIMEOUT) {
        if (g_tout_n < CAP_MAX_LEAF) {
            g_tout[g_tout_n++] = value;
        } else {
            g_leaf_overflow = 1;
        }
    }
}

void battle_capture_e0a0_exit(void *node)
{
    if (!g_e0) {
        return;
    }
    BattleAI *self = (BattleAI *)g_this;
    int code = -1, iters, node_x = -1, node_y = -1;

    if (!g_enabled || g_closed || self == NULL) {
        return;
    }
    __try {
        code = (g_out_code != NULL) ? *((const int *)g_out_code) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        code = -1;
    }
    if (node != NULL) {
        cap_pos np;
        if (cap_read_cand_pos(node, &np)) {
            node_x = np.x; node_y = np.y;
        }
    }
    /* iters = число кандидатов. H6-латч = rescan; сработавший gate (node !=
     * NULL) означает, что последний кандидат в iter_seq не попал. */
    iters = g_iter_n + ((node != NULL) ? 1 : 0);
    fprintf(g_file, "{\"v\":%d,\"kind\":\"res\",\"seq\":%d,\"node\":",
            CAP_VERSION, g_seq);
    if (node != NULL) {
        fprintf(g_file, "[%d,%d]", node_x, node_y);
    } else {
        fputs("null", g_file);
    }
    fprintf(g_file, ",\"code\":%d,\"iters\":%d,\"iter_seq\":", code, iters);
    cap_write_iter(g_file);
    fprintf(g_file, ",\"iter_cap\":%d,\"iter_overflow\":%d", g_iter_cap,
            (g_iter_n >= g_iter_cap) ? 1 : 0);
    /* map1_after/occ_after — состояние после вызова (источник новых узлов). */
    cap_read_map1(self);
    cap_read_occ(self);
    fputs(",\"map1_after\":", g_file);
    cap_write_map1(g_file);
    fputs(",\"occ_after\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    /* v4: скалярный блок BattleAI ПОСЛЕ вызова (тот же снимок, что и другие
     * after-поля). Харнесс сравнивает его с репликой; старые v1/v2/v3-файлы
     * ключа не имеют — харнесс пропускает сравнение. */
    cap_read_ai_words(self);
    fputs(",\"ai_words_after\":", g_file);
    cap_write_ai_words(g_file);
    /* Поток листьев нативных калlee (перепроецируется харнессом). */
    fputs(",\"leaf\":{\"path\":", g_file);
    cap_write_path(g_file);
    fputs(",\"gate\":", g_file);
    cap_write_gate(g_file);
    fputs(",\"ptr\":", g_file);
    cap_write_ints(g_file, g_pptr, g_path_n / CAP_WORDS);
    fputs(",\"heur\":", g_file);
    cap_write_ints(g_file, g_heur, g_heur_n);
    fputs(",\"timeout\":", g_file);
    cap_write_ints(g_file, g_tout, g_tout_n);
    fputs(",\"timer\":", g_file);
    cap_write_ints(g_file, g_tms, g_tout_n);
    cap_write_scale(g_file, g_path_n / CAP_WORDS);
    cap_write_chain(g_file, g_path_n / CAP_WORDS);   /* v8: состояние цепочки на входе */
    fprintf(g_file, ",\"overflow\":%d,\"chain_overflow\":%d},\"trap\":%d}\n",
            g_leaf_overflow, g_chain_overflow, g_map_trap);
}

/* ----------------------------- трамплины листьев ------------------------ */
/* Хук на ВХОДЕ функции (не на call-site): подменяем адрес возврата на стеке,
 * выполняем украденные байты пролога и прыгаем на оригинал. Возврат original
 * (RET 8/4) попадает в cap_ret_*, который логирует EAX и прыгает на настоящий
 * адрес возврата. Никаких манипуляций аргументами — только ESP/ECX как есть. */

/* v7: масштаб world[0x120] на входе cb40 — цепочка native 0x45c42b..0x45c430
 * (`mov (%ebx),%eax; mov 0x4(%eax),%ecx; flds 0x120(%ecx)`), ebx = self.
 * На входе cb40 ECX = this (пролог `mov %ecx,%esi` @0x45cb47). */
void __cdecl cap_read_cb40_scale(void *self)
{
    g_cb40_scale = 0;
    g_cb40_scale_ok = 0;
    __try {
        unsigned char *ctx = (unsigned char *)self;
        unsigned char *w0 = (ctx != NULL) ? *(unsigned char **)ctx : NULL;
        unsigned char *world = (w0 != NULL) ? *(unsigned char **)(w0 + 4) : NULL;
        if (world != NULL) {
            g_cb40_scale = *(const unsigned int *)(world + 0x120);
            g_cb40_scale_ok = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_cb40_scale = 0;
        g_cb40_scale_ok = 0;
    }
    cap_snapshot_chain(self);   /* v8: состояние цепочки на входе cb40 */
}

static void *g_cont_cb40 = NULL, *g_cont_heur = NULL, *g_cont_timeout = NULL;
static void *g_ret_cb40 = NULL, *g_ret_heur = NULL, *g_ret_timeout = NULL;
/* IAT-слот timeGetTime EXE (VA 0x4ec27c): украденный `call *0x4ec27c` из
 * пролога c5a0 (MSVC inline asm не принимает литеральный адрес в call). */
static void *g_iat_timer = (void *)(size_t)S6_VA_timeGetTime;

/* Определения ниже (naked); объявления нужны для `offset` в трамплинах. */
void cap_ret_cb40(void);
void cap_ret_heur(void);
void cap_ret_timeout(void);
void __cdecl cap_impl_path(int value);
void __cdecl cap_impl_heur(int value);
void __cdecl cap_impl_timeout(int value);

void battle_capture_set_leaf_fns(void *cb40, void *heur, void *timeout)
{
    g_fn_cb40 = cb40;
    g_fn_heur = heur;
    g_fn_timeout = timeout;
    if (cb40 != NULL)    g_cont_cb40 = (unsigned char *)cb40 + CAP_STOLEN_CB40;
    if (heur != NULL)    g_cont_heur = (unsigned char *)heur + CAP_STOLEN_HEUR;
    if (timeout != NULL) g_cont_timeout = (unsigned char *)timeout + CAP_STOLEN_TIMEOUT;
}

void __declspec(naked) cap_fn_cb40_hook(void)
{
    __asm {
        mov eax, dword ptr [esp]       /* настоящий адрес возврата */
        mov dword ptr [g_ret_cb40], eax
        /* v6: [esp+4]=arg1=spec (PathState*), [esp+8]=arg2=pos (не читается
         * cb40). spec+0x10 = out_metric — слот финального гейта. */
        mov eax, dword ptr [esp+4]
        mov dword ptr [g_cap_a1], eax
        mov eax, dword ptr [esp+8]
        mov dword ptr [g_cap_a2], eax
        push ecx                       /* v7/v8: снять scale + состояние цепочки */
        push ecx
        call cap_read_cb40_scale
        add esp, 4
        pop ecx
        mov dword ptr [esp], offset cap_ret_cb40
        sub esp, 0x3c                  /* украденные байты пролога cb40 */
        push ebx
        push ebp
        push esi
        push edi
        mov esi, ecx
        jmp dword ptr [g_cont_cb40]
    }
}

void __declspec(naked) cap_fn_heur_hook(void)
{
    __asm {
        mov eax, dword ptr [esp]
        mov dword ptr [g_ret_heur], eax
        mov dword ptr [esp], offset cap_ret_heur
        push ebp                       /* украденные байты c690 */
        mov ebp, ecx
        mov ecx, dword ptr [ebp]
        jmp dword ptr [g_cont_heur]
    }
}

void __declspec(naked) cap_fn_timeout_hook(void)
{
    __asm {
        mov eax, dword ptr [esp]
        mov dword ptr [g_ret_timeout], eax
        mov dword ptr [esp], offset cap_ret_timeout
        push ecx                       /* украденные байты c5a0 */
        push esi
        mov esi, ecx
        mov eax, dword ptr [g_iat_timer]
        call dword ptr [eax]           /* call *0x4ec27c (timeGetTime) */
        mov dword ptr [g_timer_ms], eax /* точный ms, который увидит c5a0 */
        jmp dword ptr [g_cont_timeout]
    }
}

void __declspec(naked) cap_ret_cb40(void)
{
    __asm {
        /* v6: cb40 уже выполнил `pop edi/esi/ebp/ebx; add esp,0x3c; ret 8`.
         * его кадр ещё не перезаписан: L[0x10] (операнд финального FCOMPS),
         * L[0x2c], L[0x14], L[0x18] лежат по отрицательным смещениям от
         * текущего ESP (см. разбор 0x45cba0/0x45cc18/0x45cd57 в game_dis). */
        mov dword ptr [g_cb40_ret], eax  /* сохранить результат cb40 до чтения кадровых слотов */
        mov eax, dword ptr [esp-48h]
        mov dword ptr [g_gate_l10], eax
        mov eax, dword ptr [esp-44h]
        mov dword ptr [g_gate_l14], eax
        mov eax, dword ptr [esp-40h]
        mov dword ptr [g_gate_l18], eax
        mov eax, dword ptr [esp-2ch]
        mov dword ptr [g_gate_l2c], eax
        mov eax, dword ptr [g_cb40_ret]
        push eax
        push ecx
        push edx
        push eax
        call cap_impl_path
        add esp, 4
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_ret_cb40]
    }
}

void __declspec(naked) cap_ret_heur(void)
{
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call cap_impl_heur
        add esp, 4
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_ret_heur]
    }
}

void __declspec(naked) cap_ret_timeout(void)
{
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call cap_impl_timeout
        add esp, 4
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [g_ret_timeout]
    }
}

void __cdecl cap_impl_path(int value);
void __cdecl cap_impl_heur(int value);
void __cdecl cap_impl_timeout(int value);

void __cdecl cap_impl_path(int value)
{
    int words[CAP_WORDS];

    if (value == 0) {
        battle_capture_leaf(CAP_LEAF_PATH, 0, NULL);
        return;
    }
    __try {
        memcpy(words, (const void *)value, sizeof(words));
        battle_capture_leaf(CAP_LEAF_PATH, value, words);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        battle_capture_leaf(CAP_LEAF_PATH, 0, NULL);
    }
}

void __cdecl cap_impl_heur(int value)
{
    battle_capture_leaf(CAP_LEAF_HEUR, value, NULL);
}

void __cdecl cap_impl_timeout(int value)
{
    /* c5a0 возвращает флаг в AL (`xor al,al` / `mov $1,al` @0x45c5d1/0x45c5d7),
     * верхние байты EAX — мусор (ms из timeGetTime); потребитель @0x45e153
     * тестирует только AL. Пишем семантический 0/1. */
    battle_capture_leaf(CAP_LEAF_TIMEOUT, value & 0xff, NULL);
    if (g_tout_n > 0 && g_tout_n <= CAP_MAX_LEAF) {
        g_tms[g_tout_n - 1] = (int)g_timer_ms;   /* v5: внешний вход предиката */
    }
}
