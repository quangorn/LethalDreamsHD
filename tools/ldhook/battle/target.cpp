/* battle/target.cpp — цикл поиска цели FUN_0045e0a0, его callees и хуки сайтов
 * (site5 диагностика, site6 полный detour). Код перенесён 1:1 из ldhook.cpp;
 * обращения к полям — через battle/structs.h.
 *
 * Установка хуков (install_site5/6) — в ldhook.cpp; трамплины (s5/s6_*_hook)
 * определяются здесь, они объявлены в ldhook_internal.h.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "capture.h"
#include "native.h"
#include "trace.h"

#ifdef BATTLE_REPLAY
/* Харнесс подменяет нативные листья захваченными результатами: см.
 * tools/probes/battle_replay.cpp. В .pyd-сборке макроса нет — ветка мертва. */
extern "C" int g_replay_override;
extern "C" int replay_leaf_next(int kind);
extern "C" void replay_note_rescan(void *cand);
extern "C" void replay_note_exit(void *node, int code, int iters);
/* v5: реальный прогон листьев — только observers, без подмены результата. */
extern "C" void replay_note_heur(int value);
extern "C" void replay_note_timeout(int value);
extern "C" unsigned long replay_timeout_ms(void);
extern "C" unsigned int replay_world_vt(void);
#endif

/* ------------------------------------------------------------------ */
/* site5 — диагностика поиска цели (FUN_0045e0a0): логирование.       */
/*                                                                    */
/* На входе и каждой итерации цикла пишет в hdlog.txt: аргументы,     */
/* кандидата, метрику [obj+0x24],                                      */
/* порог расстояния [this+0x34], состояние трекинга [this+0x30].      */
/*                                                                    */
/* Уровни логирования:                                                */
/*   LDHOOK_LOG=1 (дефолт): только «first call» + сводка;            */
/*   LDHOOK_LOG=2 (debug): первые 4 итерации + каждый 256-й;          */
/*                                                                    */
/* Сайты: пролог 0x45e0a0 (7 Б) + хвост цикла 0x45e163 (6 Б) —       */
/* те же сигнатуры, что у site6 (S5_ и S6_ — одни и те же байты).      */
/* ------------------------------------------------------------------ */

#define S5_HEAVY_ITER  20

