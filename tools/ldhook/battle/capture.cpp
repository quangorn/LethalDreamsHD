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
#define CAP_WORDS       5          /* dword'ы возвращённого cb40-объекта */

typedef struct {
    int x, y;
} cap_pos;

typedef struct {
    cap_pos pos;
    int filter, g, d2, h10k, cost;
    cap_pos parent;
    cap_pos nbr[9];
    int occ_key;
} cap_node;

static int   g_enabled = 0;
static FILE *g_file = NULL;
static int   g_max_calls = CAP_MAX_CALLS;
static int   g_seq = 0;            /* monotonic счётчик захваченных вызовов */
static int   g_closed = 0;

/* Буферы, выделяются один раз в init (без повторных больших malloc в хуке). */
static cap_node *g_nodes = NULL;
static int      *g_occ = NULL;
static cap_pos  *g_iter = NULL;
static int       g_iter_cap = 4096;
static int      *g_path = NULL;
static int      *g_heur = NULL;
static int      *g_tout = NULL;

static void     *g_this = NULL;
static void     *g_out_code = NULL;
static int       g_filter = 0;
static cap_pos   g_cmp = { -1, -1 };
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
    g_iter = (cap_pos *)malloc((size_t)g_iter_cap * sizeof(cap_pos));
    g_path = (int *)malloc((size_t)CAP_MAX_LEAF * CAP_WORDS * sizeof(int));
    g_heur = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    g_tout = (int *)malloc((size_t)CAP_MAX_LEAF * sizeof(int));
    if (!g_nodes || !g_occ || !g_iter || !g_path || !g_heur || !g_tout) {
        free(g_nodes); free(g_occ); free(g_iter);
        free(g_path); free(g_heur); free(g_tout);
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

static void cap_read_occ(BattleAI *self)
{
    TreeNode *head, *cur;

    g_occ_n = 0;
    __try {
        head = self->occ_head;
        cur = (head != NULL) ? head->left : NULL;
        while (cur != NULL && cur != head && g_occ_n < CAP_MAX_NODES) {
            g_occ[g_occ_n++] = cur->key;
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
    fprintf(f, "],\"occ_key\":%d}", n->occ_key);
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
    __try {
        if (self != NULL) {
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
    fprintf(g_file, "{\"v\":1,\"kind\":\"cap\",\"seq\":%d,\"tick\":%d,"
            "\"unit\":%d,\"fn\":\"e0a0\",\"args\":{\"filter\":%d,"
            "\"cmp\":[%d,%d],\"timeout_bits\":%u,\"out_ptr\":%d},"
            "\"ai\":{\"timer_bits\":%u,\"final_bits\":%u,"
            "\"dist_bits\":%u,\"yrad\":%d,\"xrad\":%d,\"rect\":[%d,%d,%d,%d],"
            "\"anchor\":[%d,%d],\"trk\":%d,\"bd2\":%u,\"code\":%d,"
            "\"ctx_flags\":%u,\"world_scale_bits\":%u},",
            g_seq, g_tick, g_unit, g_filter, g_cmp.x, g_cmp.y, g_timeout_bits,
            out_code ? 1 : 0, timer, finalb, distb,
            self ? self->y_radius : -1, self ? self->x_radius : -1,
            self ? self->canvas_rect.left : -1, self ? self->canvas_rect.top : -1,
            self ? self->canvas_rect.right : -1, self ? self->canvas_rect.bottom : -1,
            anchor.x, anchor.y, self ? (int)self->tracking : -1,
            self ? self->distance_threshold : 0xFFFFFFFFu,
            self ? *(const int *)((const unsigned char *)self + 0x38) : -1,
            ctxflags, worldscale);
    fputs("\"map1\":", g_file);
    cap_write_map1(g_file);
    fputs(",\"occ\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    fputs(",\"grid\":", g_file);
    cap_write_grid(g_file, self);
    fprintf(g_file, ",\"trap\":%d}\n", g_map_trap);
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
        if (g_path_n + CAP_WORDS > CAP_MAX_LEAF * CAP_WORDS) {
            g_leaf_overflow = 1;
            return;
        }
        if (value == 0 || words == NULL) {
            g_path[g_path_n++] = 0x7fffffff;    /* NULL-маркер */
            g_path_n += CAP_WORDS - 1;
            return;
        }
        __try {
            memcpy(&g_path[g_path_n], words, CAP_WORDS * sizeof(int));
            g_path_n += CAP_WORDS;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
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
    fprintf(g_file, "{\"v\":1,\"kind\":\"res\",\"seq\":%d,\"node\":", g_seq);
    if (node != NULL) {
        fprintf(g_file, "[%d,%d]", node_x, node_y);
    } else {
        fputs("null", g_file);
    }
    fprintf(g_file, ",\"code\":%d,\"iters\":%d,\"iter_seq\":", code, iters);
    cap_write_iter(g_file);
    /* map1_after/occ_after — состояние после вызова (источник новых узлов). */
    cap_read_map1(self);
    cap_read_occ(self);
    fputs(",\"map1_after\":", g_file);
    cap_write_map1(g_file);
    fputs(",\"occ_after\":", g_file);
    cap_write_ints(g_file, g_occ, g_occ_n);
    /* Поток листьев нативных калlee (перепроецируется харнессом). */
    fputs(",\"leaf\":{\"path\":", g_file);
    cap_write_path(g_file);
    fputs(",\"heur\":", g_file);
    cap_write_ints(g_file, g_heur, g_heur_n);
    fputs(",\"timeout\":", g_file);
    cap_write_ints(g_file, g_tout, g_tout_n);
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
        jmp dword ptr [g_cont_timeout]
    }
}

void __declspec(naked) cap_ret_cb40(void)
{
    __asm {
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
}
