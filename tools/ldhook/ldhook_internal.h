/* ldhook_internal.h — общие объявления между translation units ldhook.pyd
 * (Фаза 0, PLAN_BATTLE_AI_CPP.md): логгер, троттлинг и замер времени, утилиты
 * установки хуков, трамплины и счётчики сайтов.
 * ОпределяющиеTU: логгер/утилиты — ldhook.cpp; сайт1/2/3 — sites_hd.cpp;
 * сайт5/6 — battle/target.cpp.
 */
#ifndef LDHOOK_INTERNAL_H
#define LDHOOK_INTERNAL_H

#include <windows.h>

#include "battle/structs.h"

/* ---------------- Логгер (определяется в ldhook.cpp) ---------------- */
/* Один лог движка — hdlog.txt в каталоге EXE; уровень задаётся env LDHOOK_LOG
 * (0=off, 1=info — дефолт, 2=debug), ldhook.set_level(n) перекрывает на лету.
 * Ротация: файл > 8 MiB → hdlog.txt.1. */
#define LDHOOK_LOG_NAME "hdlog.txt"
#define LDHOOK_LOG_ENV  "LDHOOK_LOG"
#define LDHOOK_LOG_MAX  (8u * 1024u * 1024u)

#define LD_OFF   0
#define LD_INFO  1
#define LD_DEBUG 2

extern volatile LONG g_log_level;

void ld_info(const char *fmt, ...);
void ld_debug(const char *fmt, ...);
/* Троттлинг трассировки: первые first вызовов, затем каждый each-й. */
int ld_rate(volatile LONG *ctr, LONG first, LONG each);
/* Замер времени (QueryPerformanceCounter, микросекунды; частота кэшируется). */
LONGLONG ld_now_us(void);

/* ---------------- Crash-диагностика (LDHOOK_CRASHLOG) ---------------- */
/* Всё ниже — no-op при g_crash_on == 0 (env не задан): один предсказуемый
 * branch на входе функции, без записи в память. VEH (первый шанс AV) ставится
 * один раз из initldhook, пишет одну строку CRASH в тот же hdlog.txt
 * (отдельный handle, без CriticalSection/malloc/Python API) и НЕ глотает
 * исключение (EXCEPTION_CONTINUE_SEARCH). */
#define LDHOOK_CRASHLOG_ENV "LDHOOK_CRASHLOG"
#define LD_CRASH_MAX_LOG    16
#define LD_CRASH_MAX_STK    64

extern volatile LONG g_crash_on;      /* 1 = диагностика активна */
extern volatile LONG g_depth;         /* вложенность портированных функций */
extern volatile unsigned char g_stage; /* innermost stage, таблица ниже */
extern unsigned char g_stage_stack[LD_CRASH_MAX_STK];

/* Таблица stage (значение на входе функции, не на итерации):
 *   1 s6_unit_ai_tick          2 s6_target_search
 *   3 s6_ai_update             4 s6_path_search (+ s6_path_tree_search)
 *   5 s6_grid_recompute        6 s6_grid_anchor_lookup
 *   7 trace H1/H3              8 s6_band_*
 *   9 native::* wrapper        0 вне портированной функции */

void ld_crash_init(void);             /* initldhook: env + VEH (иначе no-op) */

static __inline void ld_crash_enter(unsigned char s)
{
    if (g_crash_on) {
        LONG d = g_depth;
        if (d >= 0 && d < LD_CRASH_MAX_STK) {
            g_stage_stack[d] = s;
        }
        g_stage = s;
        g_depth = d + 1;
    }
}

static __inline void ld_crash_leave(void)
{
    if (g_crash_on) {
        LONG d = g_depth;
        if (d > 0) {
            d--;
            g_depth = d;
            g_stage = (d > 0 && d <= LD_CRASH_MAX_STK)
                      ? g_stage_stack[d - 1] : 0;
        }
    }
}

/* Выход из функции с __try (C2712: объект с деструктором там запрещён). */
#define LD_CRASH_RET(x) do { ld_crash_leave(); return (x); } while (0)
#define LD_CRASH_RET_VOID() do { ld_crash_leave(); return; } while (0)

#ifdef __cplusplus
/* RAII-вариант для функций без __try: enter в конструкторе, leave в деструкторе. */
struct LdCrashScope {
    explicit LdCrashScope(unsigned char s) { ld_crash_enter(s); }
    ~LdCrashScope() { ld_crash_leave(); }
};
#define LD_CRASH_SCOPE(s) LdCrashScope ld_crash_scope_((unsigned char)(s))
#endif

/* ---------------- Общие утилиты установки (определяются в ldhook.cpp) -------- */
/* E9 rel32 от site до трамплина должен влезать в знаковый диапазон. */
int rel32_fits(void *site, void *dest);

