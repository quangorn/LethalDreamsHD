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
#include "leaf_sites.h"
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

/* NATIVE E0-диаг (LDHOOK_E0_DIAG=1, LDHOOK_SITE6=0) — определения ниже,
 * после e0_diag_emit. Точки снятия снимка — трамплины site5. */
static int  e0n_diag_on(void);
static void e0n_snapshot(int depth, BattleAI *self);
static void e0n_visit(int depth, void *cand);
static void e0n_exit(int depth, BattleAI *self, int code);

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
        if (e0n_diag_on()) {
            e0n_snapshot((int)g_s5_depth, self);   /* окно diag-счётчиков вызова */
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

    /* NATIVE E0-диаг: визит кандидата ДО ai_update (EAX хвоста 0x45e163) —
     * тот же снимок, что порт делает на cur перед s6_ai_update. Счётчик
     * итераций по глубине (g_site5_iter общий и сбрасывается вложенным
     * вызовом). */
    if (e0n_diag_on()) {
        e0n_visit((int)g_s5_depth, candidate);
    }

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
static void __cdecl s5_exit_impl(int *param_4, void *candidate, void *this_)
{
    LONG d = g_s5_depth;
    LONGLONG us = 0;

    /* NATIVE E0-диаг: код выхода читается из param_4 (его пишут эпилоги
     * 0x45e17d/0x45e197/0x45e1af); сводка вызова — до спуска глубины. */
    if (e0n_diag_on()) {
        int code = 0;
        if (param_4 != NULL) {
            __try { code = *param_4; }
            __except (EXCEPTION_EXECUTE_HANDLER) { code = -1; }
        }
        e0n_exit((int)g_s5_depth, (BattleAI *)this_, code);
    }
    d = InterlockedDecrement(&g_s5_depth);

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
    /* Сайт = `add esp,0x34; ret 0x10`: при входе esp = E-0x34 (E — esp после
     * push retaddr вызывающим), pop ebx/edi/esi/ebp уже сняли сохранения,
     * EDI = this, EAX = узел-возврат; param_4 (arg4) лежит на E+0x10.
     * Стек аргументов cdecl: param_4, candidate, this. */
    __asm {
        push eax                     /* esp = E-0x38 */
        push ecx                     /* esp = E-0x3C */
        push edx                     /* esp = E-0x40 */
        push edi                     /* esp = E-0x44 */
        push eax                     /* esp = E-0x48 */
        push dword ptr [esp + 0x58]  /* param_4 = E+0x10 при esp = E-0x48 */
        call s5_exit_impl            /* cdecl (param_4, candidate, this) */
        add esp, 12                  /* сняли 3 аргумента; edi снят с arg3-слота */
        pop edx
        pop ecx
        pop eax                      /* esp = E-0x34 — состояние сайта */
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
/* Гейт порта site6 — единственный источник истины для S6_ENV. Читается
 * один раз при первом обращении. Поставка: порт ВКЛЮЧЁН по умолчанию
 * (env LDHOOK_SITE6=0 — откат на нативный 0x45e0a0). */
int s6_port_enabled(void)
{
#ifdef BATTLE_REPLAY
    return 1;
#else
    static int on = -1;

    if (on < 0) {
        on = ld_env_flag(S6_ENV, 1);
    }
    return on;
#endif
}

/* Гейт detour'а 45eee0 (install_tick_hook) — LDHOOK_SITE6_EEE0; единственный
 * источник истины для него. Поставка: по умолчанию НАТИВНЫЙ тик (0) при
 * включённом порте e0a0; LDHOOK_SITE6_EEE0=1 включает и порт тика. Раньше
 * дефолт следовал мастеру LDHOOK_SITE6 — это делало порт тика неявно
 * включённым и ломало принятую конфигурацию замеров.
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
        on = ld_env_flag(S6_EEE0_ENV, 0);
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

/* ------------------------------------------------------------------ */
/* 9.1c — потолок итераций одного вызова s6_target_search.             */
/*                                                                    */
/* Причина (замер 9.1a): цикл ограничен настенными часами             */
/* (`s6_timer_restart` до вызова, порог `ctx[0x14]` ≈ 70 ms), поэтому */
/* провальный вызов всегда стоит ~71 ms независимо от цены итерации:  */
/* 4 246 итераций на линейном скане, 11 003 на куче — обе ~71 ms.     */
/* Лечится бюджетом итераций, а не ценой шага.                        */
/*                                                                    */
/* env LDHOOK_E0_MAXITER=<n>: n > 0 — вызов обрывается после n        */
/* итераций ровно как по таймауту (код 3, возврат cur); unset — 2000  */
/* (принято 9.1b), явные 0/отрицательное — потолка нет (прежнее       */
/* поведение). Не зависит от режима кучи: проверка в общем цикле.     */
/*                                                                    */
/* 9.1d — env LDHOOK_E0_MAXUS=<µs>: потолок ВРЕМЕНИ одного вызова.    */
/* Потолок итераций время не ограничивает: цена итерации растёт с     */
/* размером карты (стресс 35 юнитов: те же 2000 итераций стоят        */
/* 20–36 ms, путь = 80–90 % вызова, 9.2a). Часы — QPC (`ld_now_us`),  */
/* снимается раз в E0CAP_US_CHECK итераций и обязательно на той       */
/* итерации, где срабатывает потолок итераций. Выход — тот же, что у  */
/* капа итераций (код 3, возврат cur). unset/0 → потолка времени нет. */
/* ------------------------------------------------------------------ */
#define E0_MAXITER_ENV "LDHOOK_E0_MAXITER"
#define E0_MAXUS_ENV   "LDHOOK_E0_MAXUS"
#define E0CAP_PERIOD   250          /* сводка E0CAP: каждые N вызовов e0a0 */
#define E0CAP_US_CHECK 32           /* снимок часов раз в N итераций */

static volatile LONG g_e0cap_hits = 0;      /* вызовов, оборванных капом итераций */
static volatile LONG g_e0cap_iters = 0;     /* сумма итераций оборванных вызовов */
static volatile LONG g_e0cap_max_iter = 0;  /* максимум итераций при обрыве */
static volatile LONG g_e0cap_us_hits = 0;   /* вызовов, оборванных капом времени */
static volatile LONG g_e0cap_us_checks = 0; /* снимков часов за прогон */
static volatile LONGLONG g_e0cap_us_max = 0;/* максимум µs при обрыве по времени */
static int s_e0cap_atexit = 0;              /* atexit — один раз на процесс */

static void __cdecl e0cap_atexit(void);     /* финальная сводка (atexit) */

static void e0cap_atexit_reg(void)
{
    if (!s_e0cap_atexit) {
        s_e0cap_atexit = 1;
        atexit(e0cap_atexit);
    }
}

/* Кэш env на процесс (env в рантайме не меняется). 0 = потолка нет. */
static long s6_maxiter_get(void)
{
    static long cap = -1;

    if (cap < 0) {
        const char *e = getenv(E0_MAXITER_ENV);
        long v = (e != NULL && *e != '\0') ? atol(e) : 2000L;  /* принято 9.1b: по умолчанию 2000 */

        cap = (v > 0) ? v : 0L;
        if (cap > 0) {
            ld_info("E0CAP maxiter=%ld enabled (%s)", cap, E0_MAXITER_ENV);
            e0cap_atexit_reg();
        }
    }
    return cap;
}

/* Кэш env на процесс. 0 = потолка времени нет (unset/0 — прежнее поведение). */
static long s6_maxus_get(void)
{
    static long cap = -1;

    if (cap < 0) {
        const char *e = getenv(E0_MAXUS_ENV);
        /* принято 9.1d: по умолчанию 3000 µs; env 0 отключает */
        long v = (e != NULL && *e != '\0') ? atol(e) : 3000L;

        cap = (v > 0) ? v : 0L;
        if (cap > 0) {
            ld_info("E0CAP maxus=%ld enabled (%s)", cap, E0_MAXUS_ENV);
            e0cap_atexit_reg();
        }
    }
    return cap;
}

static void e0cap_emit(const char *why)
{
    ld_info("E0CAP %s maxiter=%ld maxus=%ld calls=%ld capped_it=%ld capped_us=%ld "
            "capped_iters=%ld max_capped_iters=%ld max_capped_us=%I64d us_checks=%ld "
            "code1=%ld code3=%ld code4=%ld tick=%ld",
            why, s6_maxiter_get(), s6_maxus_get(), (long)g_site6_calls,
            (long)g_e0cap_hits, (long)g_e0cap_us_hits,
            (long)g_e0cap_iters, (long)g_e0cap_max_iter,
            (LONGLONG)g_e0cap_us_max, (long)g_e0cap_us_checks,
            (long)g_site6_code1, (long)g_site6_code3, (long)g_site6_code4,
            battle_trace_tick_count());
}

static void __cdecl e0cap_atexit(void)
{
    e0cap_emit("final");
}

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

int e0_diag_on(void)
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

/* ------------------------------------------------------------------ */
/* E0COST — куда уходят микросекунды длинного вызова e0a0 (план 9.d2d).   */
/*                                                                        */
/* env LDHOOK_E0_DIAG=1 + порт site6. Одна строка E0COST на вызов с        */
/* iters > E0_DIAG_ITERS; сводка прогона — раз в E0C_SUM_CALLS вызовов     */
/* (полные итоги, включая короткие вызовы: доля e0a0 во всём бое).         */
/* Разложение: us_scan — s6_scan_map; us_update — s6_ai_update (включая   */
/* вложенную рекурсию и us_path); us_path — s6_path_search (45cb40);       */
/* us_lb — native::lower_bound_slots (map2: и в dd70, и в cb40).           */
/* us_scan + (us_update − us_path) + us_path ≤ us_total; «прочее» =        */
/* us_total − us_scan − us_update. us_lb — подмножество us_path/us_update, */
/* в сумму разложения не входит. Кап 400 строк; кап объявлен одной строкой.*/
/* Счётчики interlocked, diag==0 → ни записи в память, ни строки.          */
/* Разрывность: us_lb ⊆ us_path ⊆ us_update — доли НЕ дизъюнктны.          */
/* nodes — 64-бит: один длинный вызов сканирует ~11M узлов, прогон 538      */
/* вызовов ~5e9; 32-бит LONG переполняется и даёт мусор дельты.             */
/* ------------------------------------------------------------------ */

#define E0C_LINES      400           /* кап строк E0COST на процесс */
#define E0C_SUM_CALLS  250           /* период сводки E0COSTSUM (вызовов) */

/* ld_now_us() = QPC.QuadPart * 1000000 / freq (64-бит). Замер на этой машине:
 * Stopwatch.Frequency = 10 000 000, текущий QPC-таймстемп 1.45e12 (~40 ч аптайма).
 * Переполнение ×1e6 — при QPC > 9.2e12, то есть через ~10.6 суток аптайма;
 * живые прогоны (~70 с) выше не задевают. Правка ld_now_us вне этой задачи. */
volatile LONGLONG g_e0c_nodes = 0;   /* узлы map1, обошедшие сканы вызова */
volatile LONG g_e0c_b2 = 0;          /* исполнения тела ветки 2 FUN_0045dd70 */
volatile LONGLONG g_e0c_path_us = 0; /* время внутри s6_path_search */
volatile LONGLONG g_e0c_lb_us = 0;   /* время внутри native::lower_bound_slots */

static volatile LONG g_e0c_lines = 0, g_e0c_capped = 0;
/* Вложенность s6_ai_update: µs копятся только на верхнем входе, рекурсия
 * dd70→dd70 не считается дважды. */
static volatile LONG g_e0c_bad = 0;  /* самосогласованность: us_scan+us_update > us_total,
                                      * us_path > us_update, us_lb > us_update */
static volatile LONG g_e0c_depth = 0;
static volatile LONGLONG g_e0c_upd_us = 0;
/* Итоги прогона (печатаются сводкой E0COSTSUM). */
static volatile LONG g_e0c_calls = 0, g_e0c_iters = 0;
static volatile LONG g_e0c_b2_sum = 0;
static volatile LONGLONG g_e0c_nodes_sum = 0;
static volatile LONG g_e0c_live_sum = 0, g_e0c_map1_sum = 0, g_e0c_map1b_sum = 0;
static volatile LONGLONG g_e0c_us_sum = 0, g_e0c_scan_sum = 0;
static volatile LONGLONG g_e0c_upd_sum = 0, g_e0c_path_sum = 0;
static volatile LONGLONG g_e0c_lb_sum = 0;

/* Выход из s6_ai_update; edep < 0 = этот вход не измерялся (diag выкл). */
static void e0c_ai_leave(long edep, LONGLONG tu)
{
    if (edep < 0) {
        return;
    }
    InterlockedDecrement(&g_e0c_depth);
    if (edep == 0) {
        InterlockedAdd64(&g_e0c_upd_us, ld_now_us() - tu);
    }
}

/* bad = самосогласованность снимка (числа не меняются, строка помечается).
 * us_lb ⊆ us_path ⊆ us_update: us_lb и us_path — подмножества, доли не складываются. */
static void e0c_emit(long call, int filter, long iters, int code, int capped,
                     LONGLONG us_total, LONGLONG us_scan, LONGLONG us_update,
                     LONGLONG us_path, LONGLONG us_lb,
                     LONGLONG nodes, long live_before, long live_after, long b2,
                     int map1_0, int map1_1, long tick)
{
    int bad = 0;
    if (g_e0c_capped != 0) {
        return;
    }
    if (us_scan + us_update > us_total || us_path > us_update ||
        us_lb > us_update) {
        bad = 1;
    }
    ld_info("E0COST call=%ld filter=%d iters=%ld code=%d capped=%d us_total=%I64d "
            "us_scan=%I64d us_update=%I64d us_path=%I64d us_lb=%I64d "
            "nodes_scanned=%I64d live_before=%ld live_after=%ld b2=%ld "
            "map1=%d->%d tick=%ld arm=port%s",
            call, filter, iters, code, capped, us_total, us_scan, us_update,
            us_path, us_lb, nodes, live_before, live_after, b2, map1_0, map1_1,
            tick, bad ? " bad=1" : "");
    g_e0c_lines++;
    if (g_e0c_lines >= E0C_LINES) {
        ld_info("E0COST %ld lines (limit %d), capped for this run",
                (long)g_e0c_lines, E0C_LINES);
        g_e0c_capped = 1;
    }
}

/* ------------------------------------------------------------------ */
/* E0PATH (план 9.2a) — разложение µs внутри FUN_0045cb40 за вызов e0a0.   */
/*                                                                        */
/* env LDHOOK_E0_DIAG=1 + порт site6. Строка печатается на том же гейте,    */
/* что E0COST (`iters > E0_DIAG_ITERS`); кап строк общий с E0COST.          */
/* Дизъюнктность: lb_us / grid_us / ts_us ⊆ us_path и не пересекаются       */
/* (все три зовутся из grid-цикла cb40 по одному разу за итерацию);         */
/* «прочее» = us_path − (lb+grid+ts) = цикл окна, проверки границ/знаков   */
/* и финальный порог. Reкурсия: ts — все входы 0x45c3a0, recur — входы      */
/* из grid-цикла; nesting меряется (depthmax) и µs не умножаются.           */
/* dup_call / dup_tick — верхняя оценка мемоизации: path-вызовы с ключом    */
/* (cell, target), уже виденным в этом вызове / в этом тике.               */
/* E0PATHSUM — полные итоги прогона по ВСЕМ вызовам (короткие тоже),        */
/* период тот же, что у E0COSTSUM.                                          */
/* ------------------------------------------------------------------ */

static volatile LONG g_e0ps_calls = 0, g_e0ps_null = 0, g_e0ps_lb = 0;
static volatile LONG g_e0ps_grid = 0, g_e0ps_ts = 0, g_e0ps_recur = 0;
static volatile LONG g_e0ps_cells = 0, g_e0ps_dupc = 0, g_e0ps_dupt = 0;
static volatile LONG g_e0ps_ovf = 0;
static volatile LONGLONG g_e0ps_lb_us = 0, g_e0ps_grid_us = 0, g_e0ps_ts_us = 0;

static void e0path_emit(long call, long path_calls, LONGLONG us_path,
                        long lb, LONGLONG lb_us, long grid, LONGLONG grid_us,
                        long ts, long recur, LONGLONG ts_us, long depth_max,
                        long cells, long nulls, long dup_call, long dup_tick,
                        long ovf, long iters, long tick)
{
    if (g_e0c_capped != 0) {
        return;
    }
    ld_info("E0PATH call=%ld path_calls=%ld us_path=%I64d lb=%ld lb_us=%I64d "
            "grid=%ld grid_us=%I64d ts=%ld recur=%ld ts_us=%I64d depthmax=%ld "
            "cells=%ld null=%ld dup_call=%ld dup_tick=%ld ovf=%ld iters=%ld "
            "tick=%ld arm=port",
            call, path_calls, us_path, lb, lb_us, grid, grid_us, ts, recur,
            ts_us, depth_max, cells, nulls, dup_call, dup_tick, ovf, iters, tick);
    g_e0c_lines++;
    if (g_e0c_lines >= E0C_LINES) {
        ld_info("E0PATH+E0COST %ld lines (limit %d), capped for this run",
                (long)g_e0c_lines, E0C_LINES);
        g_e0c_capped = 1;
    }
}

static void e0pathsum_emit(long tick)
{
    ld_info("E0PATHSUM tick=%ld calls=%ld null=%ld lb=%ld lb_us=%I64d grid=%ld "
            "grid_us=%I64d ts=%ld recur=%ld ts_us=%I64d depthmax=%ld cells=%ld "
            "dup_call=%ld dup_tick=%ld ovf=%ld",
            tick, (long)g_e0ps_calls, (long)g_e0ps_null, (long)g_e0ps_lb,
            (LONGLONG)g_e0ps_lb_us, (long)g_e0ps_grid, (LONGLONG)g_e0ps_grid_us,
            (long)g_e0ps_ts, (long)g_e0ps_recur, (LONGLONG)g_e0ps_ts_us,
            (long)g_e0p_depth_max, (long)g_e0ps_cells, (long)g_e0ps_dupc,
            (long)g_e0ps_dupt, (long)g_e0ps_ovf);
}

/* ------------------------------------------------------------------ */
/* NATIVE E0-диаг (LDHOOK_E0_DIAG=1 + LDHOOK_SITE6=0): тот же снимок       */
/* вызова e0a0, что печатает порт, но на нативном цикле.                  */
/*                                                                        */
/* Источник итераций — хвост цикла site5 (0x45e163 `mov esi,eax`): EAX =   */
/* кандидат следующей итерации ДО ai_update — тот же снимок, что порт      */
/* снимает на cur перед s6_ai_update. Счётчик итераций — по глубине         */
/* вложенности (g_site5_iter общий и сбрасывается вложенным вызовом).      */
/* Гейты ветки 1 FUN_0045dd70: CALL-сайты 55..59 leaf_sites (diag=1) —     */
/* lower_bound @0x45decb, grid @0x45def5, path @0x45df17, ctor @0x45df2c,   */
/* register @0x45dfa6. Регистрации считаются и на ВХОДЕ FUN_0045dd00:      */
/* адрес возврата хука классифицирует caller (ветка 1 dd70 vs лист         */
/* 0x45e1d0). Строка печатается при iters > E0_DIAG_ITERS; кап строк —      */
/* отдельный счётчик. env не задан → ветки diag не выполняются.            */
/* ------------------------------------------------------------------ */

#define E0N_DEPTH     16            /* та же глубина, что у стека t0 site5 */
#define E0N_LINES     400           /* кап строк native-диага на процесс */
#define E0N_SUM_CALLS 2000          /* период строки E0DIAGSUM */
#define E0N_VA_REG    0x45dd00u
#define E0N_REG_SIG   "\x83\xec\x18\x56\x8b\x74\x24\x20"
#define E0N_REG_LEN   8             /* sub esp,0x18; push esi; mov esi,[esp+0x20] */

static int e0n_on = -1;

static int e0n_diag_on(void)
{
    if (e0n_on < 0) {
        e0n_on = (e0_diag_on() && !s6_port_enabled()) ? 1 : 0;
    }
    return e0n_on;
}

/* Глобальные счётчики (interlocked): пишутся из leaf-сайтов и из хука
 * 0x45dd00. Окно вызова — дельты по глубине. */
static volatile LONG g_e0n_lb, g_e0n_grid, g_e0n_grid_null;
static volatile LONG g_e0n_path, g_e0n_path_null, g_e0n_pfs, g_e0n_pfs_null;
static volatile LONG g_e0n_reg_all, g_e0n_reg_dd70, g_e0n_reg_leaf, g_e0n_reg_other;
static volatile LONG g_e0n_lines = 0, g_e0n_capped = 0;
static volatile LONG g_e0n_calls_all = 0, g_e0n_calls_long = 0;
static volatile LONG g_e0n_iter_all = 0, g_e0n_dist_all = 0;

/* Окно вызова по глубине (индекс = g_s5_depth-1, как у стека t0). */
static E0DiagSlot e0n_tbl[E0N_DEPTH][E0_DIAG_TABLE];
static LONG e0n_iter[E0N_DEPTH], e0n_dist[E0N_DEPTH], e0n_ovf[E0N_DEPTH];
static LONG e0n_rep[E0N_DEPTH], e0n_mchg[E0N_DEPTH], e0n_msame[E0N_DEPTH];
static LONG e0n_f3[E0N_DEPTH], e0n_f1[E0N_DEPTH];
static LONG e0n_call[E0N_DEPTH];
static LONG e0n_reg0[E0N_DEPTH], e0n_lb0[E0N_DEPTH], e0n_grid0[E0N_DEPTH];
static LONG e0n_gridn0[E0N_DEPTH], e0n_path0[E0N_DEPTH], e0n_pathn0[E0N_DEPTH];
static LONG e0n_pfs0[E0N_DEPTH], e0n_pfsn0[E0N_DEPTH];
static int  e0n_map1_0[E0N_DEPTH];

void *g_e0n_reg_cont = NULL;        /* продолжение 0x45dd00 после украденных байт */
void *g_e0n_reg_tramp = NULL;       /* трамплин e0n_reg_tramp (заполняет установщик) */

static void e0n_snapshot(int depth, BattleAI *self)
{
    LONG d = depth;
    int i;

    if (d < 1 || d > E0N_DEPTH) {
        return;
    }
    d--;
    for (i = 0; i < E0_DIAG_TABLE; i++) {
        e0n_tbl[d][i].count = 0;
    }
    e0n_iter[d] = 0;   e0n_dist[d] = 0; e0n_ovf[d] = 0;
    e0n_rep[d] = 0;    e0n_mchg[d] = 0; e0n_msame[d] = 0;
    e0n_f3[d] = 0;     e0n_f1[d] = 0;
    e0n_call[d] = (LONG)g_site5_calls;
    e0n_reg0[d]    = g_e0n_reg_all;
    e0n_lb0[d]     = g_e0n_lb;
    e0n_grid0[d]   = g_e0n_grid;      e0n_gridn0[d] = g_e0n_grid_null;
    e0n_path0[d]   = g_e0n_path;      e0n_pathn0[d]  = g_e0n_path_null;
    e0n_pfs0[d]    = g_e0n_pfs;       e0n_pfsn0[d]   = g_e0n_pfs_null;
    __try {
        e0n_map1_0[d] = self->reserved_70;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        e0n_map1_0[d] = -1;
    }
    InterlockedIncrement(&g_e0n_calls_all);
}

static void e0n_visit(int depth, void *cand)
{
    LONG d = depth;
    int m = -2, f = -2;

    if (d < 1 || d > E0N_DEPTH) {
        return;
    }
    d--;
    e0n_iter[d]++;
    if (cand != NULL) {
        __try {
            m = ((Candidate *)cand)->metric;
            f = ((Candidate *)cand)->filter;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            m = -2; f = -2;
        }
    }
    e0_diag_visit(e0n_tbl[d], &e0n_dist[d], &e0n_ovf[d], &e0n_rep[d],
                  &e0n_mchg[d], &e0n_msame[d], &e0n_f3[d], &e0n_f1[d],
                  cand, m, f);
}

static void e0n_exit(int depth, BattleAI *self, int code)
{
    LONG d = depth;
    long reg_d, lb_d, grid_d, gridn_d, path_d, pathn_d, pfs_d, pfsn_d;
    int map1_now = -1;

    if (d < 1 || d > E0N_DEPTH) {
        return;
    }
    d--;
    reg_d   = g_e0n_reg_all  - e0n_reg0[d];
    lb_d    = g_e0n_lb       - e0n_lb0[d];
    grid_d  = g_e0n_grid     - e0n_grid0[d];
    gridn_d = g_e0n_grid_null - e0n_gridn0[d];
    path_d  = g_e0n_path     - e0n_path0[d];
    pathn_d = g_e0n_path_null - e0n_pathn0[d];
    pfs_d   = g_e0n_pfs      - e0n_pfs0[d];
    pfsn_d  = g_e0n_pfs_null - e0n_pfsn0[d];
    if (e0n_map1_0[d] >= 0) {
        __try { map1_now = self->reserved_70; }
        __except (EXCEPTION_EXECUTE_HANDLER) { map1_now = -1; }
    }
    InterlockedAdd(&g_e0n_iter_all, e0n_iter[d]);
    InterlockedAdd(&g_e0n_dist_all, e0n_dist[d]);
    if (e0n_iter[d] > E0_DIAG_ITERS) {
        InterlockedIncrement(&g_e0n_calls_long);
        if (g_e0n_capped == 0 && g_e0n_lines < E0N_LINES) {
            ld_info("E0DIAG call=%ld iters=%ld distinct=%ld null_returns=%ld "
                    "code=%d map1=%d->%d reg=%ld lb=%ld grid=%ld gridnull=%ld "
                    "pfsc=%ld pfsnull=%ld rep=%ld ovf=%ld f3=%ld f1=%ld mchg=%ld "
                    "tick=%ld arm=native",
                    e0n_call[d], e0n_iter[d], e0n_dist[d], pathn_d, code,
                    e0n_map1_0[d], map1_now, reg_d, lb_d, grid_d, gridn_d,
                    pfs_d, pfsn_d, e0n_rep[d], e0n_ovf[d], e0n_f3[d], e0n_f1[d],
                    e0n_mchg[d], battle_trace_tick_count());
            g_e0n_lines++;
            if (g_e0n_lines >= E0N_LINES) {
                ld_info("E0DIAG %ld native lines (limit %d), capped for this run",
                        (long)g_e0n_lines, E0N_LINES);
                g_e0n_capped = 1;
            }
        }
    }
    if ((g_e0n_calls_all % E0N_SUM_CALLS) == 0) {
        ld_info("E0DIAGSUM arm=native calls=%ld long_calls=%ld iter_sum=%ld dist_sum=%ld "
                "reg_all=%ld reg_dd70=%ld reg_leaf=%ld reg_other=%ld lb=%ld "
                "grid=%ld gridnull=%ld path=%ld pathnull=%ld pfs=%ld pfsnull=%ld "
                "tick=%ld",
                (long)g_e0n_calls_all, (long)g_e0n_calls_long,
                (long)g_e0n_iter_all, (long)g_e0n_dist_all,
                (long)g_e0n_reg_all, (long)g_e0n_reg_dd70, (long)g_e0n_reg_leaf,
                (long)g_e0n_reg_other, (long)g_e0n_lb, (long)g_e0n_grid,
                (long)g_e0n_grid_null, (long)g_e0n_path, (long)g_e0n_path_null,
                (long)g_e0n_pfs, (long)g_e0n_pfs_null,
                battle_trace_tick_count());
    }
}

/* sink leaf-сайтов ветки 1: счётчик вызовов на входе сайта, NULL-возврат —
 * на выходе (ret_bits == 0). enter всегда даёт token: exit обязана случиться. */
static int e0n_sink_enter(int site, int va, int ecx, const int *args, int nargs)
{
    (void)va; (void)ecx; (void)args; (void)nargs;
    if (site == 0x45decb)      InterlockedIncrement(&g_e0n_lb);
    else if (site == 0x45def5) InterlockedIncrement(&g_e0n_grid);
    else if (site == 0x45df17) InterlockedIncrement(&g_e0n_path);
    else if (site == 0x45df2c) InterlockedIncrement(&g_e0n_pfs);
    return 1;
}

static void e0n_sink_exit(int token, int va, int ecx, const int *args, int nargs,
                          int ret_bits, const int *w, int nw, int trap)
{
    (void)token; (void)ecx; (void)args; (void)nargs;
    (void)w; (void)nw; (void)trap;
    if (ret_bits == 0) {
        if (va == 0x456ed0)      InterlockedIncrement(&g_e0n_grid_null);
        else if (va == 0x45cb40) InterlockedIncrement(&g_e0n_path_null);
        else if (va == 0x45c2e0) InterlockedIncrement(&g_e0n_pfs_null);
    }
}

static void e0n_sink_missing(int site, int target, const char *why)
{
    ld_info("E0DIAG call-site 0x%x->0x%x NOT installed: %s",
            (unsigned)site, (unsigned)target, (why != NULL) ? why : "");
}

static const leaf_sink g_e0n_sink = { e0n_sink_enter, e0n_sink_exit, e0n_sink_missing };

/* Хук ВХОДА FUN_0045dd00: счётчик регистраций, классификация caller'а по
 * адресу возврата. Ставится ТОЛЬКО при LDHOOK_E0_DIAG=1 и LDHOOK_SITE6=0. */
static void __cdecl e0n_reg_impl(int this_, int arg1, int retaddr)
{
    unsigned char *base = native::base();
    unsigned long off;

    (void)this_; (void)arg1;
    InterlockedIncrement(&g_e0n_reg_all);
    off = (base != NULL)
          ? (unsigned long)((unsigned)(size_t)retaddr - (unsigned)(size_t)base)
          : 0u;
    if (off == 0x45dfabu)      InterlockedIncrement(&g_e0n_reg_dd70);   /* CALL @0x45dfa6 */
    else if (off == 0x45e3f7u) InterlockedIncrement(&g_e0n_reg_leaf);   /* CALL @0x45e3f2 */
    else                       InterlockedIncrement(&g_e0n_reg_other);
}

__declspec(naked) void e0n_reg_hook(void)
{
    __asm {
        push eax
        push ecx
        push edx
        push dword ptr [esp + 12]    /* retaddr (E) при esp = E-12 */
        push dword ptr [esp + 20]    /* arg1 (E+4) при esp = E-16 */
        push ecx                     /* this: ECX на входе оригинала */
        call e0n_reg_impl
        add esp, 12                  /* сняли 3 аргумента: esp = E-12 */
        pop edx
        pop ecx
        pop eax                      /* esp = E — состояние входа оригинала */
        jmp dword ptr [g_e0n_reg_tramp]  /* трамплин играет пролог и даёт cont */
    }
}

/* Трамплин: воспроизводит украденные 8 байт пролога и продолжает 0x45dd08. */
__declspec(naked) void e0n_reg_tramp(void)
{
    __asm {
        sub esp, 0x18
        push esi
        mov esi, dword ptr [esp + 0x20]
        jmp dword ptr [g_e0n_reg_cont]
    }
}

/* Установка native-диага: из initldhook ПОСЛЕ install_site5/site6, только в
 * нативном режиме. Байтовая проверка пролога; несовпадение — запись и пропуск. */
int e0_native_install(void)
{
    char why[256];
    unsigned char *fn;
    int n = 0;

    if (!e0_diag_on()) {
        return 0;
    }
    if (s6_port_enabled()) {
        ld_info("E0DIAG: порт e0a0 активен — native-диаг не ставится");
        return 0;
    }
    if (g_site5_cont == NULL) {
        ld_info("E0DIAG: site5 не установлен — native-диаг пропущен");
        return 0;
    }
    fn = (unsigned char *)native::fn(E0N_VA_REG);
    if (fn == NULL) {
        ld_info("E0DIAG: нет base EXE — reg hook NOT installed");
        return 0;
    }
    if (memcmp(fn, E0N_REG_SIG, E0N_REG_LEN) != 0) {
        ld_info("E0DIAG: prologue 0x45dd00 mismatch — reg hook NOT installed");
    } else if (!rel32_fits(fn, (void *)e0n_reg_hook)) {
        ld_info("E0DIAG: reg hook E9 rel32 out of range");
    } else {
        g_e0n_reg_cont = (void *)(fn + E0N_REG_LEN);
        g_e0n_reg_tramp = (void *)e0n_reg_tramp;
        if (!write_jmp(fn, (void *)e0n_reg_hook, E0N_REG_LEN)) {
            ld_info("E0DIAG: reg hook write_jmp FAILED");
        } else {
            ld_info("E0DIAG: reg hook 0x45dd00 -> e0n_reg_hook (stolen %d B, "
                    "cont=0x%08x)", E0N_REG_LEN, (unsigned)(size_t)g_e0n_reg_cont);
        }
    }
    if (battle_capture_enabled()) {
        ld_info("E0DIAG: capture активен — CALL-сайты ветки 1 НЕ ставятся "
                "(захват не получает новых записей)");
    } else {
        leaf_sites_set_sink(&g_e0n_sink);
        why[0] = '\0';
        n = leaf_site_install_diag(native::fn, why, (int)sizeof(why));
        ld_info("E0DIAG: %d/%d CALL-сайта ветки 1 FUN_0045dd70 -> diag sink (%s)",
                n, leaf_site_diag_count(), why);
    }
    ld_info("E0DIAG: native-диаг включён (итерации — хвост site5 @0x45e163, "
            "регистрации — вход 0x45dd00)");
    return n;
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
/* E0-диаг: ветка 1 по гейтам — вызов lower_bound (= вход ветки 1) и пара
 * «nbr==NULL после lower_bound» (свободный путь). g_ai_reg_dup — регистрация
 * ключа, который lower_bound уже нашёл (двойная регистрация в map2). */
static volatile LONG g_ai_lb_calls = 0, g_ai_nbr_null = 0, g_ai_reg_dup = 0;
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
 * Фильтр [obj+8]==filter, минимум беззнакового [obj+0x24]; возврат LDObject*.
 * nodes — число узлов, обошедших проход; при diag оно суммируется в g_e0c_nodes
 * (глобальный счётчик, сбрасывается дельтой по вызову — least invasive: сама
 * логика прохода и порядок выбора не тронуты). */
LDObject *s6_scan_map(void *this_, int filter)
{
    TreeNode *head = ((BattleAI *)this_)->cand_head;
    TreeNode *cur;
    TreeNode *best = head;
    unsigned int best_metric = 999999999u;
    LDObject *obj;
    LONG nodes = 0;

    if (head == NULL) {
        return NULL;
    }
    cur = head->left;                    /* первый (левый) узел дерева */
    while (cur != head && cur != NULL) {
        nodes++;
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
    if (e0_diag_on()) {
        InterlockedAdd64(&g_e0c_nodes, (LONGLONG)nodes);
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

/* E0COST (9.d2d): live_before/live_after — тот же проход, но под AV-гейтом:
 * обход чужой tree при повреждённой куче ловится, значение = -1 (sentinel),
 * остальные поля строки сохраняются. Только при diag. */
static int s6_scan_count_safe(void *this_, int filter)
{
    int n = -1;
    __try {
        n = s6_scan_count(this_, filter);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = -1;
    }
    return n;
}

/* ==================================================================== */
/* 9.1a — индексированная min-куча кандидатов поиска цели (e0a0).        */
/*                                                                      */
/* Причина: оригинал на каждой итерации делает полный in-order проход     */
/* map1 (s6_scan_map / FUN_0045ce80), хотя map1 растёт до ~4900 узлов, а  */
/* меняется только вставкой новых узлов и разметкой выбранного           */
/* фильтром 3. Замер 9.d2d: скан = 61 % µs длинного вызова.              */
/*                                                                      */
/* Контракт (PLAN_BATTLE_AI_CPP.md 9.1a):                                */
/*   — куча ТОЛЬКО для вызова с filter==1: там ветка 2 FUN_0045dd70      */
/*     (единственное место, где метрика существующего узла может         */
/*     измениться) не исполняется (замер: b2=0), т.е. нужны только        */
/*     build + pop + insert. Для прочих filter остаётся родной скан;      */
/*   — ключ = (unsigned metric [obj+0x24], LDPos* pos [obj+0x00]); при     */
/*     равной метрике раньше идёт меньший адрес pos — так сканирует       */
/*     map1 (ключ карты = pos, std::less<LDPos*>, сырой адрес);           */
/*   — eligibility ровно как у скана: filter == filter_arg &&             */
/*     (unsigned)metric < 999999999u (сентинел скана);                    */
/*   — членство узла НЕ хранится в узле. Контракт 9.1a предлагал          */
/*     PathState+0x0c, но поле занято игрой: 45eee0 кладёт туда float-биты */
/*     дельты масштаба (move.cpp:458, сайт 0x45f20b, copy_fields 0x1fff) и */
/*     потребитель в нативе есть — запись индекса/маркера туда схлопывает */
/*     активность боя (735 вызовов e0a0 за прогон → 6–9). Поэтому индекс  */
/*     живёт только в массиве кучи: insert/pop работают без него, а        */
/*     произвольное удаление (только shadow-зеркало) ищет узел линейно;    */
/*     дубли отсекаются перепроверкой eligibility при pop (map1 дедуп-     */
/*     лицирует по ключу, куча — нет);                                     */
/*   — куча живёт один вызов e0a0, поля узлов не пишутся вообще,           */
/*     map1/map2 не трогаются.                                            */
/*                                                                      */
/* Режимы (env LDHOOK_E0_HEAP):                                          */
/*   unset/off — прежнее поведение (одна кэшированная проверка режима);   */
/*   shadow    — выбор ведёт родной скан, куча идёт параллельно: на       */
/*               каждой выдаче peek() сравнивается со сканом по           */
/*               указателю (NULL==NULL совпало); первые LD_HEAP_MISM_LINES */
/*               расхождений логируются; из кучи удаляется взятый сканом   */
/*               узел (зеркало — расхождения не каскадируются). Таймаут    */
/*               продолжает вызываться, но его срабатывание подавляется    */
/*               (лишний скан сделал бы его ранним); выход — по            */
/*               LDHOOK_E0_HEAP_BUDGET итераций (дефолт 20000) либо        */
/*               пустой скан;                                             */
/*   on        — куча заменяет скан для filter==1. Поведение обязано       */
/*               совпадать со сканом (гейт — shadow, 0 расхождений).      */
/*                                                                      */
/* Safety valve: живых узлов больше LD_HEAP_CAP (наблюдаемый максимум     */
/* 420) — остаток вызова идёт родным сканом, счётчики ovf/fallback.       */
/* Аллокаций нет: фиксированный static-массив; метрика не                 */
/* пересчитывается — только чтение поля.                                  */
/* ==================================================================== */

#define LD_HEAP_ENV        "LDHOOK_E0_HEAP"
#define LD_HEAP_BUDGET_ENV "LDHOOK_E0_HEAP_BUDGET"
#define LD_HEAP_CAP        8192u
#define LD_HEAP_METRIC_MAX 999999999u    /* сентинел s6_scan_map */
#define LD_HEAP_MISM_LINES 10            /* первые N расхождений — с деталями */
#define LD_HEAP_SUM_CALLS  25            /* период кумулятивной сводки (вызовов) */

#define LD_HEAP_OFF    0
#define LD_HEAP_ON     1
#define LD_HEAP_SHADOW 2

typedef struct {
    PathState *node;                     /* узел map1 (поля читаются на сравнении) */
} LdHeapEnt;

static struct {
    int          mode;                   /* LD_HEAP_OFF/ON/SHADOW (кэш env) */
    int          filter;                 /* filter аргумент вызова-владельца */
    int          active;                 /* 1 = куча принадлежит какому-то вызову */
    int          owner;                  /* 0 = владельца нет, иначе LD_HEAP_ON/SHADOW */
    int          nest;                   /* глубина входов e0a0 (учитываются все) */
    int          owner_nest;             /* g_lh.nest вызова-владельца */
    int          fallback;               /* 1 = куча снята → остаток на скане */
    unsigned int n;                      /* элементов в куче */
    LdHeapEnt    a[LD_HEAP_CAP];         /* фиксированное хранилище (32 КБ) */
    long         budget;                 /* shadow: предел итераций */
    /* --- счётчики текущего вызова --- */
    long pops, cmps, mism, missing, skip;
    long fb_skip;                        /* shadow: итераций без сверки (fallback) */
    long b2_drop;                        /* ветка 2 при активной куче (defensive) */
    long iters;                          /* shadow: номер итерации (для лога) */
    long live_max;                       /* максимум живых за вызов */
    long budget_hits, suppressed;
    /* --- сводка прогона --- */
    long run_calls, run_calls_heap, run_calls_shadow;
    long run_pops, run_cmps, run_mism, run_missing, run_skip;
    long run_fb_skip, run_b2_drop;
    long run_max_live, run_ovf, run_fallback, run_nested;
    long run_budget, run_supp;
} g_lh;

/* Компаратор: меньшая unsigned-метрика; при равенстве — меньший адрес pos
 * (порядок map1). */
static int ld_heap_less(const LdHeapEnt *x, const LdHeapEnt *y)
{
    unsigned int mx = (unsigned int)x->node->metric;
    unsigned int my = (unsigned int)y->node->metric;

    if (mx != my) {
        return mx < my;
    }
    return (unsigned int)(UINT_PTR)x->node->pos < (unsigned int)(UINT_PTR)y->node->pos;
}

static void ld_heap_swap(unsigned int i, unsigned int j)
{
    PathState *p = g_lh.a[i].node;

    g_lh.a[i].node = g_lh.a[j].node;
    g_lh.a[j].node = p;
}

static void ld_heap_up(unsigned int i)
{
    while (i > 0) {
        unsigned int p = (i - 1) / 2;

        if (!ld_heap_less(&g_lh.a[i], &g_lh.a[p])) {
            break;
        }
        ld_heap_swap(i, p);
        i = p;
    }
}

static void ld_heap_down(unsigned int i)
{
    for (;;) {
        unsigned int l = 2 * i + 1;
        unsigned int r = l + 1;
        unsigned int m = i;

        if (l < g_lh.n && ld_heap_less(&g_lh.a[l], &g_lh.a[m])) {
            m = l;
        }
        if (r < g_lh.n && ld_heap_less(&g_lh.a[r], &g_lh.a[m])) {
            m = r;
        }
        if (m == i) {
            break;
        }
        ld_heap_swap(i, m);
        i = m;
    }
}

/* Safety valve: живых больше ёмкости — куча отключается до конца вызова. */
static void ld_heap_overflow(void)
{
    g_lh.fallback = 1;
    g_lh.n = 0;
    g_lh.run_ovf++;
    /* iters в режиме on не ведётся (0) — в сообщении число извлечений. */
    ld_info("E0HEAP ovf: live > %u, fallback to linear scan (call %ld, pops=%ld)",
            LD_HEAP_CAP, g_lh.run_calls, g_lh.pops);
}

/* Вставка узла. Вызывается из s6_ai_update сразу после регистрации в map1.
 * Не активна → no-op. */
static void ld_heap_insert(PathState *node)
{
    /* Только вызов-владелец и только пока он самый внутренний: вложенный e0a0
     * работает на родном скане и чужую кучу не трогает. */
    if (!g_lh.active || g_lh.owner == 0 || g_lh.nest != g_lh.owner_nest ||
        g_lh.fallback || node == NULL) {
        return;
    }
    if (node->filter != g_lh.filter ||
        (unsigned int)node->metric >= LD_HEAP_METRIC_MAX) {
        return;                          /* скан такой узел не увидел бы */
    }
    if (g_lh.n >= LD_HEAP_CAP) {
        ld_heap_overflow();
        return;
    }
    g_lh.a[g_lh.n].node = node;
    g_lh.n++;
    ld_heap_up(g_lh.n - 1);
    if ((long)g_lh.n > g_lh.live_max) {
        g_lh.live_max = (long)g_lh.n;
    }
}

/* Eligibility ровно как у скана ce80. */
static int ld_heap_eligible(const PathState *p)
{
    return p != NULL && p->filter == g_lh.filter &&
           (unsigned int)p->metric < LD_HEAP_METRIC_MAX;
}

/* Снять минимум без проверки eligibility (индексного поля нет). */
static PathState *ld_heap_take(void)
{
    PathState *top;

    if (g_lh.n == 0) {
        return NULL;
    }
    top = g_lh.a[0].node;
    g_lh.n--;
    if (g_lh.n > 0) {
        g_lh.a[0].node = g_lh.a[g_lh.n].node;
        ld_heap_down(0);
    }
    return top;
}

/* Извлечение кандидата. NULL — кандидатов больше нет.
 * Узел перепроверяется на eligibility ровно как скан (filter/метрика): если
 * в кучу попал дубль одного узла (map1 дедуплицирует по ключу, куча — нет),
 * второй его экземпляр к этому моменту уже помечен filter=3 и выбрасывается —
 * скан такой узел тоже не вернул бы. */
static PathState *ld_heap_pop(void)
{
    PathState *top;

    while ((top = ld_heap_take()) != NULL) {
        if (ld_heap_eligible(top)) {
            g_lh.pops++;
            return top;
        }
        g_lh.skip++;                     /* дубль/невалидный — скан его не увидит */
    }
    return NULL;
}

/* Defensive (fix 3): ветка 2 FUN_0045dd70 пересчитывает метрику существующего
 * узла (mask 0x3c6) — ключи кучи после этого stale. При filter==1 ветка мертва
 * (гейт cur->filter==2, замер b2=0); если всё же исполнится — куча снимается,
 * остаток вызова идёт родным сканом. */
static void ld_heap_drop_branch2(void)
{
    if (g_lh.active && g_lh.owner != 0 && g_lh.nest == g_lh.owner_nest &&
        !g_lh.fallback) {
        g_lh.fallback = 1;
        g_lh.n = 0;
        g_lh.b2_drop++;
        ld_info("E0HEAP b2: branch 2 at filter=%d, heap dropped (call %ld, pops=%ld)",
                g_lh.filter, g_lh.run_calls, g_lh.pops);
    }
}

/* Минимум без извлечения (только shadow-сверка). */
static PathState *ld_heap_peek(void)
{
    return (g_lh.n != 0 && !g_lh.fallback) ? g_lh.a[0].node : NULL;
}

/* Произвольное удаление (shadow-зеркало): узел ищется линейно по массиву
 * (индексного поля в узле нет — PathState+0x0c занят игрой).
 * 0 — узла в куче не было. */
static int ld_heap_remove(PathState *node)
{
    unsigned int i;
    PathState *moved;

    if (node == NULL || !g_lh.active || g_lh.fallback) {
        return 0;
    }
    for (i = 0; i < g_lh.n; i++) {
        if (g_lh.a[i].node == node) {
            break;
        }
    }
    if (i == g_lh.n) {
        return 0;
    }
    g_lh.n--;
    if (i != g_lh.n) {
        moved = g_lh.a[g_lh.n].node;
        g_lh.a[i].node = moved;
        ld_heap_up(i);
        for (i = 0; i < g_lh.n; i++) {   /* moved мог уехать вверх */
            if (g_lh.a[i].node == moved) {
                break;
            }
        }
        ld_heap_down(i);
    }
    return 1;
}

/* shadow: сверка минимума кучи с результатом полного скана + зеркальное
 * удаление взятого узла. scan_pick — то, что вернул скан (NULL — скан пуст).
 * Вызывается только вызовом-владельцем (fix 1). */
static void ld_heap_shadow_check(void *scan_pick)
{
    PathState *top;

    if (!g_lh.active || g_lh.owner != LD_HEAP_SHADOW ||
        g_lh.nest != g_lh.owner_nest) {
        return;
    }
    if (g_lh.fallback) {
        g_lh.fb_skip++;                  /* сверять не с чем: куча снята */
        return;
    }
    /* Drain: невалидные вершины скан бы не вернул — снимаем их ДО сверки,
     * иначе они засчитывались бы как расхождение, а не как skip. */
    while ((top = ld_heap_peek()) != NULL && !ld_heap_eligible(top)) {
        ld_heap_take();
        g_lh.skip++;
    }
    top = ld_heap_peek();
    g_lh.cmps++;
    if (top != (PathState *)scan_pick) {
        g_lh.mism++;
        if (g_lh.mism <= LD_HEAP_MISM_LINES) {
            int hm = -1, hf = -1, sm = -1, sf = -1;

            if (top != NULL) {
                hm = top->metric;
                hf = top->filter;
            }
            if (scan_pick != NULL) {
                sm = ((LDObject *)scan_pick)->metric;
                sf = ((LDObject *)scan_pick)->filter;
            }
            ld_info("E0HEAPMISM n=%ld it=%ld heap=%p m=%u f=%d scan=%p m=%d f=%d",
                    g_lh.run_calls, g_lh.iters, (void *)top,
                    (top != NULL) ? (unsigned int)top->metric : 0u, hf,
                    scan_pick, sm, sf);
        }
    }
    if (!ld_heap_remove((PathState *)scan_pick) && scan_pick != NULL) {
        g_lh.missing++;                  /* скан взял узел, которого в куче нет */
    }
}

static void ld_heap_summary(const char *why)
{
    static const char *const names[3] = { "off", "on", "shadow" };

    if (g_lh.mode == LD_HEAP_OFF) {
        return;
    }
    ld_info("E0HEAP %s mode=%s calls=%ld heap_calls=%ld shadow_calls=%ld "
            "pops=%ld cmps=%ld mism=%ld max_live=%ld ovf=%ld fallback=%ld "
            "nested=%ld skip=%ld missing=%ld fb_skip=%ld b2_drop=%ld "
            "budget_hits=%ld suppressed=%ld",
            why, names[g_lh.mode], g_lh.run_calls, g_lh.run_calls_heap,
            g_lh.run_calls_shadow, g_lh.run_pops, g_lh.run_cmps, g_lh.run_mism,
            g_lh.run_max_live, g_lh.run_ovf, g_lh.run_fallback, g_lh.run_nested,
            g_lh.run_skip, g_lh.run_missing, g_lh.run_fb_skip, g_lh.run_b2_drop,
            g_lh.run_budget, g_lh.run_supp);
}

/* Финальная сводка процесса (atexit). Игру наш раннер гасит taskkill /F —
 * тогда строки нет, и данные несёт периодическая E0HEAPSUM. */
static void __cdecl ld_heap_atexit(void)
{
    ld_heap_summary("final");
}

static void ld_heap_init_once(void)
{
    const char *e = getenv(LD_HEAP_ENV);
    const char *b;
    int budget;

    if (e == NULL || *e == '\0') {
        g_lh.mode = LD_HEAP_ON;     /* принято 9.1b: по умолчанию включено;
                                     * env off/shadow переопределяет */
    } else if (e[0] == 's' || e[0] == 'S') {
        g_lh.mode = LD_HEAP_SHADOW;
    } else if ((e[0] == 'o' || e[0] == 'O') && (e[1] == 'n' || e[1] == 'N')) {
        g_lh.mode = LD_HEAP_ON;
    } else {
        g_lh.mode = (atoi(e) != 0) ? LD_HEAP_ON : LD_HEAP_OFF;
    }
    b = getenv(LD_HEAP_BUDGET_ENV);
    budget = (b != NULL) ? atoi(b) : 0;
    g_lh.budget = (budget > 0) ? (long)budget : 20000L;
    if (g_lh.mode != LD_HEAP_OFF) {
        atexit(ld_heap_atexit);
        ld_info("E0HEAP mode=%s cap=%u budget=%ld (%s)",
                (g_lh.mode == LD_HEAP_ON) ? "on" : "shadow", LD_HEAP_CAP,
                g_lh.budget, LD_HEAP_ENV);
    }
}

static int ld_heap_mode(void)
{
    static int on = -1;

    if (on < 0) {
        ld_heap_init_once();
        on = g_lh.mode;
    }
    return on;
}

/* shadow: таймаут выключен, покрытие ограничено бюджетом итераций.
 * Только для вызова-владельца. */
static int ld_heap_shadow_stop(long iters)
{
    if (!g_lh.active || g_lh.owner != LD_HEAP_SHADOW ||
        g_lh.nest != g_lh.owner_nest) {
        return 0;
    }
    if (iters <= g_lh.budget) {
        return 0;
    }
    g_lh.budget_hits++;
    return 1;
}

/* Начало вызова: build одним проходом map1.
 * Возврат — токен владения кучей:
 *   0              — куча НЕ принадлежит этому вызову (off, filter!=1,
 *                    вложенный вход, fallback): кандидатов даёт родной скан;
 *   LD_HEAP_ON     — вызов владеет кучей, выбор из кучи;
 *   LD_HEAP_SHADOW — вызов владеет кучей, выбор у скана (сверка).
 * Владелец ровно один: вложенный e0a0 (g_lh.owner != 0) получает 0 и чужую
 * кучу не трогает — ни insert, ни сверкой, ни сбросом на выходе. */
static int ld_heap_begin(BattleAI *self, int filter)
{
    TreeNode *head;
    TreeNode *cur;
    int live = 0;
    int token;

    if (ld_heap_mode() == LD_HEAP_OFF) {
        return 0;                        /* off: только чтение кэша режима */
    }
    g_lh.nest++;                         /* учитываются все входы e0a0 */
    if (filter != 1) {
        return 0;                        /* контракт 9.1a: куча только для filter==1 */
    }
    if (g_lh.owner != 0) {
        g_lh.run_nested++;               /* вложенный e0a0: родной скан */
        return 0;
    }
    token = (g_lh.mode == LD_HEAP_ON) ? LD_HEAP_ON : LD_HEAP_SHADOW;
    g_lh.active = 1;
    g_lh.owner = token;
    g_lh.owner_nest = g_lh.nest;
    g_lh.fallback = 0;
    g_lh.n = 0;
    g_lh.filter = filter;
    g_lh.pops = g_lh.cmps = g_lh.mism = g_lh.missing = g_lh.skip = 0;
    g_lh.fb_skip = g_lh.b2_drop = 0;
    g_lh.live_max = 0;
    g_lh.budget_hits = g_lh.suppressed = 0;
    g_lh.iters = 0;
    g_lh.run_calls++;
    if (g_lh.mode == LD_HEAP_ON) {
        g_lh.run_calls_heap++;
    } else {
        g_lh.run_calls_shadow++;
    }

    /* Build: один проход map1, вставляем подходящие. Порядок обхода не важен —
     * кучу упорядочивает компаратор. Полей узлов не пишем вообще (в т.ч.
     * PathState+0x0c — он занят игрой, см. комментарий в шапке блока). */
    head = self->cand_head;
    if (head != NULL) {
        cur = head->left;
        while (cur != head && cur != NULL) {
            PathState *node = (PathState *)cur->value;

            if (node != NULL) {
                ld_heap_insert(node);    /* eligibility — внутри */
            }
            cur = s6_tree_next(cur);
        }
        if (g_lh.n > 0) {
            live = (int)g_lh.n;
        }
    }
    if (live > g_lh.live_max) {
        g_lh.live_max = live;
    }
    return g_lh.fallback ? 0 : token;
}

/* Конец вызова. Кучу сбрасывает и сводку накапливает только владелец
 * (token != 0); любой другой вызов лишь выходит из вложенности. */
static void ld_heap_end(int token)
{
    if (g_lh.mode == LD_HEAP_OFF) {
        return;
    }
    if (token != 0) {
        g_lh.run_pops += g_lh.pops;
        g_lh.run_cmps += g_lh.cmps;
        g_lh.run_mism += g_lh.mism;
        g_lh.run_missing += g_lh.missing;
        g_lh.run_skip += g_lh.skip;
        g_lh.run_fb_skip += g_lh.fb_skip;
        g_lh.run_b2_drop += g_lh.b2_drop;
        g_lh.run_budget += g_lh.budget_hits;
        g_lh.run_supp += g_lh.suppressed;
        if (g_lh.live_max > g_lh.run_max_live) {
            g_lh.run_max_live = g_lh.live_max;
        }
        if (g_lh.fallback) {
            g_lh.run_fallback++;
        }
        g_lh.active = 0;
        g_lh.owner = 0;
        g_lh.fallback = 0;
        g_lh.n = 0;
    }
    if (g_lh.nest > 0) {
        g_lh.nest--;
    }
    /* Сводка: первые 8 вызовов (короткий прогон в arm EEE0=0 даёт единицы
     * вызовов) и далее каждые LD_HEAP_SUM_CALLS. Только от владельца. */
    if (token != 0 &&
        (g_lh.run_calls <= 8 || (g_lh.run_calls % LD_HEAP_SUM_CALLS) == 0)) {
        ld_heap_summary("sum");
    }
}

/* Следующий кандидат: куча (heap_on) либо родной скан. Переполнение кучи
 * снимает heap_on на остаток вызова — поведение остаётся родным. */
static LDObject *s6_take_candidate(void *this_, int filter, int *heap_on)
{
    if (*heap_on) {
        if (!g_lh.fallback) {
            return (LDObject *)ld_heap_pop();
        }
        *heap_on = 0;                    /* safety valve */
    }
    return s6_scan_map(this_, filter);
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
    /* E0COST (9.d2d): diag-мерки этого входа. diag==0 → edep<0, ни одна
     * ветка ниже не выполняется, поведение побайтно прежнее. */
    const int diag = e0_diag_on();
    long edep = -1;
    LONGLONG tu = 0, tp = -1;

    InterlockedIncrement(&g_ai_calls);
    /* H7: тот же счётчик vis, что у нативного 0x45dd70 (в порт-режиме он не
     * вызывается). Рекурсия s6_ai_update считает каждый вход, как native. */
    battle_trace_visit();

    if (diag) {
        tu = ld_now_us();
        edep = InterlockedIncrement(&g_e0c_depth) - 1;
    }

    if (cur == NULL) {
        e0c_ai_leave(edep, tu);
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
            e0c_ai_leave(edep, tu);
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

                    InterlockedIncrement(&g_ai_lb_calls);  /* CALL 0x434bc0 @0x45decb = вход ветки 1 */
                    native::lower_bound_slots(&self->map2, &w[0], &key);  /* @0x45dec7 out=&L[0x38], @0x45dec3 key=&L[0x2c] */
                    if ((void *)(INT_PTR)w[0] != self->occ_head) {        /* @0x45ded2/0x45dedc: узел != head → занят */
                        nbr = (PathState *)(INT_PTR)((const TreeNode *)(INT_PTR)w[0])->value;  /* узел+0x10 @0x45ded7 */
                    }
                    if (nbr == NULL) {                        /* своб. путь (head или узел с нулевым значением) */
                        InterlockedIncrement(&g_ai_nbr_null);
                        cell = s6_grid_lookup(level, x, y);   /* @0x45dee2..0x45def1 + CALL 0x456ed0 */
                        if (cell == NULL) {
                            InterlockedIncrement(&g_ai_cell_null);
                            seed = 0; continue;               /* @0x45defc JE → хвост цикла (xor esi @0x45e05a) */
                        }
                        ai_stage = 10;                        /* grid lookup done */
                        sp[0] = (int)(INT_PTR)cell;           /* spec[0x00]=cell @0x45df05 */
                        sp[4] = *(const int *)&cur->out_metric;  /* spec[0x10]=cur->[0x10] @0x45df13 */
                        ai_stage = 20;                        /* в cb40 */
                        if (diag) {
                            tp = ld_now_us();                 /* E0COST: время внутри cb40 */
                        }
                        path_res = s6_path_search(self, (PathState *)sp);  /* @0x45df17 RET 8 */
                        if (tp >= 0) {
                            InterlockedAdd64(&g_e0c_path_us, ld_now_us() - tp);
                            tp = -1;
                        }
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
                        if ((void *)(INT_PTR)w[0] != self->occ_head) {
                            InterlockedIncrement(&g_ai_reg_dup);  /* ключ уже в map2 */
                        }
                        native::register_occupied_slot(self, (LDObject *)nbr);  /* CALL 0x45dd00 @0x45dfa6 — только ветка 1 */
                        /* 9.1a: узел попал в map1 — в кучу. Строго после copy_fields
                         * (бит 3 маски 0x1fff пишет +0x0c=0) и после регистрации
                         * (видимость узла = видимости скана). Куча не активна → no-op. */
                        ld_heap_insert(nbr);
                        free_done = 1;                        /* @0x45dfab JMP 0x45e040 */
                    }
                }

                if (!free_done) {                             /* ветка 2 (@0x45dfb0..0x45e037) */
                    ai_stage = 80;                            /* branch 2 */
                    if (cur->filter == 2 && nbr->filter != 3) {  /* @0x45dfb0/0x45dfba/0x45dfc0 */
                        /* 9.1a (fix 3): здесь пересчитывается метрика существующего
                         * узла (mask 0x3c6) — ключи кучи стали бы stale. При
                         * filter==1 гейт не проходит (замер b2=0); срабатывание —
                         * сигнал нарушения контракта: снимаем кучу до конца вызова. */
                        ld_heap_drop_branch2();
                        if (diag) {
                            InterlockedIncrement(&g_e0c_b2);  /* E0COST: тело ветки 2 исполнено */
                        }
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
        e0c_ai_leave(edep, tu);
        LD_CRASH_RET(0);
    }

    (void)arg2;
    e0c_ai_leave(edep, tu);
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
 * постоянная (native c4b0 mask 4), restore не вызывается.
 * 9.1c: при LDHOOK_E0_MAXITER=n>0 рядом с таймаутом стоит потолок итераций —
 * тот же выход (code=3, return cur), но по счётчику, а не по настенным часам.
 * 9.1d: рядом с ним же — потолок времени LDHOOK_E0_MAXUS=µs (часы снимаются
 * раз в 32 итераций и на итерации капа итераций); выход тот же, capped=2.
 */
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
    LONG d_lb, d_nbrnull, d_dup;
    long ps0[7], ps1[7];
    /* LDHOOK_E0_DIAG: снимок визитов цикла (нет аллокаций — фиксированный стек;
     * diag==0 → все ветки пропущены, поведение побайтно прежнее). */
    const int diag = e0_diag_on();
    E0DiagSlot e0sl[E0_DIAG_TABLE];
    LONG e0n = 0, e0ovf = 0, e0nulls = 0, e0rep = 0;
    LONG e0mchg = 0, e0msame = 0, e0f3 = 0, e0f1 = 0;
    long e0reg0 = (long)g_reg_calls;
    /* E0COST (9.d2d): снимок аккумуляторов на входе; us_scan — время всех
     * сканов вызова (считается локально, аллокаций нет). */
    long c_b20 = 0;
    LONGLONG c_nodes0 = 0;
    LONGLONG c_upd0 = 0, c_path0 = 0, c_lb0 = 0;
    long live_before = 0;
    LONGLONG us_scan = 0, ts = -1;
    /* Дельты за вызов (E0COST) и длительность вызова при diag. */
    LONGLONG us_total = 0, d_upd = 0, d_path = 0, d_lbus = 0;
    LONGLONG d_nodes = 0;
    long d_b2 = 0, live_after = 0;
    /* E0PATH (план 9.2a): снимок path-аккумуляторов на входе, дельты за вызов.
     * diag==0 → снимка нет, ни одной записи (аккумуляторы не пишутся). */
    long p0_calls = 0, p0_null = 0, p0_lb = 0, p0_grid = 0, p0_ts = 0, p0_recur = 0;
    long p0_cells = 0, p0_dupc = 0, p0_dupt = 0, p0_ovf = 0;
    LONGLONG p0_lb_us = 0, p0_grid_us = 0, p0_ts_us = 0;
    long d_pcalls = 0, d_pnull = 0, d_plb = 0, d_pgrid = 0, d_pts = 0, d_precur = 0;
    long d_pcells = 0, d_dupc = 0, d_dupt = 0, d_povf = 0;
    LONGLONG d_plb_us = 0, d_pgrid_us = 0, d_pts_us = 0;
    /* 9.1a: индексированная min-куча для filter==1 (LDHOOK_E0_HEAP).
     * heap_owner — токен владения: 0 = куча не наша (off/filter!=1/вложенный
     * вызов/fallback), иначе LD_HEAP_ON или LD_HEAP_SHADOW. heap_on==1 —
     * кандидата даёт куча; 0 — родной скан (off/shadow/fallback). */
    int heap_owner = 0;
    int heap_on = 0;
    /* 9.1c/9.1d: потолок итераций и потолок времени вызова (0 = нет) и
     * признак обрыва по капу (идёт в E0COST как capped=0/1/2:
     * 1 = потолок итераций, 2 = потолок времени). */
    long cap_iters = s6_maxiter_get();
    long cap_us = s6_maxus_get();
    int capped = 0;

    if (diag) {
        memset(e0sl, 0, sizeof(e0sl));   /* count==0 = слот пуст */
        live_before = s6_scan_count_safe(this_, filter);  /* живые узлы на входе,
                                                            * AV → -1 */
        c_nodes0 = (LONGLONG)g_e0c_nodes;
        c_b20 = (long)g_e0c_b2;
        c_upd0 = (LONGLONG)g_e0c_upd_us;
        c_path0 = (LONGLONG)g_e0c_path_us;
        c_lb0 = (LONGLONG)g_e0c_lb_us;
        /* E0PATH: ключ повторов этого вызова + снимок path-аккумуляторов. */
        e0path_call((long)n);
        e0path_target(cmp_val);
        p0_calls  = g_e0p_calls;  p0_null = g_e0p_null;   p0_lb = g_e0p_lb;
        p0_grid   = g_e0p_grid;   p0_ts   = g_e0p_ts;     p0_recur = g_e0p_recur;
        p0_cells  = g_e0p_cells;  p0_dupc = g_e0p_dup_call;
        p0_dupt   = g_e0p_dup_tick; p0_ovf = g_e0p_ovf;
        p0_lb_us  = g_e0p_lb_us;  p0_grid_us = g_e0p_grid_us;
        p0_ts_us  = g_e0p_ts_us;
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
    /* 9.1a: куча строится на входе (один проход map1) и отдаёт минимум вместо
     * полного пере-скана; filter!=1 → heap_on=0, всё как раньше. */
    heap_owner = ld_heap_begin((BattleAI *)this_, filter);
    heap_on = (heap_owner == LD_HEAP_ON);
    if (diag) {
        ts = ld_now_us();                   /* E0COST: время первого скана */
    }
    cur = s6_take_candidate(this_, filter, &heap_on);   /* @0x45e103 */
    if (ts >= 0) {
        if (!heap_on) {
            us_scan += ld_now_us() - ts;
        }
        ts = -1;
    }
    if (heap_owner == LD_HEAP_SHADOW) {
        ld_heap_shadow_check(cur);          /* 9.1a: peek() против полного скана */
    }
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
        d_lb = g_ai_lb_calls; d_nbrnull = g_ai_nbr_null; d_dup = g_ai_reg_dup;
        s6_pfs_snapshot(ps0);
    }
    map1_before = ((BattleAI *)this_)->reserved_70;
    while (cur != NULL) {                  /* @0x45e10a / @0x45e165 */
        iters++;
        /* 9.1a shadow: выход по таймауту выключен (лишний скан сделал бы его
         * ранним), покрытие ограничено бюджетом итераций. */
        if (heap_owner == LD_HEAP_SHADOW && ld_heap_shadow_stop(iters)) {
            g_lh.iters = iters;
            break;
        }
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
        /* 9.1c: потолок итераций. Тот же выход, что у таймаута: код 3,
         * возврат cur; проверка рядом с s6_check_timeout и ДО него, чтобы
         * бюджет итераций гасил вызов, а не настенные часы. Потолок
         * не зависит от режима кучи (общий цикл). */
        /* 9.1d: потолок времени того же вызова — тот же выход и то же место.
         * Итерационный потолок время не ограничивает (цена итерации = размеру
         * карты), поэтому часы снимаем раз в E0CAP_US_CHECK итераций и
         * обязательно на той итерации, где срабатывает потолок итераций.
         * cap_us==0 → ветка не выполняется ни разу (ни одного обращения к
         * QPC). Капы независимы и складываются: сработавший первым пишет
         * capped=1 (итерации) или capped=2 (время). */
        if (cap_us > 0 &&
                ((iters % E0CAP_US_CHECK) == 0 ||
                 (cap_iters > 0 && iters >= cap_iters))) {
            LONGLONG el = ld_now_us() - t0;
            InterlockedIncrement(&g_e0cap_us_checks);
            if (el >= cap_us) {
                InterlockedIncrement(&g_e0cap_us_hits);
                InterlockedAdd(&g_e0cap_iters, iters);
                if (iters > g_e0cap_max_iter) {
                    g_e0cap_max_iter = iters;
                }
                if (el > g_e0cap_us_max) {
                    g_e0cap_us_max = el;
                }
                capped = 2;
                code = 3;                                      /* как @0x45e1af */
                result = cur;
                break;
            }
        }
        if (cap_iters > 0 && iters >= cap_iters) {
            InterlockedIncrement(&g_e0cap_hits);
            InterlockedAdd(&g_e0cap_iters, iters);
            if (iters > g_e0cap_max_iter) {
                g_e0cap_max_iter = iters;
            }
            capped = 1;
            code = 3;                                      /* как @0x45e1af */
            result = cur;
            break;
        }
        if (s6_check_timeout(this_, arg2)) {               /* @0x45e14e */
            if (heap_owner == LD_HEAP_SHADOW) {
                /* 9.1a shadow: гейт вызывается (он только читает таймер и
                 * this->timer_start_sec), но выход подавлен — нужен полный
                 * охват итераций; срабатывание считается. */
                g_lh.iters = iters;
                g_lh.suppressed++;
            } else {
                code = 3;                                  /* @0x45e1af: *param_4 = 3 */
                result = cur;
                break;
            }
        }
        if (diag) {
            ts = ld_now_us();               /* E0COST: время rescan-прохода */
        }
        cur = s6_take_candidate(this_, filter, &heap_on);   /* @0x45e15e: rescan */
        if (ts >= 0) {
            if (!heap_on) {
                us_scan += ld_now_us() - ts;
            }
            ts = -1;
        }
        if (heap_owner == LD_HEAP_SHADOW) {
            g_lh.iters = iters;
            ld_heap_shadow_check(cur);      /* 9.1a: peek() против полного скана */
        }
        if (diag && cur == NULL) {
            e0nulls++;                                     /* пустой скан → выход */
        }
        battle_trace_e0a0_latch(cur);                      /* H6: EAX @0x45e163 */
#ifdef BATTLE_REPLAY
        replay_note_rescan(cur);                           /* iter_seq порта (v5) */
#endif
        InterlockedIncrement(&g_site6_rot);                /* ротация (п. 3.4) */
    }

    /* 9.1a: куча живёт один вызов — сброс/сводка только у владельца. */
    ld_heap_end(heap_owner);

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

    /* E0COST (9.d2d): дельты аккумуляторов за этот вызов + его длительность.
     * Один QPC-снимок на вызов при diag (env не задан → ветка пропущена).
     * live_after — полный проход s6_scan_count на выходе: размер кучи, который
     * реально держал бы индекс минимума (9.1a). */
    if (diag) {
        int map1_now = ((BattleAI *)this_)->reserved_70;
        us_total = ld_now_us() - t0;
        d_nodes = (LONGLONG)g_e0c_nodes - c_nodes0;
        d_b2 = (long)g_e0c_b2 - c_b20;
        d_upd = (LONGLONG)g_e0c_upd_us - c_upd0;
        d_path = (LONGLONG)g_e0c_path_us - c_path0;
        d_lbus = (LONGLONG)g_e0c_lb_us - c_lb0;
        live_after = s6_scan_count_safe(this_, filter);   /* AV → -1 */
        if (us_scan + d_upd > us_total || d_path > d_upd || d_lbus > d_upd) {
            InterlockedIncrement(&g_e0c_bad);             /* самосогласованность */
        }
        InterlockedIncrement(&g_e0c_calls);
        InterlockedAdd(&g_e0c_iters, iters);
        InterlockedAdd64(&g_e0c_us_sum, us_total);
        InterlockedAdd64(&g_e0c_scan_sum, us_scan);
        InterlockedAdd64(&g_e0c_upd_sum, d_upd);
        InterlockedAdd64(&g_e0c_path_sum, d_path);
        InterlockedAdd64(&g_e0c_lb_sum, d_lbus);
        InterlockedAdd64(&g_e0c_nodes_sum, d_nodes);
        InterlockedAdd(&g_e0c_b2_sum, d_b2);
        InterlockedAdd(&g_e0c_live_sum, live_after);
        InterlockedAdd(&g_e0c_map1_sum, map1_now);
        InterlockedAdd(&g_e0c_map1b_sum, map1_before);
        /* E0PATH: дельты path-аккумуляторов за этот вызов + полные итоги прогона. */
        d_pcalls  = g_e0p_calls  - p0_calls;   d_pnull = g_e0p_null - p0_null;
        d_plb     = g_e0p_lb     - p0_lb;      d_pgrid = g_e0p_grid - p0_grid;
        d_pts     = g_e0p_ts     - p0_ts;      d_precur = g_e0p_recur - p0_recur;
        d_pcells  = g_e0p_cells  - p0_cells;   d_dupc = g_e0p_dup_call - p0_dupc;
        d_dupt    = g_e0p_dup_tick - p0_dupt;  d_povf = g_e0p_ovf - p0_ovf;
        d_plb_us  = g_e0p_lb_us  - p0_lb_us;
        d_pgrid_us = g_e0p_grid_us - p0_grid_us;
        d_pts_us  = g_e0p_ts_us  - p0_ts_us;
        InterlockedAdd(&g_e0ps_calls, d_pcalls);
        InterlockedAdd(&g_e0ps_null, d_pnull);
        InterlockedAdd(&g_e0ps_lb, d_plb);
        InterlockedAdd(&g_e0ps_grid, d_pgrid);
        InterlockedAdd(&g_e0ps_ts, d_pts);
        InterlockedAdd(&g_e0ps_recur, d_precur);
        InterlockedAdd(&g_e0ps_cells, d_pcells);
        InterlockedAdd(&g_e0ps_dupc, d_dupc);
        InterlockedAdd(&g_e0ps_dupt, d_dupt);
        InterlockedAdd(&g_e0ps_ovf, d_povf);
        InterlockedAdd64(&g_e0ps_lb_us, d_plb_us);
        InterlockedAdd64(&g_e0ps_grid_us, d_pgrid_us);
        InterlockedAdd64(&g_e0ps_ts_us, d_pts_us);
    }

    /* LDHOOK_E0_DIAG: сводка вызова с итерациями выше порога (runaway-класс). */
    if (diag && iters > E0_DIAG_ITERS) {
        e0_diag_emit((long)n, (long)iters, (long)e0n, (long)e0ovf,
                     (long)e0rep, (long)e0mchg, (long)e0msame,
                     (long)e0f3, (long)e0f1,
                     (long)e0nulls, code, map1_before,
                     ((BattleAI *)this_)->reserved_70,
                     (long)g_reg_calls - e0reg0, e0sl);
        ld_info("E0DIAG2 call=%ld lb=%ld nbrnull=%ld dup=%ld cellnull=%ld "
                "pathnull=%ld pfsnull=%ld tick=%ld arm=port",
                (long)n, (long)g_ai_lb_calls - d_lb,
                (long)g_ai_nbr_null - d_nbrnull, (long)g_ai_reg_dup - d_dup,
                (long)g_ai_cell_null - d_cellnull,
                (long)g_ai_path_null - d_pathnull,
                (long)g_ai_pfs_null - d_pfsnull,
                battle_trace_tick_count());
        e0c_emit((long)n, filter, (long)iters, code, capped, us_total, us_scan,
                 d_upd, d_path, d_lbus, d_nodes, live_before, live_after,
                 d_b2, map1_before, ((BattleAI *)this_)->reserved_70,
                 battle_trace_tick_count());
        e0path_emit((long)n, d_pcalls, d_path, d_plb, d_plb_us, d_pgrid,
                    d_pgrid_us, d_pts, d_precur, d_pts_us,
                    (long)g_e0p_depth_max, d_pcells, d_pnull, d_dupc, d_dupt,
                    d_povf, (long)iters, battle_trace_tick_count());
    }
    /* E0COSTSUM: агрегат прогона по ВСЕМ измеренным вызовам (короткие тоже) —
     * доля e0a0 во времени боя считается офлайн против суммы `us` всех
     * активных тиков (battle_invariants.py --profile). Период 250 вызовов:
     * прогон ~800 вызовов даёт 3 сводки, хвост виден по последней. */
    if (diag && (n % E0C_SUM_CALLS) == 0) {
        ld_info("E0COSTSUM tick=%ld call=%ld filter=%d iters=%ld code=%d calls=%ld "
                "iters_sum=%ld us=%I64d scan=%I64d update=%I64d path=%I64d lb=%I64d "
                "nodes=%I64d live_after=%ld b2=%ld map1=%ld->%ld bad=%ld depth=%ld "
                "arm=port",
                battle_trace_tick_count(), (long)n, filter, (long)iters, code,
                (long)g_e0c_calls, (long)g_e0c_iters,
                (LONGLONG)g_e0c_us_sum, (LONGLONG)g_e0c_scan_sum,
                (LONGLONG)g_e0c_upd_sum, (LONGLONG)g_e0c_path_sum,
                (LONGLONG)g_e0c_lb_sum, (LONGLONG)g_e0c_nodes_sum,
                (long)g_e0c_live_sum, (long)g_e0c_b2_sum,
                (long)g_e0c_map1b_sum, (long)g_e0c_map1_sum,
                (long)g_e0c_bad, (long)g_e0c_depth);
        e0pathsum_emit(battle_trace_tick_count());
    }
    /* Периодическая сводка порта (агрегат прогона, каждые 2000 вызовов). */
    if (diag && (n % E0N_SUM_CALLS) == 0) {
        ld_info("E0DIAGSUM arm=port calls=%ld iter_sum=%ld heavy=%ld code1=%ld "
                "code3=%ld code4=%ld reg=%ld lb=%ld nbrnull=%ld dup=%ld cells=%ld "
                "cellnull=%ld pathnull=%ld pfsnull=%ld tick=%ld",
                (long)n, (long)g_site6_iter_sum, (long)g_site6_heavy,
                (long)g_site6_code1, (long)g_site6_code3, (long)g_site6_code4,
                (long)g_reg_calls, (long)g_ai_lb_calls, (long)g_ai_nbr_null,
                (long)g_ai_reg_dup, (long)g_ai_calls, (long)g_ai_cell_null,
                (long)g_ai_path_null, (long)g_ai_pfs_null,
                battle_trace_tick_count());
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
    /* 9.1c/9.1d: сводка E0CAP (только при заданном потолке; не на каждый вызов). */
    if ((cap_iters > 0 || cap_us > 0) && (n % E0CAP_PERIOD) == 0) {
        e0cap_emit("sum");
    }
    {
        LONGLONG now = ld_now_us();        /* Фаза 4 A/B: длительность вызова */
        LONGLONG us = now - t0;
        /* E0TICK (план 9.2a): аккумулятор тика ( calls/us/max/cap/path/lb ).
         * Вне диага — no-op: trace-аккумуляторы не пишутся. */
        battle_trace_e0a0_done(us, capped, d_path, d_lbus);
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
