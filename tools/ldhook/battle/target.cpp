/* battle/target.cpp — цикл поиска цели FUN_0045e0a0, его callees и хуки сайтов
 * (site4 cap, site5 диагностика, site6 полный detour). Код перенесён 1:1 из
 * ldhook.cpp; обращения к полям — через battle/structs.h.
 *
 * Установка хуков (install_site4/5/6) — в ldhook.cpp; трамплины (s4/s5/s6_*_hook)
 * определяются здесь, они объявлены в ldhook_internal.h.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "trace.h"

/* ------------------------------------------------------------------ */
/* site4 — cap итераций цикла поиска цели (FUN_0045e0a0): тормоза боя. */
/*                                                                    */
/* Сайт цикла (установленный EXE, проверено 18.11.2026):               */
/*   0x45e117 верх итерации; хвост 0x45e163:                          */
/*     mov esi,eax (2) | cmp esi,ebp (2) | jne 0x45e117 (2) = 6 байт; */
/*   0x45e0a0 пролог: mov eax,[esp+0x10] (4) | sub esp,0x34 (3)=7 Б;  */
/*   0x45e169 штатный выход цикла (pop ebx; ...; ret 0x10).           */
/*                                                                    */
/* Патология (docs/PERF_BATTLE.md): до ~4400 итераций, 71–73 мс; норма */
/* ~2 итерации. Cap ограничивает итерации и выходит штатным «не        */
/* найдено»: eax=esi=0 -> 0x45e169 (та же точка, что у обычного        */
/* NULL-выхода цикла: вызывающий обрабатывает «цель не выбрана»).      */
/*                                                                    */
/* Счётчик итераций сбрасывается на ВХОДЕ функции (второй крошечный    */
/* хук): иначе после «найдено» остаток счётчика переезжает на следующий */
/* вызов. Если FUN_0045e0a0 окажется реентерабельной, внутренний вызов */
/* просто сбросит счётчик (cap ослабнет, но не ужесточится).           */
/* Cap: env LDHOOK_CAP (дефолт 32; 0 = хук не ставится, чистый A/B).    */
/* Код мёртвый (site4 не вызывается); снятие — Фаза 3 плана.            */
/* ------------------------------------------------------------------ */

#define S4_HEAVY_ITER  20        /* > нормы (~2): считаем вызов тяжёлым */
#define S4_CAP_DEFAULT 32

volatile LONG g_site4_calls = 0;  /* вызовов FUN_0045e0a0 */
volatile LONG g_site4_caps = 0;   /* срабатываний cap */
volatile LONG g_site4_heavy = 0;  /* вызовов с >S4_HEAVY_ITER итерациями */
volatile LONG g_site4_max = 0;    /* максимум итераций на вызов */
volatile LONG g_site4_cap = S4_CAP_DEFAULT;
static volatile LONG g_site4_iter = 0;   /* итерации текущего вызова */
static volatile LONG g_site4_dbg = 0;
static volatile LONG g_site4_cap_dbg = 0;

void *g_site4_cont = NULL;        /* entry+7: продолжение пролога */
void *g_site4_top = NULL;         /* верх цикла (цель jne) */
void *g_site4_nat = NULL;         /* tail+6: штатный выход цикла */

/* Вход функции: зафиксировать итерации прошлого вызова и сбросить счётчик. */
static void __cdecl s4_entry_impl(void)
{
    LONG prev = g_site4_iter;

    InterlockedIncrement(&g_site4_calls);
    if (prev > g_site4_max) {
        g_site4_max = prev;
    }
    if (prev > S4_HEAVY_ITER) {
        InterlockedIncrement(&g_site4_heavy);
    }
    g_site4_iter = 0;
    if (ld_rate(&g_site4_dbg, 4, 512)) {
        ld_debug("site4: entry #%ld prev_iter=%ld max=%ld heavy=%ld caps=%ld cap=%ld",
                 (long)g_site4_calls, prev, (long)g_site4_max,
                 (long)g_site4_heavy, (long)g_site4_caps, g_site4_cap);
    }
}

/* Cap сработал: выход «цель не найдена». */
static void __cdecl s4_cap_impl(void)
{
    LONG caps = InterlockedIncrement(&g_site4_caps);

    if (caps <= 3 || (caps % 128) == 0) {
        ld_info("site4: cap %ld fired (cap=%ld, call #%ld)",
                caps, g_site4_cap, (long)g_site4_calls);
    }
}

/* Трамплин входа: повторяет украденные 7 байт и уходит в entry+7. */
__declspec(naked) void s4_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        call s4_entry_impl
        pop ecx
        pop edx
        pop eax
        mov eax, dword ptr [esp + 0x10]
        sub esp, 0x34
        jmp dword ptr [g_site4_cont]
    }
}

/* Трамплин хвоста: повторяет mov/cmp, затем либо штатный выход (esi==0),
 * либо счёт итераций с проверкой cap. Cap -> eax=esi=0 -> штатный выход. */
__declspec(naked) void s4_tail_hook(void)
{
    __asm {
        mov esi, eax
        cmp esi, ebp
        jne count_iter
        jmp dword ptr [g_site4_nat]
    count_iter:
        inc dword ptr [g_site4_iter]
        mov eax, dword ptr [g_site4_iter]
        cmp eax, dword ptr [g_site4_cap]
        jbe loop_again
        xor eax, eax
        xor esi, esi
        push eax
        push edx
        push ecx
        call s4_cap_impl
        pop ecx
        pop edx
        pop eax
        jmp dword ptr [g_site4_nat]
    loop_again:
        mov eax, esi
        jmp dword ptr [g_site4_top]
    }
}

LONG s4_parse_cap(void)
{
    const char *s = getenv(S4_CAP_ENV);
    long v;

    if (s == NULL || *s == '\0') {
        return S4_CAP_DEFAULT;
    }
    v = strtol(s, NULL, 10);
    if (v < 0) {
        v = 0;
    }
    if (v > 100000) {
        v = 100000;
    }
    return (LONG)v;
}

/* ------------------------------------------------------------------ */
/* site5 — диагностика поиска цели (FUN_0045e0a0): логирование.       */
/*                                                                    */
/* Заменяет cap-подход site4. На входе и каждой итерации цикла        */
/* пишет в hdlog.txt: аргументы, кандидата, метрику [obj+0x24],       */
/* порог расстояния [this+0x34], состояние трекинга [this+0x30].      */
/*                                                                    */
/* Уровни логирования:                                                */
/*   LDHOOK_LOG=1 (дефолт): только «first call» + сводка;            */
/*   LDHOOK_LOG=2 (debug): первые 4 итерации + каждый 256-й;          */
/*                                                                    */
/* Сайты: пролог 0x45e0a0 (7 Б) + хвост цикла 0x45e163 (6 Б) —       */
/* те же сигнатуры, что у site4.                                       */
/* ------------------------------------------------------------------ */

#define S5_HEAVY_ITER  20

volatile LONG g_site5_calls = 0;
volatile LONG g_site5_heavy = 0;
volatile LONG g_site5_max = 0;
volatile LONG g_site5_iter = 0;
static volatile LONG g_site5_dbg = 0;
static volatile LONG g_site5_idbg = 0;

/* Замер времени нативного цикла: стек t0 (вход — push, выход цикла — pop),
 * сумма/максимум микросекунд, сумма итераций. Сводка — каждую 64-ю сводку. */
static LONGLONG g_s5_t0[16];
static volatile LONG g_s5_depth = 0;
static volatile LONGLONG g_s5_us_sum = 0;
static volatile LONGLONG g_s5_us_max = 0;
static volatile LONG g_s5_iter_sum = 0;
/* Фаза 4 A/B: сводка по окнам времени (5 с) нативного цикла. */
#define S5_WINDOW_US 5000000
static volatile LONG g_s5_win_calls = 0;
static volatile LONGLONG g_s5_win_us = 0;
static volatile LONGLONG g_s5_win_max = 0;
static volatile LONG g_s5_win_iter = 0;
static LONGLONG g_s5_win_t = 0;