/* ---------------- site1/2/3 (sites_hd.cpp) ---------------- */
/* Сигнатуры сайтов — только для установки хуков (ldhook.cpp). */
#define SITE1_SIG      "\xc6\x41\x58\x01\x59"      /* mov byte[ecx+0x58],1; pop ecx */
#define SITE1_SIG_LEN  5
#define SITE2_SIG       "\x89\x81\x94\x02\x00\x00"  /* mov [ecx+0x294],eax */
#define SITE2_SIG_LEN   6
#define SITE2_PATCH     6                           /* E9 rel32 + nop */
#define SITE3_SIG       "\xe8\xeb\x1b\xff\xff"      /* call FUN_0046f970 */
#define SITE3_SIG_LEN   5

extern volatile LONG g_site1_calls;
extern volatile LONG g_site2_calls;
extern volatile LONG g_site3_calls;
extern void *g_site2_norm;      /* FUN_0041cc90 (нормировка шрифта) */
extern void *g_site3_setter;    /* FUN_0046f970 (сеттер матрицы) */
extern void *g_site3_cont;      /* site+5: продолжение RenderWorld */

void site1_hook(void);          /* __declspec(naked) — определение в sites_hd.cpp */
void site2_hook(void);
void site3_hook(void);

/* ---------------- site5 (диагностика e0a0) ---------------- */
extern volatile LONG g_site5_calls;
extern volatile LONG g_site5_heavy;
extern volatile LONG g_site5_max;
extern volatile LONG g_site5_iter;
extern void *g_site5_cont;
extern void *g_site5_top;
extern void *g_site5_nat;

#define S5_ENTRY_SIG   "\x8b\x44\x24\x10\x83\xec\x34"   /* mov eax,[esp+0x10]; sub esp,0x34 */
#define S5_ENTRY_LEN   7
#define S5_TAIL_SIG    "\x8b\xf0\x3b\xf5\x75\xae"       /* mov esi,eax; cmp esi,ebp; jne -0x52 */
#define S5_TAIL_LEN    6
#define S5_EPI_SIG     "\x83\xc4\x34\xc2\x10\x00"
#define S5_EPI_LEN     6

void s5_entry_hook(void);       /* __declspec(naked) — battle/target.cpp */
void s5_tail_hook(void);
void s5_epi_hook(void);

/* ---------------- site6 (полный detour e0a0) ---------------- */
#define S6_ENTRY_SIG   "\x8b\x44\x24\x10\x83\xec\x34"   /* mov eax,[esp+0x10]; sub esp,0x34 */
#define S6_ENTRY_LEN   7
#define S6_TAIL_SIG    "\x8b\xf0\x3b\xf5\x75\xae"       /* mov esi,eax; cmp esi,ebp; jne -0x52 */
#define S6_TAIL_LEN    6
#define S6_ENV         "LDHOOK_SITE6"
/* LDHOOK_SITE6_EEE0 — отдельный гейт detour'а 45eee0 (tick-хук). Дефолт =
 * мастер LDHOOK_SITE6: без новой переменной поведение бай-в-байт прежнее
 * (SITE6=1 ставит оба detour'а); LDHOOK_SITE6=1 + SITE6_EEE0=0 даёт порт
 * e0a0 без detour'а 45eee0. s6_port_enabled() остаётся единственным
 * источником истины решения «порт e0a0» (диспетчер e0a0, site5/site6,
 * trace-наблюдатели H5/H6). */
#define S6_EEE0_ENV    "LDHOOK_SITE6_EEE0"

/* Гейт порта site6 — единственный источник истины для S6_ENV (target.cpp). */
int s6_port_enabled(void);
/* Гейт detour'а 45eee0 (единственный источник истины для S6_EEE0_ENV,
 * target.cpp); дефолт — s6_port_enabled(). */
int s6_eee0_enabled(void);

/* ---------------- 4.2: detour 45eee0 (тик юнит-AI) ---------------- */
#define S7_ENTRY_SIG   "\x83\xec\x58\x53\x55"   /* sub $0x58,%esp; push ebx; push ebp */
#define S7_ENTRY_LEN   5
extern volatile LONG g_s7_tick_on;      /* 1 = порт s6_unit_ai_tick, 0 = трамплин */
extern void *g_s7_tick_cont;            /* продолжение 0x45eee5 (push esi) */
void s7_tick_hook(void);                /* __declspec(naked) — battle/move.cpp */

/* ---------------- 5.1b: detour 44bdd0 (тик боевого AI) ---------------- */
/* H1 0x44bdf5 (10 Б) — вход порта; выход — нативный эпилог 0x44d074 (H2). */
#define B0_ENV         "LDHOOK_SITE5"
#define B0_EPILOGUE_VA 0x44d074u        /* нативный эпилог (H2/capture) */
extern volatile LONG g_b0_port_on;      /* 1 = H1 уходит в b0_battle_ai_dispatch */
extern void *g_b0_epi;                  /* 0x44d074 — нативный эпилог */

