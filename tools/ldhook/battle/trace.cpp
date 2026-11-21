/* battle/trace.cpp — наблюдатели боевого тика и trace-строка (п. 1.7 плана).
 *
 * Одна строка на вызов FUN_0044bdd0 = один тик одного юнита. strока пишется
 * на эпилоге 0x44d074 (H2); H1 на 0x44bdf5 назначает tick/ordinal и сбрасывает
 * аккумуляторы тика; H7 на 0x45dd70 (и вход порта s6_ai_update) считает vis;
 * H3 на 0x44cbc5 читает код/trk/bd2 после вызова 45eee0.
 *
 * Поля вызовов e0a0 (c1,x1,y1,i1,fx,fy,lx,ly / c2,x2,y2,i2 / g,h10k,cost) —
 * H4 (вход: слот 1/2) + H5 (эпилоги `83 C4 34 C2 10 00`) + H6 (хвост цикла
 * 0x45e163). Значения приходят от наблюдателя того режима, в котором живёт
 * e0a0: native — трамплины site5 (s5_epi_hook/s5_tail_hook), порт — сам
 * s6_target_search (battle/target.cpp). Отдельных детуров наблюдатели не
 * ставят (инвариант 5, ab_method §2).
 *
 * Дисциплина трамплина (re/battle_ai_ab_method.md §2): pushfd+pushad, вызов
 * impl, popad/popfd, затем украденные байты дословно, затем jmp на продолжение.
 * ESP при jmp равен оригинальному; H1 восстанавливает флаги для `je`; `flds`
 * на H1 — нативная инструкция и выполняется ровно один раз. H3 украденные
 * байты воспроизводит ДО pushad/pushfd (они адресуют [esp+0xc0] от ESP сайта),
 * а флаги/регистры после вызова 45eee0 восстанавливает из post-call состояния.
 *
 * Наблюдатели только читают игровую память (all derefs под __try/__except);
 * FPU control word не трогается; счётчики без блокировок (боевой AI —
 * главный поток, ab_method.md §2.6).
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "structs.h"
#include "trace.h"
#include "capture.h"

/* Оффсеты, которых нет в struct BattleAI (размер зафиксирован только до
 * +0x7c): размер map1 и размер map2 читаются эпилогом 44bdd0. */
#define TRACE_UNIT_OFF_AI    0x138   /* unit → BattleAI (встроен по адресу unit+0x138) */
#define TRACE_UNIT_OFF_U170  0x170   /* unit → dispatch-ветка */
#define TRACE_UNIT_OFF_M0    0xa4    /* unit → 3 float-бита результата 45e1d0 */
#define TRACE_UNIT_OFF_M2    0xac
#define TRACE_UNIT_ESP_OUT   0x164   /* [esp+0x164] → param_3 (out-код) */
#define TRACE_AI_OFF_S1      0x70    /* BattleAI → размер map1 */
#define TRACE_AI_OFF_S2      0x7c    /* BattleAI → размер map2 */
#define TRACE_AI_OFF_TRK     0x30    /* BattleAI → tracking (char, H3) */
#define TRACE_AI_OFF_BD2     0x34    /* BattleAI → лучший dist² (uint, H3) */
#define TRACE_AI_OFF_CODE    0x38    /* BattleAI → код решения (int, H3/H5) */
#define TRACE_POS_OFF_X      0x0c    /* LDPos → x */
#define TRACE_POS_OFF_Y      0x10    /* LDPos → y */
#define TRACE_NODE_OFF_POS   0x00    /* узел (Candidate/PathState) → LDPos* */
#define TRACE_NODE_OFF_G     0x18    /* PathState.g_cost */
#define TRACE_NODE_OFF_H     0x20    /* PathState.h10000 */
#define TRACE_NODE_OFF_COST  0x24    /* PathState.metric */

#define TRACE_UNITS          64      /* ordinal'ы юнитов: ptr → id */

/* Продолжения/цели ветвлений трамплинов (заполняет ldhook.cpp при установке). */
void *g_h1_cont = NULL;   /* 0x44bdff */
void *g_h1_je   = NULL;   /* 0x44be58 — цель нативного `je` */
void *g_h2_cont = NULL;   /* 0x44d07b */
void *g_h3_cont = NULL;   /* 0x44cbd5 */
void *g_h3_callee = NULL; /* 0x45eee0 (FUN_0045eee0, native::fn) */
void *g_h7_cont = NULL;   /* 0x45dd78 */