void *g_site5_cont = NULL;
void *g_site5_top = NULL;
void *g_site5_nat = NULL;

/* Вход: фиксирует статистику прошлого вызова, сбрасывает счётчик. */
static void __cdecl s5_entry_impl(void *this_, void *arg0, void *arg1,
                                   void *arg2, void *arg3)
{
    BattleAI *self = (BattleAI *)this_;
    LONG n = InterlockedIncrement(&g_site5_calls);
    LONG prev = g_site5_iter;
    int trace;

    /* H4: вход FUN_0045e0a0 — сброс счётчиков вызова для trace. */
    battle_trace_e0a0_enter((int)(INT_PTR)arg0);

    if (prev > g_site5_max) {
        g_site5_max = prev;
    }
    if (prev > S5_HEAVY_ITER) {
        InterlockedIncrement(&g_site5_heavy);
    }
    g_site5_iter = 0;
    InterlockedAdd(&g_s5_iter_sum, prev);

    /* t0 на входе (стек по глубине вложенности: dd70 → e0a0). */
    {
        LONG d = InterlockedIncrement(&g_s5_depth);
        if (d >= 1 && d <= 16) {
            g_s5_t0[d - 1] = ld_now_us();
        }
    }

    if (n == 1) {
        ld_info("site5: first call (hook FIRED)");
    }

    /* Первые 4 вызова — всегда (info); далее — троттлинг (debug). */
    if (n <= 4) {
        /* always log at info level */
    } else {
        trace = ld_rate(&g_site5_dbg, 0, 128);
        if (!trace || g_log_level < LD_DEBUG) {
            return;
        }
    }

    {
        unsigned char tracking = self->tracking;
        int threshold = (int)self->distance_threshold;
        void *stored = self->anchor;
        void *list_head = self->cand_head;

        if (n <= 4)
            ld_info("site5: entry #%ld this=%p a0=%p a1=%p a2=%p a3=%p "
                     "list=%p trk=%d thr=%d stored=%p prev_iter=%ld",
                     (long)n, this_, arg0, arg1, arg2, arg3,
                     list_head, (int)tracking, threshold, stored, (long)prev);
        else
            ld_debug("site5: entry #%ld this=%p a0=%p a1=%p a2=%p a3=%p "
                     "list=%p trk=%d thr=%d stored=%p prev_iter=%ld",
                     (long)n, this_, arg0, arg1, arg2, arg3,
                     list_head, (int)tracking, threshold, stored, (long)prev);
    }
}

/* Трамплин входа. ecx=this, [esp]=ret, [esp+4..0x10]=arg0..arg3. */
__declspec(naked) void s5_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push dword ptr [esp + 28]    /* arg3 */
        push dword ptr [esp + 28]    /* arg2 */
        push dword ptr [esp + 28]    /* arg1 */
        push dword ptr [esp + 28]    /* arg0 */
        push ecx                     /* this */
        call s5_entry_impl
        add esp, 20
        pop ecx
        pop edx
        pop eax
        mov eax, dword ptr [esp + 0x10]
        sub esp, 0x34
        jmp dword ptr [g_site5_cont]
    }
}

/* Хвост цикла: логирует кандидата, метрику, порог, дистанцию. */
static void __cdecl s5_tail_impl(void *this_, void *candidate)
{
    BattleAI *self = (BattleAI *)this_;
    Candidate *cand = (Candidate *)candidate;
    LONG iter = InterlockedIncrement(&g_site5_iter);
    int trace;

    /* Первые 4 итерации — всегда (info); далее — троттлинг (debug). */
    if (iter <= 4) {
        /* always log at info level */
    } else {
        trace = ld_rate(&g_site5_idbg, 0, 256);
        if (!trace || g_log_level < LD_DEBUG) {
            return;
        }
    }

    if (cand == NULL) {
        if (iter <= 4)
            ld_info("site5: iter=%ld cand=NULL", (long)iter);
        else
            ld_debug("site5: iter=%ld cand=NULL", (long)iter);
        return;
    }

    {
        int filter = cand->filter;
        int metric = cand->metric;
        int x = cand->reserved_0c;
        int y = cand->reserved_10;
        int threshold = (int)self->distance_threshold;
        LDPos *stored = self->anchor;
        int tracking = self->tracking;
        int dist2 = -1;

        if (stored != NULL) {
            int dx = x - stored->x;
            int dy = y - stored->y;
            dist2 = dx * dx + dy * dy;
        }

        if (iter <= 4)
            ld_info("site5: iter=%ld cand=%p [%d,%d] f=%d m=%d d2=%d thr=%d trk=%d",
                     (long)iter, cand, x, y, filter, metric, dist2, threshold,
                     (int)tracking);
        else
            ld_debug("site5: iter=%ld cand=%p [%d,%d] f=%d m=%d d2=%d thr=%d trk=%d",
                     (long)iter, cand, x, y, filter, metric, dist2, threshold,
                     (int)tracking);
    }
}

/* Хвост цикла (выход): замер времени вызова — pop t0, сумма/максимум µs,
 * window-сводка (окно 5 с). Вызывается только при выходе из цикла
 * (cmp esi,ebp == equal), candidate = ebp (sentinel). */
static void __cdecl s5_exit_impl(void *candidate)
{
    LONG d = InterlockedDecrement(&g_s5_depth);
    LONGLONG us = 0;

    if (d >= 0 && d < 16) {
        us = ld_now_us() - g_s5_t0[d];
    }
    InterlockedAdd64(&g_s5_us_sum, us);
    if (us > g_s5_us_max) {
        g_s5_us_max = us;
    }
    /* Окно 5 с: вызовы, µs, итерации за период (нативный цикл). */
    {
        LONGLONG now = ld_now_us();
        InterlockedIncrement(&g_s5_win_calls);
        InterlockedAdd64(&g_s5_win_us, us);
        InterlockedAdd(&g_s5_win_iter, g_site5_iter);
        if (us > g_s5_win_max) {
            g_s5_win_max = us;
        }
        if (g_s5_win_t == 0) {
            g_s5_win_t = now - us;
        }
        if (now - g_s5_win_t >= S5_WINDOW_US) {
            ld_info("site5: window dt=%I64d calls=%ld iter=%ld us_sum=%I64d us_max=%I64d total=%ld",
                    now - g_s5_win_t, (long)g_s5_win_calls, (long)g_s5_win_iter,
                    (LONGLONG)g_s5_win_us, (LONGLONG)g_s5_win_max, (long)g_site5_calls);
            g_s5_win_calls = 0;
            g_s5_win_us = 0;
            g_s5_win_max = 0;
            g_s5_win_iter = 0;
            g_s5_win_t = now;
        }
    }
}

/* Эпилог функций-выходов (add esp,0x34; ret 10h) — все выходы e0a0
 * (sentinel, code=1/4/3) идут через эти 6-байтовые копии внутри функции.
 * Хук: s5_exit_impl(candidate); затем повторяет эпилог. */
__declspec(naked) void s5_epi_hook(void)
{
    __asm {
        push eax
        push ecx
        push edx
        push eax
        call s5_exit_impl
        add esp, 4
        pop edx
        pop ecx
        pop eax
        add esp, 0x34
        ret 0x10
    }
}

/* Трамплин хвоста: s5_tail_impl(this, candidate), затем mov/cmp/jne. */
__declspec(naked) void s5_tail_hook(void)
{
    __asm {
        push eax                    /* save candidate */
        push ecx                    /* save ecx */
        push eax                    /* arg1: candidate */
        push edi                    /* arg0: this */
        call s5_tail_impl
        add esp, 8
        pop ecx
        pop eax                     /* restore candidate */
        mov esi, eax
        cmp esi, ebp
        jne s5_loop_again
        jmp dword ptr [g_site5_nat]

    s5_loop_again:
        jmp dword ptr [g_site5_top]
    }
}