static int s6_scan_count(void *this_, int filter);   /* ниже: полный проход map1 */

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

    /* H4: вход FUN_0045e0a0 — сброс счётчиков вызова для trace (слот 1/2).
     * Capture: полный вход вызова (аргументы + состояние BattleAI). */
    battle_trace_e0a0_enter(this_, (int)(INT_PTR)arg0);
    battle_capture_e0a0_enter(this_, (int)(INT_PTR)arg0, arg1, arg2, arg3);

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
        trace = ld_rate(&g_site5_dbg, 0, 8);
        if (!trace || g_log_level < LD_DEBUG) {
            return;
        }
    }
    ld_info("site5: map1 f0=%d f1=%d f2=%d f3=%d",
            s6_scan_count(this_, 0), s6_scan_count(this_, 1),
            s6_scan_count(this_, 2), s6_scan_count(this_, 3));

    {
        unsigned char tracking = self->tracking;
        int threshold = (int)self->distance_threshold;
        void *stored = self->anchor;
        void *list_head = self->cand_head;

        if (n <= 4)
            ld_info("site5: entry #%ld this=%p a0=%p a1=%p a2=%p a3=%p "
                     "list=%p trk=%d thr=%d stored=%p prev_iter=%ld "
                     "map1=%d map2=%d",
                     (long)n, this_, arg0, arg1, arg2, arg3,
                     list_head, (int)tracking, threshold, stored, (long)prev,
                     (int)self->reserved_70,
                     (int)*(const int *)((const unsigned char *)this_ + 0x7c));
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
    LONG iter;
    int trace;

    /* H6: латч цикла — EAX сайта 0x45e163 (кандидат следующей итерации или
     * NULL при исчерпании скана). До троттлинга лога: счётчик trace от уровня
     * лога не зависит. */
    battle_trace_e0a0_latch(candidate);
    battle_capture_latch(candidate);

    iter = InterlockedIncrement(&g_site5_iter);

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
        int px = -1, py = -1;

        if (stored != NULL) {
            int dx = x - stored->x;
            int dy = y - stored->y;
            dist2 = dx * dx + dy * dy;
        }
        __try {
            if (cand->pos != NULL) {
                px = cand->pos->x;
                py = cand->pos->y;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            px = -2;
            py = -2;
        }

        if (iter <= 4)
            ld_info("site5: iter=%ld cand=%p [%d,%d] f=%d m=%d d2=%d thr=%d trk=%d"
                     " pos=%p px=%d py=%d",
                     (long)iter, cand, x, y, filter, metric, dist2, threshold,
                     (int)tracking, (void *)cand->pos, px, py);
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

    /* H5: выход e0a0 (все эпилоги add esp,0x34; ret 10h) — EAX = узел-возврат
     * (NULL при пустом скане); поля узла и [this+0x38] → trace-слот вызова. */
    battle_trace_e0a0_exit(candidate);
    battle_capture_e0a0_exit(candidate);

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
/*                                                                    */
/* Faithful-цикл (п. 3.1), тело 0x45e0a0..0x45e1c0 портировано 1:1:     */
/*   FUN_0045e0a0(this, filter, target_pos, timeout, param_4):         */
/*     if (param_4) *param_4 = 0;                                     */
/*     cur = scan(this, filter);                                      */
/*     while (cur) {                                                  */
/*       FUN_0045dd70(this, cur, target_pos);   // AI/pathfinding      */
/*       FUN_0045c4b0(&local{+8=3}, cur, 4);    // cur->filter = 3      */
/*       if (cur->pos == target_pos) { *param_4=1; return cur; }      */
/*       if (FUN_0045c880(this, cur)) { *param_4=4; return cur; }     */
/*       if (FUN_0045c5a0(this, timeout)) { *param_4=3; return cur; } */
/*       cur = scan(this, filter);   // помеченные filter=3 вне скана   */
/*     }                                                              */
/*     return NULL;   // пустой скан: code=0, записи в *param_4 нет     */
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

/* Разбор env-флага (дефолт 0 = выкл): непустое числовое значение != 0 либо
 * ведущий `t`/`T` (true/TRUE) — `atoi("true")` даёт 0, поэтому префикс
 * разбирается отдельно. Значение кэшируется на первом вызове — env в рантайме
 * не меняется. */
static int ld_env_flag(const char *name, int dflt)
{
    const char *env = getenv(name);

    if (env == NULL || *env == '\0') {
        return dflt;
    }
    if (*env == 't' || *env == 'T') {
        return 1;
    }
    return (atoi(env) != 0);
}

/* Гейт порта site6 — единственный источник истины для S6_ENV. Читается
 * install_site6 (ldhook.cpp), initldhook и диспетчером e0a0 (battle/move.cpp). */
int s6_port_enabled(void)
{
#ifdef BATTLE_REPLAY
    return 1;
#else
    static int on = -1;

    if (on < 0) {
        on = ld_env_flag(S6_ENV, 0);
    }
    return on;
#endif
}

/* Гейт detour'а 45eee0 (install_tick_hook) — LDHOOK_SITE6_EEE0; единственный
 * источник истины для него. Дефолт = мастер LDHOOK_SITE6 (s6_port_enabled()),
 * поэтому без новой переменной old-поведение сохранено: SITE6=1 ставит оба
 * detour'а. SITE6=1 + SITE6_EEE0=0 = порт e0a0 на месте, 45eee0 нативный.
 * Ключевое свойство: решения НЕ пересозависимы — detour 45eee0 влияет только
 * на install_tick_hook и на выбор порт/натив для 45eee0 (tick.cpp R17),
 * решение e0a0 (site6, site5, trace H5/H6) по-прежнему только по S6_ENV. */
int s6_eee0_enabled(void)
{
#ifdef BATTLE_REPLAY
    return 1;
#else
    static int on = -1;

    if (on < 0) {
        on = ld_env_flag(S6_EEE0_ENV, s6_port_enabled());
    }
    return on;
#endif
}

volatile LONG g_site6_calls = 0;
volatile LONG g_site6_heavy = 0;
volatile LONG g_site6_max = 0;
static volatile LONG g_site6_dbg = 0;
/* Счётчики faithful-цикла (п. 3.4), читаются ldhook.stats(): сумма
 * итераций, распределение кодов 1/3/4, число ротаций (rescan после
 * не-сработавшего гейта), сумма/максимум микросекунд вызова. */
volatile LONG g_site6_iter_sum = 0;
volatile LONG g_site6_code1 = 0;
volatile LONG g_site6_code3 = 0;
volatile LONG g_site6_code4 = 0;
volatile LONG g_site6_rot = 0;
volatile LONGLONG g_s6_us_sum = 0;      /* Фаза 4 A/B: сумма/максимум микросекунд */
volatile LONGLONG g_s6_us_max = 0;
/* Фаза 4 A/B: сводка по окнам времени (5 с): вызовы и µs за период,
 * пишется на выходе из вызова при закрытии окна. */
#define S6_WINDOW_US 5000000
static volatile LONG g_s6_win_calls = 0;
static volatile LONGLONG g_s6_win_us = 0;
static volatile LONGLONG g_s6_win_max = 0;
static LONGLONG g_s6_win_t = 0;           /* начало текущего окна (µs) */
static volatile LONG g_s6_visit = 0;      /* счётчик визитов dd70 (metric rotation) */

PFN_timeGetTime g_s6_gettime = NULL;      /* кэш timeGetTime из IAT EXE */

/* Игровой таймер (native `call *0x4ec27c`, VA 0x4ec27c = timeGetTime, winmm).
 * Кэш ставит install_site6; если IAT-слот не найден — та же функция из уже
 * загруженного winmm (игра импортирует только timeGetTime, checks15 1.5(б)),
 * а не другой источник времени: GetTickCount нативным таймером не является.
 * Разрешение без malloc, результат кэшируется в g_s6_gettime. */
static PFN_timeGetTime s6_time_get(void)
{
    PFN_timeGetTime fn = g_s6_gettime;

    if (fn == NULL) {
        HMODULE mm = GetModuleHandleA("winmm.dll");
        if (mm != NULL) {
            fn = (PFN_timeGetTime)(void *)GetProcAddress(mm, "timeGetTime");
        }
        if (fn != NULL) {
            g_s6_gettime = fn;
        }
    }
    return fn;
}

unsigned long s6_time_ms(void)
{
#ifdef BATTLE_REPLAY
    /* Харнесс: часы — внешний вход (захваченный timeGetTime), порт сам
     * вычисляет решение таймаута. Подмена листа остаётся под g_replay_override. */
    if (!g_replay_override) {
        return replay_timeout_ms();
    }
#endif
    PFN_timeGetTime fn = s6_time_get();

    /* Кэш пуст и winmm не найден — невозможно: без timeGetTime EXE не
     * загрузился бы (игра импортирует только её). Часы не подменяем. */
    return (fn != NULL) ? fn() : 0UL;
}

/* ------------------------------------------------------------------ */
/* E0-диагностика faithful-цикла (env LDHOOK_E0_DIAG=1, дефолт выкл).    */
/*                                                                      */
/* Вопрос: почему порт делает больше итераций, чем натив на том же        */
/* сценарии (9.m1: e0a0 +57 % µs). Пара «уникальных кандидатов / всего   */
/* визитов» + динамика метрики одного кандидата между визитами отвечает:  */
/*   повтор + метрика изменилась   → state divergence в s6_ai_update;     */
/*   повтор + метрика та же        → скан/разметка (filter=3 не работает);*/
/*   distinct == iters, map1 растёт → порт создаёт больше кандидатов.     */
/* Кандидат помечается filter=3 ПОСЛЕ ai_update (0x45e132), поэтому визит */
/* снимается ДО ai_update: фильтр/метрика в момент, когда скан его выбрал. */
/* Строка печатается только при iters > E0_DIAG_ITERS; кап строк на прогон*/
/* (процесс) — кап объявляется одной строкой. Счётчики не пишут игровую   */
/* память: чтение [cand+0x08]/[cand+0x24] под SEH, логика цикла не тронута*/
/* ------------------------------------------------------------------ */

#define E0_DIAG_ENV   "LDHOOK_E0_DIAG"
#define E0_DIAG_TABLE 1024   /* фиксированный стек: точный счёт уникальных */
#define E0_DIAG_PROBE 8      /* линейный пробой на визит */
#define E0_DIAG_ITERS 100    /* ниже этого вызов не печатается */
#define E0_DIAG_LINES 120    /* кап строк на прогон */

typedef struct {
    void *ptr;               /* PathState* (кандидат скана) */
    LONG  count;             /* визиты за вызов (0 = слот пуст) */
    int   metric_first, metric_last;   /* [cand+0x24] */
    int   filter_first, filter_last;   /* [cand+0x08] */
} E0DiagSlot;

static volatile LONG g_e0_lines = 0;    /* строк напечатано (этот процесс) */
static volatile LONG g_e0_capped = 0;   /* 1 = кап объявлен */

static int e0_diag_on(void)
{
    static int on = -1;

    if (on < 0) {
        on = ld_env_flag(E0_DIAG_ENV, 0);
    }
    return on;
}

/* Один визит: open-addressing по указателю (без аллокаций), distinct — точный.
 * f3/f1 — фильтр кандидата в момент выбора сканом (3 = уже помечен).
 * mchg/msame — метрика того же кандидата при повторе изменилась / нет.
 * ovf — визиты, не учтённые таблицей (пробой исчерпан или таблица полна). */
static void e0_diag_visit(E0DiagSlot *tbl, LONG *distinct, LONG *ovf,
                          LONG *rep, LONG *mchg, LONG *msame,
                          LONG *f3, LONG *f1,
                          void *cand, int metric, int filter)
{
    LONG j, i, k;

    if (filter == 3) {
        (*f3)++;
    } else if (filter == 1) {
        (*f1)++;
    }
    j = (LONG)(((UINT_PTR)cand >> 4) * 2654435761u) & (E0_DIAG_TABLE - 1);
    for (k = 0; k < E0_DIAG_PROBE; k++) {
        i = (j + k) & (E0_DIAG_TABLE - 1);
        if (tbl[i].count == 0) {
            if (*distinct >= E0_DIAG_TABLE) {
                (*ovf)++;
                return;
            }
            tbl[i].ptr = cand;
            tbl[i].count = 1;
            tbl[i].metric_first = metric;
            tbl[i].metric_last = metric;
            tbl[i].filter_first = filter;
            tbl[i].filter_last = filter;
            (*distinct)++;
            return;
        }
        if (tbl[i].ptr == cand) {
            tbl[i].count++;
            (*rep)++;
            if (tbl[i].metric_last == metric) {
                (*msame)++;
            } else {
                (*mchg)++;
            }
            tbl[i].metric_last = metric;
            tbl[i].filter_last = filter;
            return;
        }
    }
    (*ovf)++;
}

/* Сводка вызова: одна строка + top-3 повторяющихся кандидата (по визитам).
 * count=0 снимается после выдачи, поэтому выбор идёт по убыванию. */
static void e0_diag_emit(long call, long iters, long distinct, long ovf,
                         long rep, long mchg, long msame,
                         long f3, long f1,
                         long nulls, int code, int map1_before, int map1_after,
                         long reg_delta, E0DiagSlot *tbl)
{
    LONG i;
    int k, top;

    if (g_e0_capped != 0) {
        return;
    }
    ld_info("E0DIAG call=%ld iters=%ld distinct=%ld rep=%ld null_returns=%ld "
            "code=%d map1=%d->%d f3=%ld f1=%ld mchg=%ld msame=%ld ovf=%ld reg=%ld",
            call, iters, distinct, rep, nulls, code, map1_before, map1_after,
            f3, f1, mchg, msame, ovf, reg_delta);
    g_e0_lines++;
    for (k = 0; k < 3; k++) {
        top = -1;
        for (i = 0; i < E0_DIAG_TABLE; i++) {
            if (tbl[i].count > 0 && (top < 0 || tbl[i].count > tbl[top].count)) {
                top = (int)i;
            }
        }
        if (top < 0) {
            break;
        }
        ld_info("E0DIAG  rep=%d cand=%p visits=%ld metric %d->%d filter %d->%d",
                k + 1, tbl[top].ptr, (long)tbl[top].count,
                tbl[top].metric_first, tbl[top].metric_last,
                tbl[top].filter_first, tbl[top].filter_last);
        g_e0_lines++;
        tbl[top].count = 0;
    }
    if (g_e0_lines >= E0_DIAG_LINES) {
        ld_info("E0DIAG %ld lines (limit %d), capped for this run",
                (long)g_e0_lines, E0_DIAG_LINES);
        g_e0_capped = 1;
    }
}

/* Диагностикa скана/регистрации (FUN_0045ce80 / FUN_0045dd00 / FUN_0045dd70)
 * — только LDHOOK_LOG=2; счётники пишутся всегда (InterlockedIncrement нейтрален),
 * лог троттлит первые 4 + каждый 128-й. Кандидатная карта map1 растёт только
 * через native::register_occupied_slot (FUN_0045dd00, ветка 1 @0x45dfa6), поэтому
 * пару «scan → рег. слот» читают вместе: скан даёт кандидата, регистрация —
 * единственный источник новых узлов. Выходы ветки 1: cell NULL, cb40 NULL,
 * pfs-ctor NULL; связка (nbr уже есть) счётчик separate. */
static volatile LONG g_scan_calls = 0;
static volatile LONG g_scan_null = 0;
static volatile LONG g_scan_found = 0;
static volatile LONG g_reg_calls = 0;
static volatile LONG g_ai_calls = 0;
static volatile LONG g_ai_cells = 0;
static volatile LONG g_ai_cell_null = 0;
static volatile LONG g_ai_path_null = 0;
static volatile LONG g_ai_pfs_null = 0;
static volatile LONG g_ai_nbr_old = 0;
static volatile LONG g_scan_dbg = 0;
static volatile LONG g_ai_degen_dbg = 0;
static volatile LONG g_ai_degen = 0;

static void s6_scan_diag(void *this_, int filter, void *res)
{
    BattleAI *self = (BattleAI *)this_;

    InterlockedIncrement(&g_scan_calls);
    if (res == NULL) {
        InterlockedIncrement(&g_scan_null);
    } else {
        InterlockedIncrement(&g_scan_found);
    }
    if (g_log_level < LD_DEBUG) {
        return;
    }
    if (ld_rate(&g_scan_dbg, 4, 128)) {
        ld_debug("scan: calls=%ld null=%ld found=%ld reg=%ld ai=%ld cells=%ld "
                 "cellnull=%ld pathnull=%ld pfsnull=%ld nbr_old=%ld "
                 "(filter=%d s1=%ld s2=%ld res=%p)",
                 (long)g_scan_calls, (long)g_scan_null, (long)g_scan_found,
                 (long)g_reg_calls, (long)g_ai_calls, (long)g_ai_cells,
                 (long)g_ai_cell_null, (long)g_ai_path_null, (long)g_ai_pfs_null,
                 (long)g_ai_nbr_old, filter, (long)self->reserved_70,
                 (long)*(const int *)((const unsigned char *)self + 0x7c), res);
    }
}

/* Один in-order проход std::map (порт FUN_0045ce80): this+0x6c = sentinel.
 * Фильтр [obj+8]==filter, минимум беззнакового [obj+0x24]; возврат LDObject*. */
LDObject *s6_scan_map(void *this_, int filter)
{
    TreeNode *head = ((BattleAI *)this_)->cand_head;
    TreeNode *cur;
    TreeNode *best = head;
    unsigned int best_metric = 999999999u;
    LDObject *obj;

    if (head == NULL) {
        return NULL;
    }
    cur = head->left;                    /* первый (левый) узел дерева */
    while (cur != head && cur != NULL) {
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
        obj = (LDObject *)best->value;
        s6_scan_diag(this_, filter, obj);
        return obj;
    }
    s6_scan_diag(this_, filter, NULL);
    return NULL;
}

/* Диагностика A/B: сколько узлов map1 несут filter==filter (полный проход;
 * только для первых вызовов e0a0 при LDHOOK_LOG>=1). */
static int s6_scan_count(void *this_, int filter)
{
    TreeNode *head = ((BattleAI *)this_)->cand_head;
    TreeNode *cur;
    int n = 0;

    if (head == NULL) {
        return 0;
    }
    cur = head->left;
    while (cur != head && cur != NULL) {
        LDObject *o = (LDObject *)cur->value;
        if (o != NULL && o->filter == filter) {
            n++;
        }
        cur = s6_tree_next(cur);
    }
    return n;
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

/* FUN_0045c5a0 (0x45c5a0..0x45c5da, RET 4, AL; x87; порт 1:1). arg = float-
 * порог (4 Б, передан по значению). Игровой таймер: `call *0x4ec27c` (IAT
 * EXE, VA 0x4ec27c = timeGetTime; game_dis.txt 131107-131131). Схема:
 *   eax = timeGetTime(); test eax,eax; fildl <int>   — знаковое 32-битное;
 *   jge → пропуск, иначе faddl 0x4ecd10 (= 2^32)      — коррекция переполнения;
 *   fmull 0x4ecd08 (= 0.001)                          — секунды (double);
 *   fsubs 0x4(%esi)                                   — − this->[0x4] (float→double);
 *   fcomps 0x8(%esp) + fnstsw/test $0x5/p            — сравнение с порогом.
 * Цепочка идёт в 80-битном x87-регистре без промежуточных округлений до
 * double, поэтому она написана инлайн-асмом: MSVC x86 в этой тулчейн-сборке
 * даёт `long double` = 64 бита (sizeof 8), т.е. C-код округлил бы fxmul/fxsub
 * до double и мог бы разойтись с нативом в последнем бите.
 * Флаги FCOM: equal → C3=C2=1, greater → 0, less → C0=1, unordered → C0=C2=1;
 * `test $0x5` (C0|C2) + `jp` → возврат 1 ⇔ elapsed ≥ порог, NaN (любой
 * операнд) → 1. Оба операнда расширяются до 80 бит (fcomps m32 → float→x87).
 * Сравнение на >= (равенство даёт 1). */
static const double s6_two32 = 4294967296.0;   /* 0x4ecd10 */
static const double s6_milli = 0.001;          /* 0x4ecd08 */

static int s6_check_timeout_body(void *this_, void *arg)
{
    BattleAI *self = (BattleAI *)this_;
    __try {
        int ms = (int)s6_time_ms();             /* mov eax,[0x4ec27c] */
        float threshold;
        int result;

        memcpy(&threshold, &arg, sizeof(float));/* порог (float по значению) */
        __asm {
            /* st0 = (double)(int)ms; fildl <int> */
            fild    dword ptr [ms]
            mov     eax, [ms]
            test    eax, eax
            jge     short s6_to_nofix          /* jge 0x45c5bc */
            fadd    qword ptr [s6_two32]       /* коррекция 2^32 */
        s6_to_nofix:
            fmul    qword ptr [s6_milli]       /* секунды (double) */
            mov     eax, [self]
            fsub    dword ptr [eax + 4]        /* − this->[0x4] */
            fcomp   dword ptr [threshold]      /* fcomps 0x8(%esp) */
            fnstsw  ax
            test    ah, 05h                    /* C0|C2 → JP */
            jp      short s6_to_true
            xor     eax, eax                   /* 0x45c5d1: xor %al,%al */
            jmp     short s6_to_done
        s6_to_true:
            mov     eax, 1                     /* 0x45c5d7: mov $0x1,%al */
        s6_to_done:
            mov     [result], eax
        }
        return result;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int s6_check_timeout(void *this_, void *arg)
{
#ifdef BATTLE_REPLAY
    if (g_replay_override) {
        return replay_leaf_next(2);
    }
    {
        int r = s6_check_timeout_body(this_, arg);
        replay_note_timeout(r);
        return r;
    }
#else
    return s6_check_timeout_body(this_, arg);
#endif
}

/* FUN_0045c4b0(&local, cur, 4) — mask bit 2 (local[2]=3 → cur[2]) inline в
 * s6_target_search @0x45e132: cur->filter (obj+0x08) = 3 — permanent-разметка,
 * помеченный кандидат выпадает из следующего скана ce80 ([obj+8]==filter,
 * filter=1) → ротация кандидатов. Восстановления нет (native так же).
 * site6_correctness_audit.md,Claim 2: «мёртвый код» опровергнут байткодом. */

/* FUN_0045dd00 — slot lookup caller (запись объекта в 2 map'а) — native-обёртка
 * native::register_occupied_slot вызывается напрямую у s6_ai_update @0x45dfa6
 * (2.6); malloc-порт s6_slot_insert удалён (malloc из .pyd в родные map без
 * rb-баланса портит кучу — defect C аудита). */
/* Диагностикa эвристики (шлюз FUN_0045c690 + FUN_0045c5e0) — только LDHOOK_LOG=2.
 * Счётники пишутся всегда (InterlockedIncrement поведенчески нейтрален), лог
 * — троттлинг первые 4 + каждый 128-й. Пути возврата шлюза: 0x110-гейт мира,
 * биты vtable-ctx, PtInRect, хвост (считается вызов c5e0); AV — дельта
 * native::av_count вокруг c5e0 (обёртки vcall0/vcall1/grid_query). */
static volatile LONG g_h_calls = 0;        /* входов в шлюз */
static volatile LONG g_h_z110 = 0;        /* мир[0x110] != 0 → 0 (@0x45c6a3) */
static volatile LONG g_h_zbits = 0;       /* биты 2/3 ctx → 0 (@0x45c6c2) */
static volatile LONG g_h_zrect = 0;       /* PtInRect = 0 (@0x45c6d2) */
static volatile LONG g_h_tail = 0;        /* хвост: CALL 0x45c5e0 (@0x45c6d7) */
static volatile LONG g_h_zero = 0;        /* шлюз вернул 0 (и на хвосте) */
static volatile LONG g_h_nonzero = 0;     /* шлюз вернул != 0 */
static volatile LONG g_h_av = 0;          /* AV, перехваченные в native-обёртках c5e0 */
static volatile LONG g_h_dbg = 0;
volatile LONG g_heur_stage = 0;           /* LOG=2: тонкая локализация AV в c690/c5e0 */

static void s6_heur_diag(int result, long av_delta)
{
    if (result != 0) {
        InterlockedIncrement(&g_h_nonzero);
    } else {
        InterlockedIncrement(&g_h_zero);
    }
    if (av_delta != 0) {
        InterlockedAdd(&g_h_av, av_delta);
    }
    if (g_log_level < LD_DEBUG) {
        return;
    }
    if (ld_rate(&g_h_dbg, 4, 128)) {
        ld_debug("heuristic: calls=%ld z110=%ld zbits=%ld zrect=%ld tail=%ld "
                 "zero=%ld nonzero=%ld av=%ld (this call r=%d av+%ld)",
                 (long)g_h_calls, (long)g_h_z110, (long)g_h_zbits, (long)g_h_zrect,
                 (long)g_h_tail, (long)g_h_zero, (long)g_h_nonzero, (long)g_h_av,
                 result, av_delta);
    }
}

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
    int arg1_bits;           /* FSTPS @0x45c658 пишет в САМ arg1-слот (S+4),
                              * а НЕ в память по указателю: spec[0] не тронут */
    int a1, w120;            /* S-0x1c @0x45c5ff; S-0x18 @0x45c624 */
    float chain1, chain2;    /* S-0x14 @0x45c637; S-0x38 @0x45c674 (FSTP m32) */
    LDPos *pos;
    float rec1[2], rec2[2];

    g_heur_stage = 10;
    obj1 = native::vcall0(world, 0x40);            /* CALL *[EDX+0x40] @0x45c5ef */
    a1 = *(int *)(obj1 + 0x244);                   /* EAX = [obj1+0x244] @0x45c5f7 → слот S-0x1c @0x45c5ff */
    obj2 = native::vcall0(world, 0x40);           /* CALL *[EDX+0x40] @0x45c603 — пушей между
                                                    * 0x45c5ff и 0x45c603 нет: форма та же, что у
                                                    * первого вызова (ECX = world, без аргументов);
                                                    * a1 — значение [obj1+0x244], не аргумент. */
    obj3 = *(int *)(obj2 + 0x228);                 /* EBX = [obj2+0x228] @0x45c606 — только receiver @0x45c63b */
    w120 = *(int *)(world + 0x120);                /* EDX @0x45c611 → слот S-0x18 @0x45c624 */

    g_heur_stage = 30;
    pos = (LDPos *)*slot;                          /* ECX = [EDI] @0x45c61b */
    g_heur_stage = 40;                             /* 456b20(pos): pos->level */
    s6_grid_scale_compute(pos, rec1);              /* CALL 0x456b20 @0x45c628, out = S-0x10 (EAX читается @0x45c62d) */

    /* @0x45c62d..0x45c637: FLDS rec1[1]; FADD [arg1+0x14] (int-биты float'ом);
     * FSTP m32 → chain1 в слот S-0x14 (@0x45c637, esp = S-0x2c после RET 4).
     * Живой потребитель — arg3 листа: значение читается @0x45c66a (`MOV EDX,
     * [ESP+0x24]`, esp = S-0x38) и кладётся в [EBX+8] @0x45c671 = S-0x30. */
    chain1 = (float)((double)rec1[1] + s6_bits_to_float(*(int *)((char *)slot + 0x14)));

    g_heur_stage = 50;
    obj4 = native::vcall0((void *)obj3, 0x48);     /* CALL *[EDX+0x48] @0x45c63b, ECX = obj3 */

    /* @0x45c63e..0x45c658: FLDS [ESP+0x10] = a1 (слот S-0x1c, записанный @0x45c5ff);
     * FMULS [obj4+0x34]; FADD [arg1+0x10] (int-биты float'ом); FADD слот S-0x18
     * = world[0x120] (@0x45c654); FSTP m32 [ESP+0x40] = float в САМ СЛОТ arg1
     * (S+4), а не в spec[0] по указателю — иначе копирование @0x45df9e кладёт
     * в nbr->pos биты float вместо(path_res) и следующий dd70 падает. */
    arg1_bits = s6_float_bits((float)(s6_bits_to_float(a1) * *(float *)((char *)obj4 + 0x34)
                                      + s6_bits_to_float(*(int *)((char *)slot + 0x10))
                                      + s6_bits_to_float(w120)));

    g_heur_stage = 60;
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
    g_heur_stage = 70;
    return native::grid_query(world, s6_float_bits(chain2), arg1_bits,
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
static int s6_heuristic_gate_body(void *this_, int *slot)
{
    char *w0 = *(char **)(char *)this_;
    char *world = *(char **)(w0 + 4);
    int vt;
    LDPos *pos;
    int r, av0, av1;

    InterlockedIncrement(&g_h_calls);
    g_heur_stage = 1;
    if (*(unsigned char *)(world + 0x110) != 0) {   /* @0x45c699..0x45c6a3 JNE → return 0 */
        InterlockedIncrement(&g_h_z110);
        s6_heur_diag(0, 0);
        return 0;
    }
    pos = (LDPos *)*slot;                           /* @0x45c6a9: EAX = [EDI] */
    g_heur_stage = 2;
#ifdef BATTLE_REPLAY
    vt = (int)(g_replay_override ? (unsigned)*(void **)w0 : replay_world_vt());
#else
    vt = (int)*(void **)w0;                         /* @0x45c6b1: MOV ECX,[ECX] — vtable ctx (w0), не this_ */
#endif
    if (((vt >> 2) & 1) == 0) {                     /* @0x45c6ba JNE → хвост при бите 2 */
        if (((vt >> 3) & 1) == 0) {                 /* @0x45c6c2 JE → return 0 */
            InterlockedIncrement(&g_h_zbits);
            s6_heur_diag(0, 0);
            return 0;
        }
        g_heur_stage = 3;
        if (!native::pt_in_rect((const int *)&((BattleAI *)this_)->canvas_rect,
                                pos->y, pos->x)) {  /* pt_x = game-Y (+0x10), pt_y = game-X (+0xc) — раскладка POINT */
            InterlockedIncrement(&g_h_zrect);
            s6_heur_diag(0, 0);
            return 0;                               /* @0x45c6d2 JE → return 0 */
        }
    }
    InterlockedIncrement(&g_h_tail);
    g_heur_stage = 4;
    av0 = (long)native::av_count();
    r = s6_heuristic(this_, slot);                  /* @0x45c6d4..0x45c6d7 tail CALL */
    av1 = (long)native::av_count();
    s6_heur_diag(r, av1 - av0);
    return r;
}

int s6_heuristic_gate(void *this_, int *slot)
{
#ifdef BATTLE_REPLAY
    if (g_replay_override) {
        return replay_leaf_next(1);
    }
    {
        int r = s6_heuristic_gate_body(this_, slot);
        replay_note_heur(r);
        return r;
    }
#else
    return s6_heuristic_gate_body(this_, slot);
#endif
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
 * +0x14,+0x20,+0x24,+0x28} (@0x45e01e push). Экспортирован для move.cpp
 * (site @0x45f20b, маска 0x1fff); порт инвертирует нативный порядок:
 * s6_copy_fields(dst, src, mask) при нативном (src, dst, mask). */
void s6_copy_fields(int dst, int src_, unsigned int mask)
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

/* LDHOOK_LOG=2: provenance координат кандидата (first 12 + every 256-й) и
 * безусловные первые 32 случая `(-1,-1)` — чтобы увидеть ПЕРВОЕ появление.
 * emb = *(cur->pos) — нативный центр cb40 (arg1 @0x45df04, чтение
 * @0x45cb4d/0x45cb4f/0x45cb52), которого нет в порту s6_path_search. */
static volatile LONG g_cell_dbg = 0;
static volatile LONG g_bad_dbg = 0;
static volatile LONG g_ai_win_dbg = 0;

static void s6_cell_diag(long ai_call, int x, int y, void *cur, void *curpos,
                         int cpx, int cpy, void *emb, int embx, int emby,
                         const char *branch, void *path_res, const int *sp,
                         void *node, void *nodepos, int npx, int npy)
{
    int bad = (cpx == -1 || cpy == -1 || embx == -1 || emby == -1 ||
               npx == -1 || npy == -1);

    if (g_log_level < LD_DEBUG) {
        return;
    }
    if ((!bad && !ld_rate(&g_cell_dbg, 12, 256)) ||
        (bad && !ld_rate(&g_bad_dbg, 32, 256))) {
        return;
    }
    ld_debug("cell: ai=%ld grid=(%d,%d) cur=%p curpos=%p (%d,%d) "
             "emb=%p (%d,%d) br=%s path=%p sp=[%08x,%08x,%08x,%08x,%08x,%08x] "
             "node=%p nodepos=%p (%d,%d)%s",
             ai_call, x, y, cur, curpos, cpx, cpy, emb, embx, emby, branch,
             path_res, sp[0], sp[1], sp[2], sp[3], sp[4], sp[5],
             node, nodepos, npx, npy, bad ? " BAD" : "");
}

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
    int cpx = -2, cpy = -2, embx = -2, emby = -2;
    void *emb = NULL;
    volatile LONG ai_stage = 0;   /* LOG=2: где порвал AV внутри 3x3 */
    ld_crash_enter(3);            /* LDHOOK_CRASHLOG: stage 3, depth++ (no-op off) */
    void *ex_path = NULL;         /* LOG=2: последний path_res для ai_ex */

    InterlockedIncrement(&g_ai_calls);
    /* H7: тот же счётчик vis, что у нативного 0x45dd70 (в порт-режиме он не
     * вызывается). Рекурсия s6_ai_update считает каждый вход, как native. */
    battle_trace_visit();

    if (cur == NULL) {
        LD_CRASH_RET(0);
    }

    __try {
        pos = cur->pos;                                       /* @0x45dd7f */
        level = (GridLevel *)(INT_PTR)*(const int *)((const unsigned char *)self->game_ctx + 8);
                                                              /* grid root @0x45dee4/0x45dee7 */
        cpx = pos->x;
        cpy = pos->y;
        /* LOG=2: *(cur->pos) — то, что cb40 читает как центр (@0x45cb4d/4f/52).
         * pos+0x00 — НЕ всегда указатель (замер: 0xa7b3), и разыменование мусора
         * давало AV на первый шанс → ложную CRASH-строку в логе. Гейт ниже —
         * ДИАГНОСТИЧЕСКАЯ ЭВРИСТИКА правдоподобия user-space указателя
         * (диапазон + выравнивание), а не знание о поле: при непроходе
         * остаются sentinel'ы «неизвестно» (emb=NULL, embx=emby=-2), разыменования нет.
         * Внутренний __try оставлен: правдоподобное, но невалидное значение
         * и дальше обрабатывается здесь же, а не внешним handler'ом s6_ai_update. */
        __try {
            unsigned long v = (unsigned long)(INT_PTR)*(const void **)(pos);
            if (v >= 0x00010000ul && v < 0x7fff0000ul && (v & 3u) == 0u) {
                emb = (void *)(INT_PTR)v;
                embx = *(const int *)((const unsigned char *)emb + 0x0c);
                emby = *(const int *)((const unsigned char *)emb + 0x10);
            } else {
                emb = NULL; embx = -2; emby = -2;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            emb = NULL; embx = -2; emby = -2;
        }

        x_start = s6_imax(pos->x - 1, 0);                     /* @0x45dd85/0x45dda5 */
        x_max   = s6_grid_clamp(level->size_x, pos->x + 1);   /* @0x45dd8c..@0x45ddd9 */
        y_start = s6_imax(pos->y - 1, 0);                     /* @0x45dd91/0x45dda9 */
        y_max   = s6_grid_clamp(level->size_y, pos->y + 1);   /* @0x45dde1..0x45ddfc */


        if (x_start > x_max) {                                /* @0x45de0e JG 0x45e08d */
            LD_CRASH_RET(1);
        }
        if ((x_max - x_start < 0 || y_max - y_start < 0)) {
            InterlockedIncrement(&g_ai_degen);      /* вырожденный 3x3 (счётчик) */
        }
        /* LOG=2: геометрия 3×3 — откуда берётся вырождение окна. */
        if (g_log_level >= LD_DEBUG && ld_rate(&g_ai_win_dbg, 8, 256)) {
            ld_debug("win: ai=%ld cur=%p pos=(%d,%d) level=%p size=(%d,%d) "
                     "x=[%d,%d] y=[%d,%d] target=%p",
                     (long)g_ai_calls, (void *)cur, cpx, cpy, (void *)level,
                     level->size_x, level->size_y, x_start, x_max,
                     y_start, y_max, target);
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
                void *diag_path = NULL;

                InterlockedIncrement(&g_ai_cells);
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
                if (nbr != NULL) {
                    InterlockedIncrement(&g_ai_nbr_old);
                }

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
                            InterlockedIncrement(&g_ai_cell_null);
                            seed = 0; continue;               /* @0x45defc JE → хвост цикла (xor esi @0x45e05a) */
                        }
                        ai_stage = 10;                        /* grid lookup done */
                        sp[0] = (int)(INT_PTR)cell;           /* spec[0x00]=cell @0x45df05 */
                        sp[4] = *(const int *)&cur->out_metric;  /* spec[0x10]=cur->[0x10] @0x45df13 */
                        ai_stage = 20;                        /* в cb40 */
                        path_res = s6_path_search(self, (PathState *)sp);  /* @0x45df17 RET 8 */
                        diag_path = path_res;
                        ex_path = path_res;
                        ai_stage = 30;                        /* cb40 вернул */
                        if (path_res == NULL) {
                            InterlockedIncrement(&g_ai_path_null);
                            seed = 0; continue;               /* @0x45df24 JE → хвост цикла */
                        }
                        ai_stage = 40;                        /* pfs ctor */
                        nbr = s6_pfs_ctor();                  /* @0x45df2c CALL 0x45c2e0, куча игры */
                        if (nbr == NULL) {
                            InterlockedIncrement(&g_ai_pfs_null);
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
                        ai_stage = 50;                        /* dist2(path_res, target) */
                        dist2 = s6_distance2((void *)(INT_PTR)path_res, target);  /* CALL 0x45c480 @0x45df66 */
                        sp[7] = dist2;                        /* spec[0x1c] @0x45df6b */
                        ai_stage = 60;                        /* heuristic(sp) */
                        h = s6_heuristic_gate(self, sp);      /* CALL 0x45c690 @0x45df76, &spec @0x45df6f */
                        sp[8] = h * 10000;                    /* spec[0x20] @0x45df85 */
                        cost = h * 10000 + dist2 + (cur->g_cost + 1);  /* @0x45df7b/0x45df8e/0x45df95 */
                        sp[9] = cost;                         /* spec[0x24] store @0x45df9a */
                        ai_stage = 70;                        /* copy + register */
                        s6_copy_fields((int)(INT_PTR)nbr, (int)(INT_PTR)sp, 0x1fff);  /* src=&spec @0x45df91, CALL 0x45c4b0 @0x45df9e */
                        InterlockedIncrement(&g_reg_calls);
                        native::register_occupied_slot(self, (LDObject *)nbr);  /* CALL 0x45dd00 @0x45dfa6 — только ветка 1 */
                        free_done = 1;                        /* @0x45dfab JMP 0x45e040 */
                    }
                }

                if (!free_done) {                             /* ветка 2 (@0x45dfb0..0x45e037) */
                    ai_stage = 80;                            /* branch 2 */
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

                /* LOG=2: координаты, которые реально несёт новый/существующий узел. */
                {
                    void *nodepos = NULL;
                    int npx = -2, npy = -2;

                    __try {
                        if (nbr != NULL) {
                            nodepos = nbr->pos;
                            if (nodepos != NULL) {
                                npx = *(const int *)((const unsigned char *)nodepos + 0x0c);
                                npy = *(const int *)((const unsigned char *)nodepos + 0x10);
                            }
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        nodepos = NULL; npx = -2; npy = -2;
                    }
                    s6_cell_diag((long)g_ai_calls, x, y, cur, pos, cpx, cpy,
                                 emb, embx, emby, free_done ? "free" : "old",
                                 diag_path, sp, nbr, nodepos, npx, npy);
                }

                cur->nbr[idx] = nbr;                          /* @0x45e04c store %ESI */
                nbr->nbr[rev] = cur;                          /* @0x45e050 store %EBX */
                seed = (int)(INT_PTR)nbr;                     /* esi после связки */
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_log_level >= LD_DEBUG) {
            ld_debug("ai_ex: ai=%ld stage=%ld hstage=%ld cur=%p pos=(%d,%d) "
                     "cells=%ld av=%ld path=%p",
                     (long)g_ai_calls, (long)ai_stage, (long)g_heur_stage,
                     (void *)cur, cpx, cpy, (long)g_ai_cells,
                     native::av_count(), ex_path);
        }
        LD_CRASH_RET(0);
    }

    (void)arg2;
    LD_CRASH_RET(1);
}

/* Замена FUN_0045e0a0 (cdecl; вызывается из трамплина). Faithful-цикл (п. 3.1):
 * порт тела 0x45e0a0..0x45e1c0 (game_dis.txt 133731..133838). Порядок:
 *   if (param_4) *param_4 = 0;                       @0x45e0b2
 *   cur = scan(filter);                              @0x45e103 (ce80)
 *   while (cur) {                                    @0x45e10a / @0x45e165
 *     ai_update(this, cur, target_pos);              @0x45e11b (dd70)
 *     c4b0(&local{+8=3}, cur, 4) → cur->filter = 3;  @0x45e132 (permanent, без restore)
 *     if (cur->pos == target_pos) { code=1; return cur; }    @0x45e137 / @0x45e17d
 *     if (check_distance(this, cur)) { code=4; return cur; } @0x45e13e / @0x45e197
 *     if (check_timeout(this, timeout)) { code=3; return cur; } @0x45e14e / @0x45e1af
 *     cur = scan(filter);                            @0x45e15e (rescan)
 *   }
 *   return NULL;   // пустой скан: code остаётся 0 (entry-запись), записи в *param_4 нет
 * Гейты — в родном порядке; ai_update/scan/предикаты — порты (выше). Разметка
 * постоянная (native c4b0 mask 4), restore не вызывается. */
void *__cdecl s6_target_search(void *this_, void *arg0, void *arg1,
                                      void *arg2, void *arg3)
{
    int filter = (int)(INT_PTR)arg0;
    void *cmp_val = arg1;                /* target pos_obj (сравнение с cur->pos) */
    int *out_code = (int *)arg3;
    int code = 0;
    LDObject *cur;
    LDObject *result = NULL;             /* cur на сработавшем гейте; NULL на пустом скане */
    LONG n = InterlockedIncrement(&g_site6_calls);
    LONG iters = 0;
    LONGLONG t0 = 0;                       /* Фаза 4 A/B: точка отсчёта */
    int map1_before = 0;
    LONG d_cells, d_cellnull, d_pathnull, d_pfsnull, d_reg, d_degen;
    long ps0[7], ps1[7];
    /* LDHOOK_E0_DIAG: снимок визитов цикла (нет аллокаций — фиксированный стек;
     * diag==0 → все ветки пропущены, поведение побайтно прежнее). */
    const int diag = e0_diag_on();
    E0DiagSlot e0sl[E0_DIAG_TABLE];
    LONG e0n = 0, e0ovf = 0, e0nulls = 0, e0rep = 0;
    LONG e0mchg = 0, e0msame = 0, e0f3 = 0, e0f1 = 0;
    long e0reg0 = (long)g_reg_calls;

    if (diag) {
        memset(e0sl, 0, sizeof(e0sl));   /* count==0 = слот пуст */
    }

    ld_crash_enter(2);          /* LDHOOK_CRASHLOG: stage 2, depth++ (no-op off) */

    /* H4: вход FUN_0045e0a0 (детур) — сброс счётчиков вызова для trace. */
    battle_trace_e0a0_enter(this_, filter);

    if (out_code != NULL) {
        *out_code = 0;
    }

    if (n == 1) {
        ld_info("site6: first call (hook FIRED) this=%p filter=%d cmp=%p "
                "layout_ld=%d layout_node=%d",
                this_, filter, cmp_val, (int)ld_off_ok, (int)ld_off_ok2);
    }

    t0 = ld_now_us();                      /* Фаза 4 A/B: время вызова */

    /* Faithful-цикл оригинала (while(cur), без ограничения итераций): на каждом
     * кандидате — ai_update, permanent-разметка cur->filter=3 (c4b0 mask 4),
     * затем гейты в родном порядке: pos==cmp → code=1; c880 → code=4;
     * c5a0 → code=3. Rescan после каждого не-сработавшего кандидата; пустой
     * скан → выход без кода, return NULL. */
    cur = s6_scan_map(this_, filter);      /* @0x45e103 */
    if (diag && cur == NULL) {
        e0nulls++;                          /* скан пуст: цикл не начнётся */
    }
    if (ld_rate(&g_site6_dbg, 4, 64)) {
        ld_debug("site6: call %ld scan -> %p (filter=%d cmp=%p)",
                 (long)n, (void *)cur, filter, cmp_val);
    }
    if (n <= 2) {
        ld_info("site6: call %ld pre-scan f1=%d (cmp=%p trk=%d thr=%u anchor=%p)",
                (long)n, s6_scan_count(this_, filter), cmp_val,
                (int)((BattleAI *)this_)->tracking,
                (unsigned)((BattleAI *)this_)->distance_threshold,
                (void *)((BattleAI *)this_)->anchor);
    }
    if (n <= 2 || g_log_level >= LD_DEBUG) {
        ld_info("site6: call %ld map1 f0=%d f1=%d f2=%d f3=%d filter=%d",
                (long)n, s6_scan_count(this_, 0), s6_scan_count(this_, 1),
                s6_scan_count(this_, 2), s6_scan_count(this_, 3), filter);
    }
    {
        d_cells = g_ai_cells; d_cellnull = g_ai_cell_null;
        d_pathnull = g_ai_path_null; d_pfsnull = g_ai_pfs_null;
        d_reg = g_reg_calls;
        d_degen = g_ai_degen;
        s6_pfs_snapshot(ps0);
    }
    map1_before = ((BattleAI *)this_)->reserved_70;
    while (cur != NULL) {                  /* @0x45e10a / @0x45e165 */
        iters++;
        if (diag) {
            /* Снимок ДО ai_update и разметки: фильтр/метрика в момент, когда
             * скан выбрал кандидата (разметка filter=3 идёт @0x45e132). */
            int m = -2, f = -2;
            __try {
                m = cur->metric;
                f = cur->filter;
            } __except (EXCEPTION_EXECUTE_HANDLER) { m = -2; f = -2; }
            e0_diag_visit(e0sl, &e0n, &e0ovf, &e0rep, &e0mchg, &e0msame,
                          &e0f3, &e0f1, (void *)cur, m, f);
        }
        if (n <= 2 && iters <= 40) {
            LDPos *p = NULL;
            int px = -1, py = -1;
            __try {
                p = cur->pos;
                if (p != NULL) { px = p->x; py = p->y; }
            } __except (EXCEPTION_EXECUTE_HANDLER) { px = -2; py = -2; }
            ld_info("site6: call %ld it=%ld cand=%p f=%d m=%d pos=%p px=%d py=%d",
                    (long)n, (long)iters, (void *)cur, cur->filter, cur->metric,
                    (void *)p, px, py);
        }
        /* LOG=2: provenance кандидата для коротких вызовов (code=0). */
        if (g_log_level >= LD_DEBUG && (n <= 12 || iters <= 2)) {
            LDPos *cp = NULL, *tq = (LDPos *)cmp_val;
            int cpx = -2, cpy = -2, qx = -2, qy = -2;

            __try {
                cp = cur->pos;
                if (cp != NULL) { cpx = cp->x; cpy = cp->y; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                cp = NULL; cpx = -2; cpy = -2;
            }
            __try {
                if (tq != NULL) { qx = tq->x; qy = tq->y; }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                qx = -2; qy = -2;
            }
            ld_debug("cand: call=%ld it=%ld cand=%p pos=%p (%d,%d) cmp=%p (%d,%d) "
                     "f=%d m=%d",
                     (long)n, (long)iters, (void *)cur, (void *)cp, cpx, cpy,
                     (void *)tq, qx, qy, cur->filter, cur->metric);
        }
        /* AI/pathfinding update — порт FUN_0045dd70: arg2 = arg1 (pos_obj),
         * см. 0x45e117 push ebx. */
        s6_ai_update(this_, cur, (int)(INT_PTR)cmp_val);   /* @0x45e11b */
        /* FUN_0045c4b0(&local, cur, 4): cur->filter = 3 — permanent-разметка,
         * помеченный кандидат выпадает из следующего скана ce80 (native-ротация,
         * без восстановления). */
        __try {
            cur->filter = 3;                               /* @0x45e132 */
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (cur->pos == (LDPos *)cmp_val) {                /* @0x45e137 */
            code = 1;                                      /* @0x45e17d: *param_4 = 1 */
            result = cur;
            break;
        }
        if (s6_check_distance(this_, cur)) {               /* @0x45e13e */
            code = 4;                                      /* @0x45e197: *param_4 = 4 */
            result = cur;
            break;
        }
        if (s6_check_timeout(this_, arg2)) {               /* @0x45e14e */
            code = 3;                                      /* @0x45e1af: *param_4 = 3 */
            result = cur;
            break;
        }
        cur = s6_scan_map(this_, filter);                  /* @0x45e15e: rescan */
        if (diag && cur == NULL) {
            e0nulls++;                                     /* пустой скан → выход */
        }
        battle_trace_e0a0_latch(cur);                      /* H6: EAX @0x45e163 */
#ifdef BATTLE_REPLAY
        replay_note_rescan(cur);                           /* iter_seq порта (v5) */
#endif
        InterlockedIncrement(&g_site6_rot);                /* ротация (п. 3.4) */
    }

    /* Единственная запись кода — под guard'ом param_4 != NULL; при пустом скане
     * code==0 и запись не делается (в *param_4 уже 0 из entry-записи @0x45e0b2). */
    if (out_code != NULL && code != 0) {
        *out_code = code;
    }

    /* H5: выход e0a0 порта (эквивалент эпилога add esp,0x34; ret 10h) —
     * узел-возврат (NULL при пустом скане) и [this+0x38] → trace-слот. */
    battle_trace_e0a0_exit(result);
#ifdef BATTLE_REPLAY
    replay_note_exit(result, code, (int)iters);            /* результат порта (v5) */
#endif

    /* LDHOOK_E0_DIAG: сводка вызова с итерациями выше порога (runaway-класс). */
    if (diag && iters > E0_DIAG_ITERS) {
        e0_diag_emit((long)n, (long)iters, (long)e0n, (long)e0ovf,
                     (long)e0rep, (long)e0mchg, (long)e0msame,
                     (long)e0f3, (long)e0f1,
                     (long)e0nulls, code, map1_before,
                     ((BattleAI *)this_)->reserved_70,
                     (long)g_reg_calls - e0reg0, e0sl);
    }

    /* Статистика (счётчики п. 3.4): итерации, распределение кодов 1/3/4
     * (code==0 — пустой скан), ротации считаются на rescan. */
    if (iters > g_site6_max) {
        g_site6_max = iters;
    }
    if (iters > S6_HEAVY_ITER) {
        InterlockedIncrement(&g_site6_heavy);
    }
    InterlockedAdd(&g_site6_iter_sum, iters);
    switch (code) {
    case 1:  InterlockedIncrement(&g_site6_code1); break;
    case 3:  InterlockedIncrement(&g_site6_code3); break;
    case 4:  InterlockedIncrement(&g_site6_code4); break;
    default: break;                        /* 0 — пустой скан */
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
                 (long)g_site6_heavy, us, (void *)result, code);
        if (g_log_level >= LD_DEBUG || map1_before !=
                ((BattleAI *)this_)->reserved_70) {
            ld_info("site6: call %ld filter=%d iters=%ld code=%d map1 %d -> %d "
                    "map2 %d -> %d (reg=%ld)",
                    (long)n, filter, (long)iters, code, map1_before,
                    (int)((BattleAI *)this_)->reserved_70,
                    (int)*(const int *)((const unsigned char *)this_ + 0x7c),
                    (int)*(const int *)((const unsigned char *)this_ + 0x7c),
                    (long)g_reg_calls);
        }
        s6_pfs_snapshot(ps1);
        if (g_log_level >= LD_DEBUG) {
            ld_info("site6: call %ld outs cells=%ld cellnull=%ld pathnull=%ld "
                    "pfsnull=%ld reg=%ld degen=%ld pfs(first=%ld sign=%ld bound=%ld grid=%ld "
                    "recur=%ld gate=%ld res=%ld)",
                    (long)n, (long)(g_ai_cells - d_cells),
                    (long)(g_ai_cell_null - d_cellnull),
                    (long)(g_ai_path_null - d_pathnull),
                    (long)(g_ai_pfs_null - d_pfsnull),
                    (long)(g_reg_calls - d_reg),
                    (long)(g_ai_degen - d_degen),
                    ps1[0]-ps0[0], ps1[1]-ps0[1], ps1[2]-ps0[2],
                    ps1[3]-ps0[3], ps1[4]-ps0[4], ps1[5]-ps0[5], ps1[6]-ps0[6]);
        }
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
    LD_CRASH_RET((void *)result);
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