static int  g_enabled = 0;
static volatile LONG g_tick_no = 0;
static int  g_tick_unit_id = -1;
static void *g_tick_unit = NULL;   /* сохранён H1; читается H2 */
static volatile LONGLONG g_tick_t0 = 0;
static volatile LONG g_vis = 0;

/* H3: поля тика (последняя запись 45eee0 за тик; sentinel — не было вызова). */
static int          g_tick_code = -1;
static int          g_tick_trk  = -1;
static unsigned int g_tick_bd2  = 0xFFFFFFFFu;

/* Один слот на вызов e0a0 внутри тика (поля 7–21 п. 1.7): вызов №1 (filter=1)
 * и №2 (filter=2). Слот выбирается счётчиком H4; поля — H5 (узел-возврат,
 * [this+0x38]) и H6 (латчи цикла, endpoint'ы). */
typedef struct {
    int  used;                  /* выход e0a0 был (эпилог native / return порта) */
    int  c;                     /* [this+0x38] на выходе (H5) → c1/c2 */
    int  x, y;                  /* pos возвращённого узла (H5) */
    long g, h10k, cost;         /* узел +0x18/+0x20/+0x24 (H5) */
    long latches;               /* H6: число латчей цикла (rescan) */
    int  seen;                  /* H6: был хотя бы один непустой кандидат */
    int  fx, fy;                /* H6: первый непустой кандидат (LDPos) */
    int  lx, ly;                /* H6: последний непустой кандидат */
} trace_e0a0_slot;

static trace_e0a0_slot g_slot[2];
static int  g_slot_cur = 0;        /* 1-й/2-й вызов e0a0 в тике (индекс 0/1) */
static void *g_e0a0_this = NULL;   /* ECX входа e0a0 (BattleAI) — H5: +0x38 */

/* Счётчик входов e0a0 за тик: H4 (сброс), выбор слота trace. */
static volatile LONG g_e0a0_calls = 0;

/* ------------------------ STATEFP: отпечаток состояния ------------------ *
 * Считается на входе e0a0 (H4), когда LDHOOK_BATTLE_TRACE=1. Хэш FNV-1a 32:
 * basis 0x811c9dc5, prime 0x01000193, каждое int32 подаётся 4 байтами
 * little-endian. В хэш идут ТОЛЬКО значения: map1 in-order (pos.x, pos.y,
 * filter, g_cost +0x18, dist2 +0x1c, h10000 +0x20, metric/cost +0x24),
 * map2 in-order (ключ узла), а также размеры s1/s2 и vis пишутся отдельно.
 * Указатели в хэш не попадают; при трапе разыменования — trap=1, хэш
 * достраивается sentinels. Вне трассировки функция не вызывается.
 */
#define FP_FNV_BASIS  0x811c9dc5u
#define FP_FNV_PRIME  0x01000193u
#define FP_MAX_NODES  200000      /* защита от зацикливания обхода дерева */

typedef struct {
    unsigned int h1;              /* FNV(map1) */
    unsigned int h2;              /* FNV(map2 keys) */
    long s1, s2;                  /* [ai+0x70], [ai+0x7c] */
    int trap;
} trace_statefp;

static void fp_put(unsigned int *h, unsigned int v)
{
    int b;
    for (b = 0; b < 4; b++) {
        *h ^= (v >> (b * 8)) & 0xffu;
        *h *= FP_FNV_PRIME;
    }
}

