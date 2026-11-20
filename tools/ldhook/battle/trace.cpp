/* battle/trace.cpp — наблюдатели боевого тика и trace-строка (п. 1.7 плана).
 *
 * Одна строка на вызов FUN_0044bdd0 = один тик одного юнита. strока пишется
 * на эпилоге 0x44d074 (H2); H1 на 0x44bdf5 назначает tick/ordinal и сбрасывает
 * аккумуляторы тика; H7 на 0x45dd70 (и вход порта s6_ai_update) считает vis.
 *
 * Дисциплина трамплина (re/battle_ai_ab_method.md §2): pushfd+pushad, вызов
 * impl, popad/popfd, затем украденные байты дословно, затем jmp на продолжение.
 * ESP при jmp равен оригинальному; H1 восстанавливает флаги для `je`; `flds`
 * на H1 — нативная инструкция и выполняется ровно один раз.
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
#include "structs.h"
#include "trace.h"

/* Оффсеты, которых нет в struct BattleAI (размер зафиксирован только до
 * +0x7c): размер map1 и размер map2 читаются эпилогом 44bdd0. */
#define TRACE_UNIT_OFF_AI    0x138   /* unit → BattleAI* */
#define TRACE_UNIT_OFF_U170  0x170   /* unit → dispatch-ветка */
#define TRACE_UNIT_OFF_M0    0xa4    /* unit → 3 float-бита результата 45e1d0 */
#define TRACE_UNIT_OFF_M2    0xac
#define TRACE_UNIT_ESP_OUT   0x164   /* [esp+0x164] → param_3 (out-код) */
#define TRACE_AI_OFF_S1      0x70    /* BattleAI → размер map1 */
#define TRACE_AI_OFF_S2      0x7c    /* BattleAI → размер map2 */

#define TRACE_UNITS          64      /* ordinal'ы юнитов: ptr → id */

/* Продолжения/цели ветвлений трамплинов (заполняет ldhook.cpp при установке). */
void *g_h1_cont = NULL;   /* 0x44bdff */
void *g_h1_je   = NULL;   /* 0x44be58 — цель нативного `je` */
void *g_h2_cont = NULL;   /* 0x44d07b */
void *g_h7_cont = NULL;   /* 0x45dd78 */

static int  g_enabled = 0;
static volatile LONG g_tick_no = 0;
static int  g_tick_unit_id = -1;
static volatile LONGLONG g_tick_t0 = 0;
static volatile LONG g_vis = 0;

/* Аккумуляторы вызова e0a0 (H4); поля i1/i2/endpoint'ов пока sentinel'ы —
 * значения будут подключены хуками Фазы 3/4. Счётчик нужен уже сейчас,
 * чтобы H4 реально выполнял сброс на входе функции. */
static volatile LONG g_e0a0_calls = 0;
static volatile LONG g_e0a0_iters = 0;
static int g_ep_first_valid = 0;
static int g_ep_last_valid = 0;

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

void __cdecl battle_trace_tick_begin(void *unit)
{
    if (!g_enabled) {
        return;
    }
    g_tick_unit_id = trace_unit_id(unit);
    InterlockedIncrement(&g_tick_no);
    g_tick_t0 = ld_now_us();
    g_vis = 0;
    g_e0a0_calls = 0;
    g_e0a0_iters = 0;
    g_ep_first_valid = 0;
    g_ep_last_valid = 0;
}

/* Строка пишется ДО воспроизведения украденных байт эпилога. */
void __cdecl battle_trace_tick_end(int al, void *unit, void *esp)
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
    void *ai_ptr = NULL;
    int trap = 0;

    /* Поля следующих фаз — sentinel'ы (решение 2.0 плана). */
    const int  code = -1;
    const int  c1 = -1, x1 = -1, y1 = -1;
    const long i1 = -1L;
    const int  fx = -1, fy = -1, lx = -1, ly = -1;
    const int  c2 = -1, x2 = -1, y2 = -1;
    const long i2 = -1L;
    const long g = -1L, h10k = -1L, cost = -1L;
    const int  trk = -1;
    const unsigned int bd2 = 0xFFFFFFFFu;

    if (!g_enabled) {
        return;
    }

    tick = g_tick_no;
    unit_id = g_tick_unit_id;

    __try {
        if (unit != NULL) {
            u170 = *(const int *)((const unsigned char *)unit +
                                  TRACE_UNIT_OFF_U170);
            m0 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M0);
            m1 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M0 + 4);
            m2 = *(const unsigned int *)((const unsigned char *)unit +
                                         TRACE_UNIT_OFF_M2);
            ai_ptr = *(void * const *)((const unsigned char *)unit +
                                       TRACE_UNIT_OFF_AI);
            if (ai_ptr != NULL) {
                ai = (unsigned int)(size_t)ai_ptr;
                s1 = *(const long *)((const unsigned char *)ai_ptr +
                                     TRACE_AI_OFF_S1);
                s2 = *(const long *)((const unsigned char *)ai_ptr +
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

void __cdecl battle_trace_e0a0_enter(int filter)
{
    if (!g_enabled) {
        return;
    }
    g_e0a0_calls++;
    g_e0a0_iters = 0;
    g_ep_first_valid = 0;
    g_ep_last_valid = 0;
    (void)filter;
}

/* ------------------------------------------------------------------ */
/* H1 — 0x44bdf5: 8B E9 (mov ebp,ecx) | 74 5F (je 0x44be58) |          */
/*                 D9 85 14 01 00 00 (flds [ebp+0x114]); продолжение   */
/*                 0x44bdff. Флаги для `je` — от TEST AL,AL @0x44bdef. */
/* ------------------------------------------------------------------ */
__declspec(naked) void h1_tick_hook(void)
{
    __asm {
        pushfd
        pushad
        push ecx                     /* unit */
        call battle_trace_tick_begin
        add esp, 4
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
/*                 продолжение 0x44d07b. AL — возврат 44bdd0, EBP —     */
/*                 unit, [esp+0x164] — param_3.                         */
/* ------------------------------------------------------------------ */
__declspec(naked) void h2_epilogue_hook(void)
{
    __asm {
        pushfd
        pushad
        mov edx, dword ptr [esp + 12]    /* сохранённый ESP (S-4) */
        add edx, 4                       /* исходный ESP (S) */
        mov ecx, dword ptr [esp + 8]     /* сохранённый EBP = unit */
        movzx eax, byte ptr [esp + 28]   /* сохранённый AL */
        push edx                         /* arg3: esp кадра */
        push ecx                         /* arg2: unit */
        push eax                         /* arg1: al */
        call battle_trace_tick_end
        add esp, 12
        popad
        popfd
        mov ecx, dword ptr [esp + 0x14c] /* украденная инструкция */
        add esp, 8                       /* 2 nops закрывают 5f/5e @0x44d07b/7c */
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