extern volatile LONG g_site6_calls;
extern volatile LONG g_site6_heavy;
extern volatile LONG g_site6_max;
/* Faithful-цикл (п. 3.4): итерации, коды 1/3/4, ротации, µs (ldhook.stats). */
extern volatile LONG g_site6_iter_sum;
extern volatile LONG g_site6_code1;
extern volatile LONG g_site6_code3;
extern volatile LONG g_site6_code4;
extern volatile LONG g_site6_rot;
extern volatile LONGLONG g_s6_us_sum;
extern volatile LONGLONG g_s6_us_max;

void s6_entry_hook(void);       /* __declspec(naked) — battle/target.cpp */

/* ---------------- trace-наблюдатели тика (battle/trace.cpp) ---------------- */
/* H1 0x44bdf5 (10 Б): mov ebp,ecx; je; flds [ebp+0x114]; cont 0x44bdff.
 * H2 0x44d074 (7 Б): mov ecx,[esp+0x14c]; cont 0x44d07b.
 * H3 0x44cbc5 (16 Б): movl $0,0xc0(%esp) + call 0x45eee0; cont 0x44cbd5.
 * H7 0x45dd70 (8 Б): sub esp,0x60; push ebx; mov ebx,[esp+0x68]; cont 0x45dd78.
 * H2 в .text встречается дважды (0x420d90 и 0x44d074) — сигнатура расширена
 * хвостом эпилога (pop edi/esi/ebp) до уникальной. */
#define H1_SIG      "\x8b\xe9\x74\x5f\xd9\x85\x14\x01\x00\x00"
#define H1_SIG_LEN  10
#define H2_SIG      "\x8b\x8c\x24\x4c\x01\x00\x00\x5f\x5e\x5d"
#define H2_SIG_LEN  10
#define H3_SIG      "\xc7\x84\x24\xc0\x00\x00\x00\x00\x00\x00\x00" \
                    "\xe8\x0b\x23\x01\x00"
#define H3_SIG_LEN  16
#define H7_SIG      "\x83\xec\x60\x53\x8b\x5c\x24\x68"
#define H7_SIG_LEN  8

extern void *g_h1_cont;         /* 0x44bdff */
extern void *g_h1_je;           /* 0x44be58 — цель нативного je */
extern void *g_h2_cont;         /* 0x44d07b */
extern void *g_h3_cont;         /* 0x44cbd5 */
extern void *g_h3_callee;       /* 0x45eee0 (FUN_0045eee0, native::fn) */
extern void *g_h7_cont;         /* 0x45dd78 */

/* R1..R3: пиннинг нативного seed'а (7 Б: call [IAT] + push eax) под LDHOOK_SEED. */
#define R1_SIG      "\xff\x15\x7c\xc2\x4e\x00\x50"   /* call [0x4ec27c]; push eax */
#define R2_SIG      "\xff\x15\xb0\xc0\x4e\x00\x50"   /* call [0x4ec2b0]; push eax (2 шт.) */
#define R_SIG_LEN   7

/* ---------------- capture-наблюдатели листьев (LDHOOK_BATTLE_CAPTURE) ----- */
/* Хук на ВХОДЕ функции-листа: E9 rel32 (5 Б) вместо первых байт пролога;
 * пролог воспроизводится в трамплине, затем jmp на cont = entry+stolen.
 * Адреса — по VA (не find_in_text: прологи не уникальны в .text), байты
 * проверяются memcmp по сигнатуре (game_dis.txt 0x45c5a0/0x45c690/0x45cb40). */
#define CAP_ENTRY_LEN     5
#define CAP_VA_CB40       0x45cb40u     /* RET 8;  пролог 9 Б */
#define CAP_VA_HEUR       0x45c690u     /* RET 4;  пролог 6 Б */
#define CAP_VA_TIMEOUT    0x45c5a0u     /* RET 4;  пролог 10 Б (call timeGetTime) */
#define CAP_STOLEN_CB40     9
#define CAP_STOLEN_HEUR     6
#define CAP_STOLEN_TIMEOUT 10
#define CAP_PROL_CB40     "\x83\xec\x3c\x53\x55\x56\x57\x8b\xf1"
#define CAP_PROL_HEUR     "\x55\x8b\xe9\x8b\x4d\x00"
#define CAP_PROL_TIMEOUT  "\x51\x56\x8b\xf1\xff\x15\x7c\xc2\x4e\x00"

void cap_fn_cb40_hook(void);     /* __declspec(naked) — battle/capture.cpp */
void cap_fn_heur_hook(void);
void cap_fn_timeout_hook(void);

#endif /* LDHOOK_INTERNAL_H */