static void battle_statefp(void *this_, trace_statefp *out)
{
    BattleAI *ai = (BattleAI *)this_;
    TreeNode *head, *cur;
    int guard;

    out->h1 = FP_FNV_BASIS;
    out->h2 = FP_FNV_BASIS;
    out->s1 = -1;
    out->s2 = -1;
    out->trap = 0;
    if (ai == NULL) {
        out->trap = 1;
        return;
    }
    __try {
        head = ai->cand_head;
        out->s1 = *(const long *)((const unsigned char *)ai + TRACE_AI_OFF_S1);
        out->s2 = *(const long *)((const unsigned char *)ai + TRACE_AI_OFF_S2);
        cur = (head != NULL) ? head->left : NULL;
        for (guard = 0; cur != NULL && cur != head && guard < FP_MAX_NODES;
             guard++) {
            PathState *ps = (PathState *)cur->value;
            if (ps != NULL) {
                LDPos *p = ps->pos;
                fp_put(&out->h1, (p != NULL) ? (unsigned int)p->x : 0xffffffffu);
                fp_put(&out->h1, (p != NULL) ? (unsigned int)p->y : 0xffffffffu);
                fp_put(&out->h1, (unsigned int)ps->filter);
                fp_put(&out->h1, (unsigned int)ps->g_cost);
                fp_put(&out->h1, (unsigned int)ps->dist2);
                fp_put(&out->h1, (unsigned int)ps->h10000);
                fp_put(&out->h1, (unsigned int)ps->metric);
            }
            cur = s6_tree_next(cur);
        }
        if (guard >= FP_MAX_NODES) {
            out->trap = 1;
        }
        head = ai->occ_head;
        cur = (head != NULL) ? head->left : NULL;
        for (guard = 0; cur != NULL && cur != head && guard < FP_MAX_NODES;
             guard++) {
            fp_put(&out->h2, (unsigned int)cur->key);
            cur = s6_tree_next(cur);
        }
        if (guard >= FP_MAX_NODES) {
            out->trap = 1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out->trap = 1;
    }
}

/* uint-таблица ordinal'ов: id = порядок первого появления указателя. */
static void *g_unit_ptr[TRACE_UNITS];
static int   g_unit_id[TRACE_UNITS];
static int   g_unit_n = 0;

void battle_trace_init(void)
{
    const char *s = getenv(BATTLE_TRACE_ENV);

    g_enabled = (s != NULL && *s != '\0' && atoi(s) != 0) ? 1 : 0;
    if (g_enabled) {
        memset(g_unit_ptr, 0, sizeof(g_unit_ptr));
        memset(g_unit_id, 0, sizeof(g_unit_id));
        g_unit_n = 0;
        ld_info("battle_trace: %s=1 (tick trace ON)", BATTLE_TRACE_ENV);
    }
}

int battle_trace_enabled(void)
{
    return g_enabled;
}

/* uint32 из env: decimal (по умолчанию) или 0x-hex. */
int battle_seed_get(unsigned long *out)
{
    const char *s = getenv(BATTLE_SEED_ENV);
    char *end = NULL;
    unsigned long v;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        v = strtoul(s + 2, &end, 16);
    } else {
        v = strtoul(s, &end, 10);
    }
    if (end == s || (end != NULL && *end != '\0')) {
        ld_info("battle_seed: %s=%s invalid (decimal or 0x-hex uint32)",
                BATTLE_SEED_ENV, s);
        return 0;
    }
    *out = (unsigned long)(unsigned int)v;
    return 1;
}

/* ordinal юнита: линейный поиск по 64 слотам, id = порядок появления. */
static int trace_unit_id(void *unit)
{
    int i;

    if (unit == NULL) {
        return -1;
    }
    for (i = 0; i < g_unit_n; i++) {
        if (g_unit_ptr[i] == unit) {
            return g_unit_id[i];
        }
    }
    if (g_unit_n < TRACE_UNITS) {
        g_unit_ptr[g_unit_n] = unit;
        g_unit_id[g_unit_n] = g_unit_n + 1;
        return g_unit_id[g_unit_n++];
    }
    return -1;                  /* таблица переполнена — ordinal неизвестен */
}

static void trace_slot_reset(trace_e0a0_slot *s)
{
    s->used = 0;
    s->c = -1;
    s->x = -1;
    s->y = -1;
    s->g = -1;
    s->h10k = -1;
    s->cost = -1;
    s->latches = 0;
    s->seen = 0;
    s->fx = -1;
    s->fy = -1;
    s->lx = -1;
    s->ly = -1;
}