/* ------------------------------------------------------------------ */
/* site6 — Фаза 3 (docs/PLAN_BATTLE_AI_CPP.md): полная замена (detour)  */
/* поиска цели FUN_0045e0a0 + скана FUN_0045ce80 на C++ в .pyd.        */
/* Статус: микро-цикл ≤2 — временный; faithful-цикл, снятие fallback'а  */
/* и нативная регистрация слотов — Фазы 2–3 плана.                     */
/*                                                                    */
/* Горячий путь: O(итерации × n) native (до ~4400 полных сканов дерева */
/* на поиск) → один проход s6_scan_map на скан. Логика 1:1 с оригиналом*/
/* (дизасм Ghidra, подтверждён на установленном EXE):                  */
/* Принято (site6_correctness_audit.md): native-цикл НЕ вырожден — FUN_0045c4b0 */
/* (mask 4) помечает cur->filter=3 и помеченные исключаются следующим скане    */
/* (ротация кандидатов). Нормальный выход — code=4 на 2-й итерации;           */
/* патологический — code=3 по таймауту. s6_target_search — микро-цикл 2        */
/* итераций: scan + ai_update + marking + гейты на каждой; таймаут заменён      */
/* ограниченным циклом (cost O(2n)).                                             */
/*   FUN_0045e0a0(this, param_1, param_2, param_3, param_4):           */
/*     if (param_4) *param_4 = 0;                                     */
/*     cur = scan(this, param_1);                                     */
/*     while (cur) {        // порт — микро-цикл, максимум 2 итерации */
/*       FUN_0045dd70(this, cur, param_2);   // AI update (порт)      */
/*       FUN_0045c4b0(&local, cur, 4);       // cur->filter = 3       */
/*       if (*(int*)cur == param_2) { *param_4=1; return cur; }       */
/*       if (FUN_0045c880(this, cur)) { *param_4=4; return cur; }     */
/*       if (FUN_0045c5a0(this, param_3)) { *param_4=3; return cur; } */
/*       cur = scan(this, param_1);   // помеченные filter=3 вне скана*/
/*     }                                                              */
/*     return NULL;      // скан пуст: code=0, записи в *param_4 нет   */
/*   FUN_0045ce80(this, filter): один in-order проход std::map this+0x6c*/
/*     ((TreeNode*)this->[0x6c] = sentinel; obhod ot head->left);     */
/*     фильтр [obj+8]==filter, минимум [obj+0x24], возврат value1.     */
/*                                                                    */
/* Оригинальные предикаты/AI-update НЕ вызываются — портированы (выше):*/
/*   FUN_0045dd70 = s6_ai_update (порт: сетка 3×3 + lower_bound map this+0x74,*/
/*     поднимает метрику кандидата [obj+0x24] → scan возвращает другой объект),*/
/*   FUN_0045c480 = s6_distance2, FUN_0045c880 = s6_check_distance,     */
/*   FUN_0045c5a0 = s6_check_timeout. Убирает краш (оригиналы ждут SEH). */
/* Гейт: env LDHOOK_SITE6 (дефолт 0 = выкл); 1/true = поставить хук.     */
/* ------------------------------------------------------------------ */

#define S6_HEAVY_ITER  20        /* > нормы (~2): считаем вызов тяжёлым */

volatile LONG g_site6_calls = 0;
volatile LONG g_site6_heavy = 0;
volatile LONG g_site6_max = 0;
static volatile LONG g_site6_iter = 0;    /* сканов (итераций) в прошлом вызове */
static volatile LONG g_site6_dbg = 0;
static volatile LONG g_site6_depth = 0;   /* защита от рекурсии: dd70 → e0a0 → site6 */
static volatile LONGLONG g_s6_us_sum = 0; /* Фаза 4 A/B: сумма/максимум микросекунд */
static volatile LONGLONG g_s6_us_max = 0;
/* Фаза 4 A/B: сводка по окнам времени (5 с): вызовы и µs за период,
 * пишется на выходе из вызова при закрытии окна. */
#define S6_WINDOW_US 5000000
static volatile LONG g_s6_win_calls = 0;
static volatile LONGLONG g_s6_win_us = 0;
static volatile LONGLONG g_s6_win_max = 0;
static LONGLONG g_s6_win_t = 0;           /* начало текущего окна (µs) */
static volatile LONG g_s6_visit = 0;      /* счётчик визитов dd70 (metric rotation) */

PFN_timeGetTime g_s6_gettime = NULL;      /* кэш timeGetTime из IAT EXE */

/* Один in-order проход std::map (порт FUN_0045ce80): this+0x6c = sentinel.
 * Фильтр [obj+8]==filter, минимум беззнакового [obj+0x24]; возврат LDObject*. */
LDObject *s6_scan_map(void *this_, int filter)
{
    TreeNode *head = ((BattleAI *)this_)->cand_head;
    TreeNode *cur;
    TreeNode *best = head;
    unsigned int best_metric = 999999999u;
    LDObject *obj;
    int steps = 0;

    if (head == NULL) {
        return NULL;
    }
    cur = head->left;                    /* первый (левый) узел дерева */
    while (cur != head && cur != NULL) {
        /* Защита от повреждённого дерева: не больше 10000 шагов. */
        if (++steps > 10000) {
            return NULL;
        }
        obj = (LDObject *)cur->value;
        if (obj != NULL && obj->filter == filter) {
            unsigned int m = (unsigned int)obj->metric;
            if (m < best_metric) {
                best_metric = m;
                best = cur;
            }
        }
        cur = s6_tree_next(cur);
    }
    if (best != head) {
        return (LDObject *)best->value;
    }
    return NULL;
}

/* FUN_0045c480 — distance²: (a->x−b->x)² + (a->y−b->y)², коорд. +0x0c/+0x10.
 * 32-битная арифметика (как IMUL/ADD оригинала): младшие 32 бита знакового
 * произведения; сравнение в c880 — беззнаковое (JA). */
int s6_distance2(void *a, void *b)
{
    int dx = *(int *)((unsigned char *)a + 0x0c) -
             *(int *)((unsigned char *)b + 0x0c);
    int dy = *(int *)((unsigned char *)a + 0x10) -
             *(int *)((unsigned char *)b + 0x10);
    return (int)((unsigned int)dx * (unsigned int)dx +
                 (unsigned int)dy * (unsigned int)dy);
}

/* FUN_0045c880 — предикат дистанции (thiscall, RET 4, AL; порт 1:1).
 * this+0x30 (байт tracking): 0 → return 0 (cand не разыменовывается).
 * Иначе dist² между cand->pos (LDObject->[0]) и якорем this->[0x2c]:
 * dist² > this->[0x34] (unsigned) → this->[0x2c]=cand->pos, return 1;
 * иначе this->[0x34]=dist², return 0. Все разыменования под SEH. */
int s6_check_distance(void *this_, void *cand)
{
    BattleAI *self = (BattleAI *)this_;
    __try {
        unsigned char tracking = self->tracking;
        LDPos *anchor;
        LDObject *obj;
        LDPos *pos;
        unsigned int thr;
        int dist2;

        if (tracking == 0) {
            return 0;                       /* трекинг выключен → cand не трогаем */
        }
        anchor = self->anchor;              /* this->[0x2c] — якорь (LDPos*) */
        obj = (LDObject *)cand;
        pos = obj->pos;                     /* cand->[0] */
        if (pos == NULL) {
            return 0;
        }
        dist2 = s6_distance2(pos, anchor);  /* c480(pos, anchor) */
        thr = self->distance_threshold;
        if ((unsigned int)dist2 > thr) {
            self->anchor = pos;             /* this->[0x2c] = pos */
            return 1;                       /* предикат сработал → выход code=4 */
        }
        self->distance_threshold = (unsigned int)dist2;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;                           /* битые указатели → не нашли */
    }
}

/* FUN_0045c5a0 — таймаут (thiscall, RET 4, AL; x87; порт 1:1). arg = float-
 * порог (4 Б, передан по значению). ms = timeGetTime() (IAT base+0xec27c);
 * сек = (double)ms*0.001 (коррекция знака 2^32); elapsed = сек − this->[0x4]
 * (float); return elapsed > порог (NaN → 1). Под SEH. */
