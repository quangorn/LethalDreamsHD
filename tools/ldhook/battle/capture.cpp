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
#include "native.h"

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
static void     *g_out_code = NULL;
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
} bdd0_frame;

static bdd0_frame g_bdd0_stack[BDD0_STACK_MAX];
static int    g_bdd0_sp = 0;       /* 0 — стек пуст */

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
    setvbuf(g_file, NULL, _IONBF, 0);   /* сборочная запись: краш не теряет записи */
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
    ld_info("battle_capture: %s=%s (max=%d)", BATTLE_CAPTURE_ENV, path,
            g_max_calls);
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

static void cap_write_grid_record(BattleAI *self)
{
    GridLevel *levels[16];
    int nlev = 0, leaf = 0;
    int sx = 0, sy = 0, n = 0, ok = 1;
    unsigned char *cells = NULL;
    int **rows = NULL;
    cap_grid_deps dep;
    void *world = NULL;
    int i;

    cap_read_grid_deps(self, &dep);
    __try {
        if (self != NULL && self->game_ctx != NULL) {
            world = *(void **)((unsigned char *)self->game_ctx + 4);
            levels[0] = *(GridLevel **)((unsigned char *)self->game_ctx + 8);
            while (nlev < 16 && levels[nlev] != NULL) {
                if (levels[nlev]->next_level == NULL) break;
                levels[nlev + 1] = levels[nlev]->next_level;
                nlev++;
            }
            nlev++;
            leaf = nlev - 1;
            sx = levels[leaf]->size_x;
            sy = levels[leaf]->size_y;
            cells = (unsigned char *)levels[leaf]->cells;
            rows = levels[leaf]->rows;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = 0;
    }
    if (!ok || sx <= 0 || sy <= 0 || cells == NULL || rows == NULL ||
        (long long)sx * sy > CAP_MAX_GRID_CELLS) {
        fprintf(g_file, "{\"v\":5,\"kind\":\"grid\",\"error\":1,"
                        "\"sx\":%d,\"sy\":%d}\n", sx, sy);
        return;
    }
    n = sx * sy;
    __try {
        for (i = 0; i < n; i++) {
            unsigned char *c = cells + i * S6_GRID_CELL_SIZE;
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
            g_gstatus[i] = native::tree_lookup(world, (unsigned int)key);
            g_gmin[i] = *(int *)(c + 0x14);
            g_gmax[i] = *(int *)(c + 0x18);
            /* +0x1c — уровень ячейки (FUN_00456b20 через spec->pos) */
            g_gnext[i] = (nx >= cells && nx < cells + (size_t)n * S6_GRID_CELL_SIZE)
                         ? (int)(((unsigned char *)nx - cells) / S6_GRID_CELL_SIZE) : -1;
            g_gchain[i] = (ch >= cells && ch < cells + (size_t)n * S6_GRID_CELL_SIZE)
                          ? (int)(((unsigned char *)ch - cells) / S6_GRID_CELL_SIZE) : -1;
        }
        g_grid_n = n;
        memcpy(g_tree_prev, g_gstatus, (size_t)n * sizeof(int));
        cap_read_cell_fields(cells, n, g_prev_fields);
        for (i = 0; i < n; i++) {
            g_prev_rows[i] = rows[i / sy][i % sy];
        }
        for (i = 0; i < n; i++) {
            int x = i / sy, y = i % sy;
            g_grows[i] = rows[x][y];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(g_file, "{\"v\":5,\"kind\":\"grid\",\"error\":1,"
                        "\"sx\":%d,\"sy\":%d,\"trap\":1}\n", sx, sy);
        return;
    }

    fprintf(g_file, "{\"v\":%d,\"kind\":\"grid\",\"sx\":%d,\"sy\":%d,"
                    "\"leaf\":%d,\"levels\":[", CAP_VERSION, sx, sy, leaf);
    for (i = 0; i < nlev; i++) {
        fprintf(g_file, "%s{\"i\":%d,\"sx\":%d,\"sy\":%d,\"rx\":%d,\"ry\":%d,"
                        "\"sa\":%u,\"sl\":%u,\"sm\":%u,\"next\":%d}",
                i ? "," : "", i, levels[i]->size_x, levels[i]->size_y,
                levels[i]->rec_x, levels[i]->rec_y,
                *(unsigned int *)((unsigned char *)levels[i] + 0x20),
                *(unsigned int *)((unsigned char *)levels[i] + 0x24),
                *(unsigned int *)((unsigned char *)levels[i] + 0x28),
                (i + 1 < nlev) ? i + 1 : -1);
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
        return;
    }
    if (g_seq >= g_max_calls) {
        g_closed = 1;
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
        return;
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
    int ai_ptr = 0, u170 = -1;
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
            ai_ptr = *(const int *)((const unsigned char *)self + 0x138);
            u170 = *(const int *)((const unsigned char *)self + 0x170);
            /* +0x114 — float, который входной гейт сравнивает с [0x4ec37c]
             * (`flds 0x114(%ebp)` @0x44bdf9); пишем биты. */
            gate114 = *(const unsigned int *)((const unsigned char *)self +
                                              0x114);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        p1 = -1; p2 = -1; p3 = 0; ai_ptr = 0; u170 = -1; gate114 = 0;
    }

    seq = ++g_bdd0_seq;
    g_bdd0_stack[g_bdd0_sp].seq = seq;
    g_bdd0_stack[g_bdd0_sp].self = self;
    g_bdd0_sp++;
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_cap\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"44bdd0\",\"self\":%d,\"p1\":%d,\"p2\":%d,"
            "\"p3\":%d,\"ai\":%d,\"u170\":%d,\"gate114\":%u,\"trap\":%d}\n",
            CAP_BDD0_VER, seq, g_tick, g_unit, (int)(size_t)self, p1, p2, p3,
            ai_ptr, u170, gate114, trap);
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
    fprintf(g_file, "{\"v\":%d,\"kind\":\"bdd0_res\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"44bdd0\",\"al\":%d,\"u170\":%d,\"out\":%d,"
            "\"m0\":%u,\"m1\":%u,\"m2\":%u,\"anc\":%d,\"tag\":%u,"
            "\"tri0\":%u,\"tri1\":%u,\"tri2\":%u,\"a4n\":%d,\"trap\":%d}\n",
            CAP_BDD0_VER, seq, g_tick, g_unit, al, u170, out, m0, m1, m2,
            anc, tag, tri0, tri1, tri2, a4n, trap);
}

void battle_capture_leaf(int kind, int value, const int *words)
{
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