void __cdecl battle_trace_tick_begin(void *unit)
{
    int cap_on = battle_capture_enabled();

    /* Capture работает и при trace=0: H1 даёт ему tick и ordinal юнита. */
    if (!g_enabled && !cap_on) {
        return;
    }
    g_tick_unit = unit;
    g_tick_unit_id = trace_unit_id(unit);
    InterlockedIncrement(&g_tick_no);
    if (cap_on) {
        battle_capture_tick_begin(unit, g_tick_no, g_tick_unit_id);
    }
    if (!g_enabled) {
        return;
    }
    g_tick_t0 = ld_now_us();
    g_vis = 0;
    g_e0a0_calls = 0;
    g_slot_cur = 0;
    g_e0a0_this = NULL;
    g_tick_code = -1;
    g_tick_trk = -1;
    g_tick_bd2 = 0xFFFFFFFFu;
    trace_slot_reset(&g_slot[0]);
    trace_slot_reset(&g_slot[1]);
}

/* Строка пишется ДО воспроизведения украденных байт эпилога.
 * unit берётся из g_tick_unit (сохранён H1), esp — из кадра для [esp+0x164]. */
void __cdecl battle_trace_tick_end(int al, void *esp)
{
    long tick;
    int unit_id;
    int u170 = -1;
    int out = -1;
    long s1 = -1;
    long s2 = -1;
    long vis;
    unsigned int m0 = 0xFFFFFFFFu;
    unsigned int m1 = 0xFFFFFFFFu;
    unsigned int m2 = 0xFFFFFFFFu;
    unsigned int ai = 0;
    LONGLONG us = 0;
    int trap = 0;
    void *unit;
    void *this_ptr;

    /* H3 (код/trk/bd2) и слоты вызовов e0a0 (H5/H6) — заполнены в течение тика. */
    const trace_e0a0_slot *sl1 = &g_slot[0];
    const trace_e0a0_slot *sl2 = &g_slot[1];
    const trace_e0a0_slot *last = sl2->used ? sl2 : (sl1->used ? sl1 : NULL);
    const int  code = g_tick_code;
    const int  trk = g_tick_trk;
    const unsigned int bd2 = g_tick_bd2;
    const int  c1 = sl1->used ? sl1->c : -1;
    const int  x1 = sl1->used ? sl1->x : -1;
    const int  y1 = sl1->used ? sl1->y : -1;
    const long i1 = sl1->used ? sl1->latches : -1L;
    const int  fx = (sl1->used && sl1->seen) ? sl1->fx : -1;
    const int  fy = (sl1->used && sl1->seen) ? sl1->fy : -1;
    const int  lx = (sl1->used && sl1->seen) ? sl1->lx : -1;
    const int  ly = (sl1->used && sl1->seen) ? sl1->ly : -1;
    const int  c2 = sl2->used ? sl2->c : -1;
    const int  x2 = sl2->used ? sl2->x : -1;
    const int  y2 = sl2->used ? sl2->y : -1;
    const long i2 = sl2->used ? sl2->latches : -1L;
    const long g = last ? last->g : -1L;
    const long h10k = last ? last->h10k : -1L;
    const long cost = last ? last->cost : -1L;

    if (!g_enabled) {
        return;
    }

    tick = g_tick_no;
    unit_id = g_tick_unit_id;
    unit = g_tick_unit;
    this_ptr = NULL;

    __try {
        if (unit != NULL) {
            /* BattleAI встроен в unit по адресу unit+0x138 (this передаётся
             * как адрес, а не значение); map1 в BattleAI+0x68, map2 в +0x74,
             * size в +0x70/+0x7c (MSVC std::map: head+4, size+8). */
            this_ptr = (void *)((const unsigned char *)unit + TRACE_UNIT_OFF_AI);
            u170 = *(const int *)((const unsigned char *)unit +
                                  TRACE_UNIT_OFF_U170);
            m0 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M0);
            m1 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M0 + 4);
            m2 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M2);
            if (this_ptr != NULL) {
                ai = (unsigned int)(size_t)this_ptr;
                s1 = *(const long *)((const unsigned char *)this_ptr +
                                     TRACE_AI_OFF_S1);
                s2 = *(const long *)((const unsigned char *)this_ptr +
                                     TRACE_AI_OFF_S2);
            }
        }
        if (esp != NULL) {
            void *op = *(void * const *)((const unsigned char *)esp +
                                         TRACE_UNIT_ESP_OUT);
            out = (op != NULL) ? *(const int *)op : -1;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        trap = 1;
        u170 = -1;
        out = -1;
        s1 = -1;
        s2 = -1;
        m0 = 0xFFFFFFFFu;
        m1 = 0xFFFFFFFFu;
        m2 = 0xFFFFFFFFu;
        ai = 0;
    }

    vis = g_vis;
    if (g_tick_t0 != 0) {
        us = ld_now_us() - g_tick_t0;
    }

    ld_info("BATTLE tick=%ld unit=%d code=%d u170=%d al=%d out=%d "
            "c1=%d x1=%d y1=%d i1=%ld fx=%d fy=%d lx=%d ly=%d "
            "c2=%d x2=%d y2=%d i2=%ld g=%ld h10k=%ld cost=%ld "
            "trk=%d bd2=%u s1=%ld s2=%ld vis=%ld "
            "m0=%08x m1=%08x m2=%08x us=%I64d ai=%08x",
            tick, unit_id, code, u170, al, out,
            c1, x1, y1, i1, fx, fy, lx, ly,
            c2, x2, y2, i2, g, h10k, cost,
            trk, bd2, s1, s2, vis,
            m0, m1, m2, us, ai);

    if (trap) {
        static volatile LONG trap_ctr = 0;
        if (ld_rate(&trap_ctr, 1, 4096)) {
            ld_debug("battle_trace: trap on tick=%ld unit=%d (sentinels written)",
                     tick, unit_id);
        }
    }
}