int s6_check_timeout(void *this_, void *arg)
{
    BattleAI *self = (BattleAI *)this_;
    __try {
        unsigned long ms;
        double sec;
        float start, threshold, elapsed;

        ms = g_s6_gettime ? g_s6_gettime() : (unsigned long)GetTickCount();
        sec = (double)(unsigned long)ms * 0.001;   /* мс→сек (unsigned-коррекция) */
        start = self->timer_start_sec;             /* this->[0x4] — старт (сек) */
        memcpy(&threshold, &arg, sizeof(float));   /* порог (float по значению) */
        elapsed = (float)sec - start;
        if (elapsed != elapsed) {                  /* NaN → timeout */
            return 1;
        }
        return (elapsed > threshold) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

/* FUN_0045c4b0(&local, cur, 4) — port (mask bit 2: local[2]=3 → cur[2]).
 * Помеченный кандидат cur->filter (obj+0x08) = 3 выпадает из скана ce80
 * ([obj+8]==filter, filter=1) → ротация кандидатов цикла. Разметка transient:
 * s6_target_search восстанавливает сохранённый filter на выходе из вызова
 * (net state = как до вызова; native-слой Python сбрасывает filter между
 * AI-раундами, порт без восстановления исчерпывал скан → calls падали).
 * site6_correctness_audit.md,Claim 2: «мёртвый код» опровергнут байткодом. */
void s6_mark_filter(LDObject *obj)
{
    if (obj == NULL) {
        return;
    }
    __try {
        obj->filter = 3;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* Парная к mark: вернуть obj->filter = saved (значение до разметки).
 * Разыменование под SEH. */
void s6_restore_filter(LDObject *obj, int saved)
{
    if (obj == NULL) {
        return;
    }
    __try {
        obj->filter = saved;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

/* FUN_0045dd00 — slot lookup caller (запись объекта в 2 map'а) — native-обёртка
 * native::register_occupied_slot вызывается напрямую у s6_ai_update @0x45dfa6
 * (2.6); malloc-порт s6_slot_insert удалён (malloc из .pyd в родные map без
 * rb-баланса портит кучу — defect C аудита). */
/* Чтение int-бит как float (x87 FADD/FLDS m32 с операндом из int-слота) и
 * обратное: float-биты как int (для стек-аргументов листа). */
static double s6_bits_to_float(int v)
{
    return (double)*(float *)&v;
}

static int s6_float_bits(float f)
{
    return *(int *)&f;
}

/* FUN_0045c5e0 (0x45c5e0..0x45c686, 169 байт, RET 4) — эвристика сетки
 * (п. 2.5). Оригинал без SEH; вызывается из FUN_0045c690 (тоже без SEH), а тот
 * — из FUN_0045dd70, у которого SEH есть (0x45dd70-фрейм): разыменования
 * идут под ним.
 * Карта слотов (S = ESP на входе FUN_0045c5e0, arg1 = [S+4]; push ebx/esi/edi
 * @0x45c5e3..0x45c5ee, push edi @0x45c5ee — esp = S-0x28):
 *   arg1 (S+4) = LDPos** slot вызывающего (dd70 L[0x3c], `LEA EAX,[ESP+0x3c]`
 *   @0x45dfe4); [arg1] = LDPos*; +0xc/+0x10/+0x14 от arg1 — соседние int-слоты
 *   dd70, читаемые x87 как float.
 *   S-0x20/S-0x24/S-0x28 — сохранённые EBX/ESI/EDI; S-0x1c = a1 (@0x45c5ff);
 *   S-0x18 = w120 (@0x45c624); S-0x14 = chain1 (@0x45c637 FSTP m32);
 *   rec1 out = S-0x10 (@0x45c61f); rec2 out = S-0x08 (@0x45c64a).
 *   Живая запись: float в arg1-хранилище (@0x45c658 FSTP m32 [ESP+0x40] = S+4).
 *   Она мёртвая в dd70 (проверено): dd70 пишет в L[0x3c] pos @0x45dfe0 перед
 *   следующим гейтом.
 *   0x45c67b: CALL 0x44bbf0, ECX = world; ESP на вызове = S-0x38, аргументы —
 *   [S-0x38..S-0x2c] (младший первым): arg1 = int-биты chain2 (S-0x38),
 *   arg2 = int-биты arg1-хранилища (S-0x34), arg3 = int-биты chain1 (S-0x30),
 *   arg4 = 0 (остаток `PUSH 0x0` @0x45c61d, S-0x2c). Детали — в конце тела.
 *   EAX листа — возврат pass-through (FUN_0045c690 отдаёт его как есть, dd70
 *   умножает на 10000 @0x45df7b). */
int s6_heuristic(void *this_, int *slot)
{
    char *world = *(char **)(*(char **)(char *)this_ + 4);   /* MOV EAX,[ECX]; MOV ECX,[EAX+4] */
    int obj1, obj2, obj3, obj4;
    int a1, w120;            /* S-0x1c @0x45c5ff; S-0x18 @0x45c624 */
    float chain1, chain2;    /* S-0x14 @0x45c637; S-0x38 @0x45c674 (FSTP m32) */
    LDPos *pos;
    float rec1[2], rec2[2];

    obj1 = native::vcall0(world, 0x40);            /* CALL *[EDX+0x40] @0x45c5ef */
    a1 = *(int *)(obj1 + 0x244);                   /* EAX = [obj1+0x244] @0x45c5f7 → слот S-0x1c @0x45c5ff */
    obj2 = native::vcall1(world, 0x40, a1);        /* CALL *[EDX+0x40] @0x45c603. Допущение: форма с
                                                    * одним int-аргументом (a1 в spilled-слоте S-0x1c,
                                                    * `MOV EDI,[ESP+0x2C]` @0x45c617 читает arg1-слот);
                                                    * листинг не фиксирует число аргументов vtable- call. */
    obj3 = *(int *)(obj2 + 0x228);                 /* EBX = [obj2+0x228] @0x45c606 — только receiver @0x45c63b */
    w120 = *(int *)(world + 0x120);                /* EDX @0x45c611 → слот S-0x18 @0x45c624 */

    pos = (LDPos *)*slot;                          /* ECX = [EDI] @0x45c61b */
    s6_grid_scale_compute(pos, rec1);              /* CALL 0x456b20 @0x45c628, out = S-0x10 (EAX читается @0x45c62d) */

    /* @0x45c62d..0x45c637: FLDS rec1[1]; FADD [arg1+0x14] (int-биты float'ом);
     * FSTP m32 → chain1 в слот S-0x14 (@0x45c637, esp = S-0x2c после RET 4).
     * Живой потребитель — arg3 листа: значение читается @0x45c66a (`MOV EDX,
     * [ESP+0x24]`, esp = S-0x38) и кладётся в [EBX+8] @0x45c671 = S-0x30. */
    chain1 = (float)((double)rec1[1] + s6_bits_to_float(*(int *)((char *)slot + 0x14)));

    obj4 = native::vcall0((void *)obj3, 0x48);     /* CALL *[EDX+0x48] @0x45c63b, ECX = obj3 */

    /* @0x45c63e..0x45c658: FLDS [ESP+0x10] = a1 (слот S-0x1c, записанный @0x45c5ff);
     * FMULS [obj4+0x34]; FADD [arg1+0x10] (int-биты float'ом); FADD слот S-0x18
     * = world[0x120] (@0x45c654); FSTP m32 [ESP+0x40] = float в arg1-хранилище. */
    *(float *)slot = (float)(s6_bits_to_float(a1) * *(float *)((char *)obj4 + 0x34)
                             + s6_bits_to_float(*(int *)((char *)slot + 0x10))
                             + s6_bits_to_float(w120));

    s6_grid_scale_compute(pos, rec2);              /* CALL 0x456b20 @0x45c65c, out = S-0x08 (EAX читается @0x45c668) */
    /* @0x45c661..0x45c674: EBX = S-0x38 (@0x45c64e `MOV ESP,EBX`); FLDS [arg1+0xc];
     * FADD rec2[0]; FSTP m32 [EBX] = chain2 в S-0x38; MOV [EBX+4] @0x45c66e —
     * bits arg1-хранилища (`MOV ECX,[ESP+0x3C]` @0x45c664, esp = S-0x38);
     * MOV [EBX+8] @0x45c671 — chain1. Эти слоты [S-0x38..S-0x2c] и есть
     * 4 стек-аргумента листа @0x45c67b (esp = S-0x38, лист — `RET 0x10`). */
    chain2 = (float)((double)*(float *)((char *)slot + 0xc) + (double)rec2[0]);

    /* @0x45c67b: ESP = S-0x38, ECX = world; лист — 4 стек-аргумента
     * (`ADD ESP,0x28; RET 0x10` @0x44bceb/0x44bcee), лежащие в [S-0x38..S-0x2c]
     * (младший первым): arg1 = int-биты chain2 (@0x45c674 FSTP [EBX]);
     * arg2 = int-биты arg1-хранилища (@0x45c66e MOV [EBX+4]); arg3 = int-биты
     * chain1 (@0x45c671 MOV [EBX+8] ← значение @0x45c66a); arg4 = 0 (остаток
     * `PUSH 0x0` @0x45c61d в S-0x2c). Риск: лист зовёт _free (0x4d0e16
     * @0x44bcbd/0x44bcd1) на указателях из аргументов. */
    return native::grid_query(world, s6_float_bits(chain2), *(int *)slot,
                               s6_float_bits(chain1), 0);
}

/* FUN_0045c690 (0x45c690..0x45c6e7, 90 байт, RET 4) — гейт эвристики (п. 2.5).
 * this_ = BattleAI (ECX), slot = LDPos** (arg1 dd70 `LEA EAX,[ESP+0x3c]` @0x45dfe4).
 * Возврат — результат FUN_0045c5e0 как есть (не 0/1; хвост @0x45c6d7..0x45c6df
 * отдаёт EAX листа без нормализации — поправка артефакта разведки, который
 * описывал выход как «1»); 0 — только на гейтах.
 *   0x45c693..0x45c6a3: w0 = [this_+0x00] (vtable ctx); EAX = [w0+4];
 *   byte [EAX+0x110] != 0 → 0.
 *   0x45c6a5..0x45c6ae: EDI = arg1; EAX = [EDI] = pos; EDX = [pos+0x10] (game-Y),
 *   ESI = [pos+0xc] (game-X).
 *   0x45c6b1..0x45c6c2: ECX = [ECX] = w0 (vtable ctx, не this_); (w0>>2)&1 → хвост;
 *   иначе ((w0>>3)&1)==0 → 0.
 *   0x45c6c4..0x45c6d2: PUSH ESI; PUSH EDX; PUSH &RECT(this_+0x18); ECX = RECT;
 *   CALL *IAT 0x4ec26c (PtInRect). POINT.x = game-Y (+0x10), POINT.y = game-X
 *   (+0xc) — по PUSH-порядку (y,x). Вне rect → 0; внутри → хвост.
 *   0x45c6d4..0x45c6d7: PUSH EDI; ECX = this_; tail CALL 0x45c5e0. */
int s6_heuristic_gate(void *this_, int *slot)
{
    char *w0 = *(char **)(char *)this_;
    char *world = *(char **)(w0 + 4);
    int vt;
    LDPos *pos;

    if (*(unsigned char *)(world + 0x110) != 0) {   /* @0x45c699..0x45c6a3 JNE → return 0 */
        return 0;
    }
    pos = (LDPos *)*slot;                           /* @0x45c6a9: EAX = [EDI] */
    vt = (int)*(void **)w0;                         /* @0x45c6b1: MOV ECX,[ECX] — vtable ctx (w0), не this_ */
    if (((vt >> 2) & 1) == 0) {                     /* @0x45c6ba JNE → хвост при бите 2 */
        if (((vt >> 3) & 1) == 0) {                 /* @0x45c6c2 JE → return 0 */
            return 0;
        }
        if (!native::pt_in_rect((const int *)&((BattleAI *)this_)->canvas_rect,
                                pos->y, pos->x)) {  /* pt_x = game-Y (+0x10), pt_y = game-X (+0xc) — раскладка POINT */
            return 0;                               /* @0x45c6d2 JE → return 0 */
        }
    }
    return s6_heuristic(this_, slot);               /* @0x45c6d4..0x45c6d7 tail CALL */
}

/* Clamp dd70 (@0x45dd78..0x45de08): FILD level->[0x2c] (size); FILD(v);
 * FCOMP st(1); при v >= size — FSUB [0x4ec380] и CALL 0x4d17e0 (ftol),
 * результат size−1; при v < size — fstp, v остаётся. Итог: min(v, size−1).
 * Значения констант измерены по baseline EXE (VA 0x4ec37c/0x4ec380 = file
 * offsets 0xec37c/0xec380, 4 байта, little-endian float): [0x4ec37c] =
 * 0.0f, [0x4ec380] = 1.0f. */
static int s6_grid_clamp(int size, int v)
{
    if (v >= size) {
        return size - 1;                    /* FSUB 1.0f + ftol (@0x45ddca..0x45ddd5) */
    }
    return v;                               /* fstp (@0x45dddf) */
}

/* FUN_0045c4b0 (thiscall, RET 0xc) — copy-по-маске src→dst, сигнатура
 * (src, dst, mask); каллеры dd70 @0x45df9e/@0x45e03b, src = base spec-окна
 * L[0x3c] (@0x45df91/0x45e02e `lea 0x44(esp)` при ESP−4 от push-пар).
 * Bit-table 0x45c4cc..0x45c557: бит0→+0x00, бит1→+0x04, бит2→+0x08,
 * бит3→+0x0c, бит4→+0x10, бит5→+0x14, бит6→+0x18; бит7 — знаковый тест
 * `TEST %DL,%DL JNS` (@0x45c518) → +0x1c; DH-порядок: DH.1 (бит9)→+0x24
 * (@0x45c522), DH.0 (бит8)→+0x20 (@0x45c52d), DH.2 (бит10)→+0x28
 * (@0x45c538), DH.3 (бит11)→+0x2c, DH.4 (бит12)→+0x30. Маски dd70:
 * ветка 1 — 0x1fff (@0x45df89 push), ветка 2 — 0x3c6 = {+0x04,+0x08,
 * +0x14,+0x20,+0x24,+0x28} (@0x45e01e push). */
static void s6_copy_fields(int dst, int src_, unsigned int mask)
{
    int *d = (int *)(INT_PTR)dst;
    const int *s = (const int *)(INT_PTR)src_;

    if ((mask & 0x0001) != 0) d[0]  = s[0];   /* бит0 → +0x00 */
    if ((mask & 0x0002) != 0) d[1]  = s[1];   /* бит1 → +0x04 */
    if ((mask & 0x0004) != 0) d[2]  = s[2];   /* бит2 → +0x08 */
    if ((mask & 0x0008) != 0) d[3]  = s[3];   /* бит3 → +0x0c */
    if ((mask & 0x0010) != 0) d[4]  = s[4];   /* бит4 → +0x10 */
    if ((mask & 0x0020) != 0) d[5]  = s[5];   /* бит5 → +0x14 */
    if ((mask & 0x0040) != 0) d[6]  = s[6];   /* бит6 → +0x18 */
    if ((int)(mask << 24) < 0) d[7] = s[7];   /* бит7: TEST %DL,%DL JNS (@0x45c518) → +0x1c */
    if ((mask & 0x0200) != 0) d[9]  = s[9];   /* DH.1 (бит9) → +0x24 (@0x45c522) */
    if ((mask & 0x0100) != 0) d[8]  = s[8];   /* DH.0 (бит8) → +0x20 (@0x45c52d) */
    if ((mask & 0x0400) != 0) d[10] = s[10];  /* DH.2 (бит10) → +0x28 (@0x45c538) */
    if ((mask & 0x0800) != 0) d[11] = s[11];  /* DH.3 (бит11) → +0x2c */
    if ((mask & 0x1000) != 0) d[12] = s[12];  /* DH.4 (бит12) → +0x30 */
}

/* Spec-окно dd70: int[13] в PathState-форме (L[0x3c..0x6c, поля +0x00..+0x30])
 * плюс out-слот lower_bound L[0x38] (base−4). Посев (@0x45de51..0x45dea1):
 * fields +0x00/+0x04/+0x08/+0x18/+0x1c/+0x20/+0x24/+0x30 ← esi (nbr
 * предыдущей итерации; 0 при первой), слоты +0x0c/+0x10/+0x14 ← 0
 * (@0x45de81..0x45de91), сентинелы +0x28 = 0xcb18967f (−1e7), +0x2c =
 * 0x4b18967f (+1e7) (@0x45de99/0x45dea1). */

/* FUN_0045dd70 — AI/pathfinding update (thiscall RET 8, ~807 байт; хит-патч
 * целой функции 0x45dd73). this = BattleAI (ecx), cur = PathState* (arg1,
 * слот L[0x74]), arg2 = target LDPos* (слот L[0x78]). Рамка: base U =
 * entry−112 (`sub esp,0x60` @0x45dd70 + push ebx/ebp/esi/edi). Порядок тела
 * по листингу game_dis.txt 0x45dd70..0x45e096 (ESP-выводка re/esp_scan.py):
 *   Clamp @0x45dd78..0x45de08: x+1, y+1 по level->[0x2c]/[0x30] — min(v,
 *   size−1) (константы [0x4ec37c]=0.0f, [0x4ec380]=1.0f, baseline EXE).
 *   x_start = max(x−1, 0) (@0x45dd85 lea −1, @0x45dda5 JGE/0x45dda7 XOR);
 *   x-счётчик @0x45de08 `mov %ebp,%edx` → L[0x18]; @0x45de0e JG 0x45e08d —
 *   старт > clamp(ш) → цикл пропускается. y_start = max(y−1,0) (@0x45dd91,
 *   @0x45ddad store); y-счётчик L[0x10] @0x45de2c; self-skip (@0x45de36/
 *   0x45de3c: x==pos->x && y==pos->y).
 *   idx = 3·(x−x_start) + (y−y_start) — row-major по 3×3 (L[0x20] посев
 *   −y_start @0x45de18 neg, +3 внешний хвост @0x45e07a, idx = L[0x20]+
 *   y-счётчик @0x45de46, store @0x45de79); rev = 8−idx (@0x45de4c/
 *   0x45de75, store @0x45de7d). Сосед @0x45de71: cur->nbr[idx].
 *   Ветка 1 (nbr==NULL, @0x45deaf..0x45dfab): key=(short)y<<16|(short)x
 *   (@0x45deaf..0x45deba, L[0x2c]); lower_bound(map2, &L[0x38], &L[0x2c])
 *   (@0x45dec3/0x45dec7 CALL 0x434bc0); узел == occ_head → своб. (@0x45ded2);
 *   узел != head → nbr = узел+0x10 (@0x45ded7), ненулевой → ветка 2
 *   (@0x45dedc JNE), нулевой → своб. путь; rows[x][y] (@0x45dee2..0x45def1)
 *   + CALL 0x456ed0 (@0x45def5), cell NULL → хвост цикла (@0x45defc JE
 *   0x45e05a); spec[0x00]=cell (@0x45df05), spec[0x10]=cur->[0x10] (@0x45df13),
 *   CALL cb40 (push pos, push &spec; @0x45df17 RET 8), результат NULL →
 *   хвост (@0x45df24 JE 0x45e056); ctor pfs (@0x45df2c CALL 0x45c2e0, игровая
 *   куча operator new 0x58); spec[0x00]=результат (@0x45df39), spec[0x04]=
 *   cur (@0x45df4a), spec[0x08]=cur->filter (@0x45df43/0x45df4e), spec[0x0c]=
 *   0 (@0x45df52), spec[0x14]=0 (@0x45df5a), spec[0x18]=g+1 (@0x45df62, INC
 *   @0x45df46); dist² = CALL 0x45c480(результат, target) (@0x45df47/0x45df40
 *   push, @0x45df66), store spec[0x1c] (@0x45df6b); h = CALL 0x45c690(&spec)
 *   (@0x45df6f/0x45df73 push, @0x45df76); spec[0x20]=h·10000 (@0x45df85);
 *   cost = h·10000 + dist² + (g+1) (@0x45df7b imul 0x2710, @0x45df8e,
 *   @0x45df95), store spec[0x24] (@0x45df9a); copy(&spec, pfs, 0x1fff)
 *   (@0x45df89..@0x45df9e CALL 0x45c4b0); native-регистрация pfs (@0x45dfa3
 *   ..0x45dfa6 CALL 0x45dd00) — только в ветке 1; @0x45dfab JMP 0x45e040 —
 *   ветка 2 в ветке 1 не выполняется.
 *   Ветка 2 (@0x45dfb0..0x45e03b): гейт cur.filter==2 (@0x45dfb0) и
 *   nbr.filter!=3 (@0x45dfba/0x45dfc0); рекурсия dd70(this, nbr, this+0x2c)
 *   при nbr.filter==1 (@0x45dfd2); spec[0x00]=nbr->pos (@0x45dfe0), spec[0x10]
 *   =nbr->[0x10] (@0x45dfe7), h2 = CALL 0x45c690(&spec) (@0x45dfeb),
 *   spec[0x18]=g+1 (@0x45e003, INC @0x45e002), spec[0x08]=cur->filter
 *   (@0x45e00a), spec[0x20]=h2·10000 (@0x45e011), spec[0x04]=cur (@0x45e015),
 *   dist² = CALL 0x45c480(nbr->pos, target) (@0x45e009/0x45e00e push,
 *   @0x45e019), store spec[0x1c] (@0x45e025); cost2 = h2·10000 + dist² +
 *   spec[0x18] (@0x45dffc imul/0x45e023/0x45e029/0x45e032), store spec[0x24]
 *   (@0x45e037); copy(&spec, nbr, 0x3c6) (@0x45e01e..@0x45e03b).
 *   Связка (@0x45e040..0x45e050): cur->nbr[idx]=nbr; nbr->nbr[8−idx]=cur.
 * cb40 (FUN_0045cb40, RET 8, 2 стек-арга pos/&spec, arg2 не читается): ESP
 *   на call = U−8 (pushes @0x45df04/0x45df10), entry = U−12, `sub esp,0x3c`
 *   + 4 push → cb40 base Y = U − 88; слот cb40 k ↔ dd70 slot (k − 88),
 *   alias'а L[0x34]/L[0x3c] нет. loop_base L[0x1c]=−y_radius пишется
 *   @0x45cbe9 до чтения @0x45cc86; min_slot L[0x24]=−x_radius пишется
 *   @0x45cbca до чтения @0x45cd1f; чтение L[0x20] @0x45cbad — слот,
 *   записанный @0x45cb71 — stale-read'ов в cb40 нет (see path.cpp).
 * Разыменования — под harness-__try (рамка 0x44bdd0 в игре). */
int s6_ai_update(void *this_, void *cand, int arg2)
{
    BattleAI *self = (BattleAI *)this_;
    PathState *cur = (PathState *)cand;
    LDPos *pos;
    LDPos *target = (LDPos *)(INT_PTR)arg2;             /* L[0x78] (@0x45df31/0x45dff0) */
    GridLevel *level;
    int w[14];                        /* w[0]=L[0x38] out-слот; sp=&w[1]=L[0x3c] spec-окно */
    int x_start, x_max, y_start, y_max;
    int row_off, seed;
    int x, y;

    /* H7: тот же счётчик vis, что у нативного 0x45dd70 (в порт-режиме он не
     * вызывается). Рекурсия s6_ai_update считает каждый вход, как native. */
    battle_trace_visit();

    if (cur == NULL) {
        return 0;
    }

    __try {
        pos = cur->pos;                                       /* @0x45dd7f */
        level = (GridLevel *)(INT_PTR)*(const int *)((const unsigned char *)self->game_ctx + 8);
                                                              /* grid root @0x45dee4/0x45dee7 */

        x_start = s6_imax(pos->x - 1, 0);                     /* @0x45dd85/0x45dda5 */
        x_max   = s6_grid_clamp(level->size_x, pos->x + 1);   /* @0x45dd8c..0x45ddd9 */
        y_start = s6_imax(pos->y - 1, 0);                     /* @0x45dd91/0x45dda9 */
        y_max   = s6_grid_clamp(level->size_y, pos->y + 1);   /* @0x45dde1..0x45ddfc */

        if (x_start > x_max) {                                /* @0x45de0e JG 0x45e08d */
            return 1;
        }

        w[0] = 0;
        seed = 0;                                             /* esi @0x45dd99 xor */
        for (x = x_start; x <= x_max; x++) {                  /* @0x45e079/0x45e07d */
            row_off = 3 * (x - x_start);                      /* L[0x20] += 3 @0x45e07a */
            for (y = y_start; y <= y_max; y++) {              /* @0x45e064/0x45e06b */
                PathState *nbr;
                int *sp = &w[1];                              /* spec base = L[0x3c] */
                int idx = row_off + (y - y_start);            /* @0x45de46, store @0x45de79 */
                int rev = 8 - idx;                            /* @0x45de4c/0x45de75, store @0x45de7d */
                int dist2, h, cost;
                int free_done = 0;

                if (x == pos->x && y == pos->y) {            /* self-skip @0x45de36/0x45de3c */
                    continue;
                }

                /* Посев spec-окна (@0x45de51..0x45dea1: fields ← esi, 0, сентинелы). */
                sp[0] = seed; sp[1] = seed; sp[2] = seed;
                sp[3] = 0; sp[4] = 0; sp[5] = 0;            /* @0x45de81..0x45de91 */
                sp[6] = seed; sp[7] = seed; sp[8] = seed; sp[9] = seed;
                sp[10] = 0xcb18967f; sp[11] = 0x4b18967f;   /* @0x45de99/0x45dea1 */
                sp[12] = seed;

                nbr = cur->nbr[idx];                          /* @0x45de71 */

                if (nbr == NULL) {
                    int key = (int)((unsigned short)y << 16) | (unsigned short)x;  /* @0x45deaf..0x45deba */
                    GridCell *cell;
                    void *path_res;

                    native::lower_bound_slots(&self->map2, &w[0], &key);  /* @0x45dec7 out=&L[0x38], @0x45dec3 key=&L[0x2c] */
                    if ((void *)(INT_PTR)w[0] != self->occ_head) {        /* @0x45ded2/0x45dedc: узел != head → занят */
                        nbr = (PathState *)(INT_PTR)((const TreeNode *)(INT_PTR)w[0])->value;  /* узел+0x10 @0x45ded7 */
                    }
                    if (nbr == NULL) {                        /* своб. путь (head или узел с нулевым значением) */
                        cell = s6_grid_lookup(level, x, y);   /* @0x45dee2..0x45def1 + CALL 0x456ed0 */
                        if (cell == NULL) {
                            seed = 0; continue;               /* @0x45defc JE → хвост цикла (xor esi @0x45e05a) */
                        }
                        sp[0] = (int)(INT_PTR)cell;           /* spec[0x00]=cell @0x45df05 */
                        sp[4] = *(const int *)&cur->out_metric;  /* spec[0x10]=cur->[0x10] @0x45df13 */
                        path_res = s6_path_search(self, (PathState *)sp);  /* @0x45df17 RET 8 */
                        if (path_res == NULL) {
                            seed = 0; continue;               /* @0x45df24 JE → хвост цикла */
                        }
                        nbr = s6_pfs_ctor();                  /* @0x45df2c CALL 0x45c2e0, куча игры */
                        if (nbr == NULL) {
                            /* harness-guard: native продолжает со spec[0]=0, copy
                             * dst NULL (c4b0 guard), register obj NULL, связка
                             * nbr->nbr[rev] → AV под SEH-рамкой; порт пропускает
                             * итерацию (deviation зафиксирован). */
                            seed = 0; continue;
                        }
                        sp[0] = (int)(INT_PTR)path_res;       /* spec[0x00]=результат @0x45df39 */
                        sp[1] = (int)(INT_PTR)cur;           /* spec[0x04]=cur @0x45df4a */
                        sp[2] = cur->filter;                  /* spec[0x08] @0x45df43/0x45df4e */
                        sp[3] = 0;                            /* spec[0x0c] @0x45df52 */
                        sp[5] = 0;                            /* spec[0x14] @0x45df5a */
                        sp[6] = cur->g_cost + 1;              /* spec[0x18] @0x45df62 (INC @0x45df46) */
                        dist2 = s6_distance2((void *)(INT_PTR)path_res, target);  /* CALL 0x45c480 @0x45df66 */
                        sp[7] = dist2;                        /* spec[0x1c] @0x45df6b */
                        h = s6_heuristic_gate(self, sp);      /* CALL 0x45c690 @0x45df76, &spec @0x45df6f */
                        sp[8] = h * 10000;                    /* spec[0x20] @0x45df85 */
                        cost = h * 10000 + dist2 + (cur->g_cost + 1);  /* @0x45df7b/0x45df8e/0x45df95 */
                        sp[9] = cost;                         /* spec[0x24] store @0x45df9a */
                        s6_copy_fields((int)(INT_PTR)nbr, (int)(INT_PTR)sp, 0x1fff);  /* src=&spec @0x45df91, CALL 0x45c4b0 @0x45df9e */
                        native::register_occupied_slot(self, (LDObject *)nbr);  /* CALL 0x45dd00 @0x45dfa6 — только ветка 1 */
                        free_done = 1;                        /* @0x45dfab JMP 0x45e040 */
                    }
                }

                if (!free_done) {                             /* ветка 2 (@0x45dfb0..0x45e037) */
                    if (cur->filter == 2 && nbr->filter != 3) {  /* @0x45dfb0/0x45dfba/0x45dfc0 */
                        if (nbr->filter == 1) {               /* @0x45dfc6 */
                            s6_ai_update(this_, nbr, (int)(INT_PTR)self->anchor);  /* @0x45dfd2 CALL 0x45dd70, arg2=[this+0x2c] */
                        }
                        sp[0] = (int)(INT_PTR)nbr->pos;       /* spec[0x00] @0x45dfe0 */
                        sp[4] = *(const int *)&nbr->out_metric;  /* spec[0x10] = nbr->[0x10] @0x45dfe7 */
                        h = s6_heuristic_gate(self, sp);      /* CALL 0x45c690 @0x45dfeb */
                        sp[6] = cur->g_cost + 1;             /* spec[0x18] @0x45e003 (INC @0x45e002) */
                        sp[2] = cur->filter;                  /* spec[0x08] @0x45e00a */
                        sp[8] = h * 10000;                    /* spec[0x20] @0x45e011 */
                        sp[1] = (int)(INT_PTR)cur;            /* spec[0x04] @0x45e015 */
                        dist2 = s6_distance2(nbr->pos, target);  /* CALL 0x45c480 @0x45e019 */
                        sp[7] = dist2;                        /* spec[0x1c] @0x45e025 */
                        cost = h * 10000 + dist2 + sp[6];     /* @0x45dffc/0x45e023/0x45e029/0x45e032 */
                        sp[9] = cost;                         /* spec[0x24] store @0x45e037 */
                        s6_copy_fields((int)(INT_PTR)nbr, (int)(INT_PTR)sp, 0x3c6);  /* src=&spec @0x45e02e, CALL 0x45c4b0 @0x45e03b */
                    }
                }

                cur->nbr[idx] = nbr;                          /* @0x45e04c store %ESI */
                nbr->nbr[rev] = cur;                          /* @0x45e050 store %EBX */
                seed = (int)(INT_PTR)nbr;                     /* esi после связки */
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    (void)arg2;
    return 1;
}

/* Замена FUN_0045e0a0 (cdecl; вызывается из трамплина). МИКРО-ЦИКЛ 2 ИТЕРАЦИЙ.
 * Порт цикла оригинала (site6_correctness_audit.md): FUN_0045c4b0(mask 4)
 * помечает cur->filter=3, помеченные выпадуют из скана — native-цикл ротирует
 * кандидатов; нормальный выход code=4 — на 2-й итерации, патологический —
 * code=3 по таймауту (c5a0). Таймаут заменён ограничением цикла 2 итерации
 * (cost O(2n)): 2 итерации без гейта → code=3 с текущим cur. Пустой скан:
 * выход без гейтов, code=0, *out_code остаётся 0 (entry-запись), return NULL. */
static void *__cdecl s6_target_search(void *this_, void *arg0, void *arg1,
                                      void *arg2, void *arg3)
{
    int filter = (int)(int)arg0;
    void *cmp_val = arg1;                /* target pos_obj (сравнение с cur->pos) */
    int *out_code = (int *)arg3;
    int code = 0;
    LDObject *cur;
    LONG n = InterlockedIncrement(&g_site6_calls);
    LONG iters = 0;
    int iter;
    LONGLONG t0 = 0;                       /* Фаза 4 A/B: точка отсчёта */
    /* Разметка transient: сохранённые filter помеченных кандидатов цикла
     * (макс. 2 пометки — цикл ≤2 итераций); восстановление — на выходе из
     * вызова, после последнего скана и до возврата управляющему коду. */
    LDObject *marked[2];
    int saved_filter[2];
    int nmark = 0;

    (void)arg2;                          /* порог таймаута не читается: таймаут = граница цикла */

    /* H4: вход FUN_0045e0a0 (детур) — сброс счётчиков вызова для trace. */
    battle_trace_e0a0_enter(filter);

    if (out_code != NULL) {
        *out_code = 0;
    }

    /* Защита от рекурсии: FUN_0045dd70 (AI/pathfinding) внутри вызывает
     * FUN_0045e0a0, который перехвачен site6. Без защиты — stack overflow.
     * При рекурсивном вызове возвращаем NULL (цель не найдена на этом уровне),
     * внешний цикл продолжит работу. */
    if (InterlockedIncrement(&g_site6_depth) > 1) {
        if (ld_rate(&g_site6_dbg, 4, 64)) {
            ld_debug("site6: recursive call (depth=%ld) -> return NULL",
                     (long)g_site6_depth);
        }
        InterlockedDecrement(&g_site6_depth);
        return NULL;
    }

    if (n == 1) {
        ld_info("site6: first call (hook FIRED) this=%p filter=%d cmp=%p "
                "layout_ld=%d layout_node=%d",
                this_, filter, cmp_val, (int)ld_off_ok, (int)ld_off_ok2);
    }

    t0 = ld_now_us();                      /* Фаза 4 A/B: время вызова */

    cur = s6_scan_map(this_, filter);
    if (ld_rate(&g_site6_dbg, 4, 64)) {
        ld_debug("site6: call %ld scan -> %p (filter=%d cmp=%p depth=%ld)",
                 (long)n, (void *)cur, filter, cmp_val, (long)g_site6_depth);
    }

    /* Микро-цикл — тело оригинального while, максимум 2 итерации. На каждой:
     * ai_update + marking (cur->filter=3 → ротация скана) + гейты в родном
     * порядке pos==cmp → code=1; c880 → code=4. Гейты проверяются на КАЖДОМ
     * кандидате, как в native (нормальный выход — code=4 на 2-й итерации). */
    for (iter = 1; iter <= 2; iter++) {
        if (cur == NULL) {
            break;                         /* скан пуст: code=0 (entry-запись), return NULL */
        }
        iters = iter;
        /* AI/pathfinding update — порт FUN_0045dd70: arg2 = arg1 (pos_obj),
         * см. 0x45e117 push ebx. */
        s6_ai_update(this_, cur, (int)(int)cmp_val);
        /* FUN_0045c4b0(&local, cur, 4): cur->filter = 3 — помеченный
         * кандидат выпадает из скана ce80 (native-ротация). Сохраняем
         * старый filter (под SEH), восстановление — на выходе из вызова. */
        if (nmark < 2) {
            __try {
                marked[nmark] = cur;
                saved_filter[nmark] = cur->filter;
                nmark++;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        s6_mark_filter(cur);
        if (cur->pos == (LDPos *)cmp_val) {
            code = 1;
            break;
        }
        if (s6_check_distance(this_, cur)) {
            code = 4;                      /* c880 — типичный native-выход, итерация 2 */
            break;
        }
        cur = s6_scan_map(this_, filter);  /* rescan: помеченные (filter=3) исключены */
    }
    if (code == 0 && cur != NULL) {
        code = 3;                          /* 2 итерации без гейта: timeout-выход оригинала */
    }

    /* Восстановление сохранённых filter: разметка transient внутри вызова —
     * net state объекта = как до вызова. После последнего скана (помеченные
     * были исключены из rescan), до возврата управляющему коду. */
    {
        int i;
        for (i = 0; i < nmark; i++) {
            s6_restore_filter(marked[i], saved_filter[i]);
        }
    }

    if (out_code != NULL) {
        *out_code = code;
    }

    InterlockedDecrement(&g_site6_depth);

    /* Статистика. */
    g_site6_iter = iters;
    if (iters > g_site6_max) {
        g_site6_max = iters;
    }
    if (iters > S6_HEAVY_ITER) {
        InterlockedIncrement(&g_site6_heavy);
    }
    {
        LONGLONG now = ld_now_us();        /* Фаза 4 A/B: длительность вызова */
        LONGLONG us = now - t0;
        InterlockedAdd64(&g_s6_us_sum, us);
        if (us > g_s6_us_max) {
            g_s6_us_max = us;
        }
        ld_debug("site6: call %ld iters=%ld max=%ld heavy=%ld us=%I64d -> %p code=%d",
                 (long)n, (long)iters, (long)g_site6_max,
                 (long)g_site6_heavy, us, (void *)cur, code);
        /* Окно 5 с: вызовы и µs за период. Окно закрывается первым вызовом
         * после его конца (простой период виден по dt). */
        InterlockedIncrement(&g_s6_win_calls);
        InterlockedAdd64(&g_s6_win_us, us);
        if (us > g_s6_win_max) {
            g_s6_win_max = us;
        }
        if (g_s6_win_t == 0) {
            g_s6_win_t = t0;
        }
        if (now - g_s6_win_t >= S6_WINDOW_US) {
            ld_info("site6: window dt=%I64d calls=%ld us_sum=%I64d us_max=%I64d total=%ld",
                    now - g_s6_win_t, (long)g_s6_win_calls,
                    (LONGLONG)g_s6_win_us, (LONGLONG)g_s6_win_max, (long)n);
            g_s6_win_calls = 0;
            g_s6_win_us = 0;
            g_s6_win_max = 0;
            g_s6_win_t = now;
        }
    }
    return (void *)cur;
}

/* Трамплин: вход — E9 из пролога FUN_0045e0a0 (7 Б), ecx=this,
 * [esp]=ret, [esp+4..0x10]=arg0..arg3. Полная замена: оригинальный код
 * не исполняется. Результат в eax, калlee (трамплин) чистит 0x10 (RET 0x10). */
__declspec(naked) void s6_entry_hook(void)
{
    __asm {
        push eax
        push edx
        push ecx
        push dword ptr [esp + 28]    /* arg3 */
        push dword ptr [esp + 28]    /* arg2 */
        push dword ptr [esp + 28]    /* arg1 */
        push dword ptr [esp + 28]    /* arg0 */
        push ecx                     /* this */
        call s6_target_search
        add esp, 20                  /* cdecl: снять 5 аргументов */
        pop ecx
        pop edx
        add esp, 4                   /* пропустить сохранённый eax (результат уже в eax) */
        ret 0x10                     /* оригинальный хвост: thiscall, 4 арг */
    }
}
