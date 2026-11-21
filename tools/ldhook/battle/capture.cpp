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
#define CAP_VERSION     6          /* версия cap/res; v1..v5 — старые захваты */

/* v3: значение узла map2 (PathState*) — раньше реплика ставила NULL и всегда
 * шла «свободным путём». w[] — сырые dword'ы PathState; ptr[] — 11 указателей
 * LDPos* (pos, parent, nbr[9]), ptr-поля в w[] харнесс подменяет по identity. */
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
static int   g_seq = 0;            /* monotonic счётчик захваченных вызовов */
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
        !g_heur || !g_tout || !g_tms || !g_gate || !g_gkey || !g_gstatus || !g_gmin ||
        !g_gmax || !g_gnext || !g_gchain || !g_grows ||
        !g_gw0 || !g_gw1 || !g_gw2 || !g_gw3 || !g_gw4 ||
        !g_tree_prev || !g_td_idx || !g_td_val ||
        !g_prev_fields || !g_cd_idx || !g_cd_val || !g_prev_rows) {
        free(g_nodes); free(g_occ); free(g_occ_nodes); free(g_iter);
        free(g_path); free(g_pptr); free(g_heur); free(g_tout); free(g_tms);
        free(g_gate);
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
    fprintf(g_file, ",\"overflow\":%d},\"trap\":%d}\n",
            g_leaf_overflow, g_map_trap);
}

/* ----------------------------- трамплины листьев ------------------------ */
/* Хук на ВХОДЕ функции (не на call-site): подменяем адрес возврата на стеке,
 * выполняем украденные байты пролога и прыгаем на оригинал. Возврат original
 * (RET 8/4) попадает в cap_ret_*, который логирует EAX и прыгает на настоящий
 * адрес возврата. Никаких манипуляций аргументами — только ESP/ECX как есть. */

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