void __cdecl battle_trace_visit(void)
{
    if (!g_enabled) {
        return;
    }
    g_vis++;
}

/* H4: вход FUN_0045e0a0 (native site5 entry или detour s6_target_search).
 * Счётчик тика выбирает слот: вызов №1 → c1/x1/y1/i1/fx..ly, №2 → c2/x2/y2/i2.
 * this сохраняется для H5 ([this+0x38]). Сброс слота — здесь, а не на выходе:
 * выход может не случиться (trap/ранний return вызывающего). */
void __cdecl battle_trace_e0a0_enter(void *this_, int filter)
{
    LONG n;

    if (!g_enabled) {
        return;
    }
    n = InterlockedIncrement(&g_e0a0_calls);
    g_e0a0_this = this_;
    g_slot_cur = (n == 1) ? 0 : ((n == 2) ? 1 : -1);
    if (g_slot_cur >= 0) {
        trace_slot_reset(&g_slot[g_slot_cur]);
    }

    /* STATEFP — состояние, которое реально приходит в e0a0. Отдельная строка,
     * чтобы не раздувать BATTLE; пишется только при trace=1 (выше early-return). */
    {
        trace_statefp fp;
        battle_statefp(this_, &fp);
        ld_info("STATEFP tick=%ld unit=%d call=%ld filter=%d "
                "s1=%ld s2=%ld h1=%08x h2=%08x vis=%ld trap=%d",
                g_tick_no, g_tick_unit_id, n, filter,
                fp.s1, fp.s2, fp.h1, fp.h2, g_vis, fp.trap);
    }
}

/* H5: выход e0a0 — EAX = возвращённый узел (NULL при пустом скане).
 * native: эпилоги `83 C4 34 C2 10 00` (s5_epi_hook); порт: return
 * s6_target_search. Читается узел (+0x00 → LDPos, +0x18/+0x20/+0x24) и
 * [this+0x38] → c1/c2. */
void __cdecl battle_trace_e0a0_exit(void *node)
{
    trace_e0a0_slot *s;

    if (!g_enabled || g_slot_cur < 0) {
        return;
    }
    s = &g_slot[g_slot_cur];
    s->used = 1;
    __try {
        if (g_e0a0_this != NULL) {
            s->c = *(const int *)((const unsigned char *)g_e0a0_this +
                                  TRACE_AI_OFF_CODE);
        }
        if (node != NULL) {
            const unsigned char *n = (const unsigned char *)node;
            const void *pos = *(void * const *)(n + TRACE_NODE_OFF_POS);
            if (pos != NULL) {
                s->x = *(const int *)((const unsigned char *)pos +
                                      TRACE_POS_OFF_X);
                s->y = *(const int *)((const unsigned char *)pos +
                                      TRACE_POS_OFF_Y);
            }
            s->g = *(const int *)(n + TRACE_NODE_OFF_G);
            s->h10k = *(const int *)(n + TRACE_NODE_OFF_H);
            s->cost = *(const int *)(n + TRACE_NODE_OFF_COST);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s->c = -1;
        s->x = -1;
        s->y = -1;
        s->g = -1;
        s->h10k = -1;
        s->cost = -1;
    }
}

/* H6: латч цикла e0a0 — native 0x45e163 (EAX = результат rescan, т.е.
 * кандидат следующей итерации) и rescan порта. Счётчик латчей = i1/i2;
 * первый и последний непустой кандидат → (fx,fy)/(lx,ly). У самого первого
 * кандидата (из начального скана 0x45e103) латча нет — как в native. */
void __cdecl battle_trace_e0a0_latch(void *cand)
{
    trace_e0a0_slot *s;

    if (!g_enabled || g_slot_cur < 0) {
        return;
    }
    s = &g_slot[g_slot_cur];
    s->latches++;
    if (cand == NULL) {
        return;                 /* пустой rescan: латч есть, кандидата нет */
    }
    __try {
        const void *pos = *(void * const *)((const unsigned char *)cand +
                                            TRACE_NODE_OFF_POS);
        if (pos != NULL) {
            int x = *(const int *)((const unsigned char *)pos + TRACE_POS_OFF_X);
            int y = *(const int *)((const unsigned char *)pos + TRACE_POS_OFF_Y);
            if (!s->seen) {
                s->fx = x;
                s->fy = y;
                s->seen = 1;
            }
            s->lx = x;
            s->ly = y;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s->seen = 0;
        s->fx = -1;
        s->fy = -1;
        s->lx = -1;
        s->ly = -1;
    }
}

/* H3: после `call 45eee0` — EAX = код решения (0/1/2/4), ESI = &unit+0x138
 * (BattleAI). Поля тика: code, trk (+0x30, char), bd2 (+0x34, uint). */
void __cdecl battle_trace_code(int code, void *ai)
{
    if (!g_enabled) {
        return;
    }
    g_tick_code = code;
    if (ai == NULL) {
        g_tick_trk = -1;
        g_tick_bd2 = 0xFFFFFFFFu;
        return;
    }
    __try {
        g_tick_trk = (int)*(const unsigned char *)(
            (const unsigned char *)ai + TRACE_AI_OFF_TRK);
        g_tick_bd2 = *(const unsigned int *)((const unsigned char *)ai +
                                             TRACE_AI_OFF_BD2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_tick_trk = -1;
        g_tick_bd2 = 0xFFFFFFFFu;
    }
}

/* ------------------------------------------------------------------ */
/* H1 — 0x44bdf5: 8B E9 (mov ebp,ecx) | 74 5F (je 0x44be58) |          */
/*                 D9 85 14 01 00 00 (flds [ebp+0x114]); продолжение   */
/*                 0x44bdff. Флаги для `je` — от TEST AL,AL @0x44bdef. */
/*                                                                     */
/* v10: capture 44bdd0 — между battle_trace_tick_begin и popad; стек после
 * `add esp,4` == стек после popad (flush-слот pushfd ниже). S = [esp+12]+4,
 * unit = слот ECX pushad = [esp+28] (после push). EFLAGS/регистры не меняются:
 * popfd возвращает флаги, попавшие в pushad. */
/* ------------------------------------------------------------------ */
__declspec(naked) void h1_tick_hook(void)
{
    __asm {
        pushfd
        pushad
        push ecx                     /* unit */
        call battle_trace_tick_begin
        add esp, 4
        mov edx, dword ptr [esp + 12]    /* сохранённый ESP (S-4) */
        add edx, 4                       /* ESP кадра 44bdd0 (S) */
        push edx                         /* arg1: esp кадра */
        push dword ptr [esp + 28]        /* arg0: unit (слот ECX pushad) */
        call battle_capture_bdd0_enter
        add esp, 8
        popad
        mov ebp, ecx                 /* украденная инструкция 1 */
        popfd                        /* флаги для je */
        je h1_je_taken
        _emit 0xD9                 /* flds dword ptr [ebp+0x114] */
        _emit 0x85
        _emit 0x14
        _emit 0x01
        _emit 0x00
        _emit 0x00
        jmp dword ptr [g_h1_cont]

    h1_je_taken:
        jmp dword ptr [g_h1_je]
    }
}

/* ------------------------------------------------------------------ */
/* H2 — 0x44d074: 8B 8C 24 4C 01 00 00 (mov ecx,[esp+0x14c]);          */
/*                 продолжение 0x44d07b. unit берётся из g_tick_unit,    */
/*                 [esp+0x164] — param_3 (out-код).                     */
/*                                                                     */
/* v10: capture 44bdd0 — тот же кадр S, AL и ESP кадра; сохраняет все   */
/* регистры/флаги/ESP, затем украденная инструкция и jmp.               */
/* ------------------------------------------------------------------ */
__declspec(naked) void h2_epilogue_hook(void)
{
    __asm {
        pushfd
        pushad
        mov edx, dword ptr [esp + 12]    /* сохранённый ESP (S-4) */
        add edx, 4                       /* исходный ESP (S) */
        movzx eax, byte ptr [esp + 28]   /* сохранённый AL */
        push edx                         /* arg2: esp кадра */
        push eax                         /* arg1: al */
        call battle_trace_tick_end
        add esp, 8
        mov edx, dword ptr [esp + 12]    /* arg1: esp кадра (S) */
        add edx, 4
        movzx eax, byte ptr [esp + 28]   /* arg0: al */
        push edx
        push eax
        call battle_capture_bdd0_exit
        add esp, 8
        popad
        popfd
        mov ecx, dword ptr [esp + 0x14c] /* украденная инструкция */
        jmp dword ptr [g_h2_cont]
    }
}

/* ------------------------------------------------------------------ */
/* H7 — 0x45dd70: 83 EC 60 (sub esp,0x60) | 53 (push ebx) |            */
/*                8B 5C 24 68 (mov ebx,[esp+0x68]); продолжение         */
/*                0x45dd78. Счётчик vis общий с портом s6_ai_update.     */
/* ------------------------------------------------------------------ */
__declspec(naked) void h7_visit_hook(void)
{
    __asm {
        pushfd
        pushad
        call battle_trace_visit
        popad
        popfd
        sub esp, 0x60
        push ebx
        mov ebx, dword ptr [esp + 0x68]
        jmp dword ptr [g_h7_cont]
    }
}

/* ------------------------------------------------------------------ */
/* H3 — 0x44cbc5: 16 Б = `C7 84 24 C0 00 00 00 00 00 00 00`            */
/*                 (movl $0,0xc0(%esp)) + `E8 0B 23 01 00`             */
/*                 (call 0x45eee0); продолжение 0x44cbd5.               */
/*                                                                     */
/* v9: обёртка capture 45eee0 (battle_capture_eee0_enter/exit) вокруг  */
/* вызова 0x45eee0 с сохранением всех регистров и флагов.               */
/* ------------------------------------------------------------------ */
__declspec(naked) void h3_code_hook(void)
{
    __asm {
        mov dword ptr [esp + 0xc0], 0     /* украденная инструкция 1 */
        push eax
        push edx
        push ecx
        push dword ptr [esp + 12]         /* arg2: rec ([esp] на входе) */
        push ecx                          /* arg1: BattleAI* self */
        call battle_capture_eee0_enter
        add esp, 8
        pop ecx
        pop edx
        pop eax
        call dword ptr [g_h3_callee]      /* украденная инструкция 2 (0x45eee0) */
        push eax
        push edx
        push ecx
        push esi                          /* arg1: BattleAI* self */
        push eax                          /* arg0: return code EAX */
        call battle_capture_eee0_exit
        add esp, 8
        pop ecx
        pop edx
        pop eax
        pushfd                            /* ESP = S-4; флаги post-call */
        pushad                            /* ESP = S-36 */
        push esi                          /* arg2: &unit+0x138 (BattleAI) */
        push eax                          /* arg1: код */
        call battle_trace_code
        add esp, 8                        /* ESP = S-32 */
        popad                             /* ESP = S-4; EAX = код */
        popfd                             /* ESP = S; флаги post-call */
        jmp dword ptr [g_h3_cont]         /* ESP = S == native @0x44cbd5 */
    }
}
