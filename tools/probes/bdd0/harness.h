/* tools/probes/bdd0/harness.h — офлайн-харнесс боевого тика 44bdd0: общие типы.
 *
 * Ядро (tools/probes/bdd0_harness.cpp) знает ровно три вещи:
 *   1. имена и длины областей состояния (frame/unit/ai/band/gs/ctx/out/node),
 *   2. имена полей (декларативная таблица fields в registry.cpp),
 *   3. формат журнала вызовов (CallLogEnt).
 * Доменных правил в ядре нет: проверки живут в checks_builtin.cpp и видят только
 * read-only TickContext; реализация — за ImplDef; нативный слой — native_*.cpp.
 *
 * Данные захвата v12 (bdd0_in / bdd0_leaf / bdd0_native / bdd0_res) описаны в
 * tools/ldhook/battle/capture.h; загрузчик — scenario.cpp.
 */
#ifndef BDD0_HARNESS_H
#define BDD0_HARNESS_H

#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>

/* --------------------------- данные захвата v12 --------------------------- */
#define BDD0_FRAME_LEN  0x168     /* локалы 0x00..0x15b + param_1/2/3 */
#define BDD0_UNIT_LEN   0x180     /* все чтения unit в tick.cpp до +0x17f */
#define BDD0_AI_LEN     0x80      /* встроенный BattleAI, unit+0x138..+0x1b7 */
#define BDD0_AI_OFF     0x138
#define BDD0_BAND_ELEM  0x18      /* элемент ленты (45af50: (end-begin)/0x18) */
#define BDD0_BAND_MAX   64        /* потолок материализуемых элементов ленты */
#define BDD0_GS_LEN     0x20      /* объект FUN_00442570, читаемый телом */

/* Один вызов логического листа из потока bdd0_leaf.
 * w/nw — dword'ы, которые лист записал ЧЕРЕЗ указатель (45c6f0 out-тройка,
 * 45e1d0 возврат-тройка); в захвате это необязательный ключ `w`.
 * trap — захват записал запись после сбоя/рассинхрона LIFO (`"trap":1`):
 * значения (ret/args/w) в ней недостоверны, replay её НЕ потребляет. */
struct LeafRec {
    int order, va, ecx, nargs, args[6], ret, w[3], nw, trap;
    int site;                  /* v12: call-site VA (0 = older record) */
};

/* v12: не-листовой нативный вызов тела (`bdd0_native`).  slot != 0 — косвенный
 * вызов через vtable unit'а; тогда va — фактическая цель, slot — id слота.
 * kind: 0 int / 1 al / 2 f32 / 3 void / 4 ptr / 5 ptr+w. */
struct NativeRec {
    int order, site, va, slot, kind, ecx, nargs, args[8], ret, w[3], nw, trap;
    int ww[32], nww;             /* [unit_off, value] pairs (45eee0 state) */
    NativeRec()
        : order(0), site(0), va(0), slot(0), kind(0), ecx(0), nargs(0), ret(0),
          nw(0), trap(0), nww(0)
    {
        for (int i = 0; i < 8; i++) args[i] = 0;
        for (int i = 0; i < 3; i++) w[i] = 0;
        for (int i = 0; i < 32; i++) ww[i] = 0;
    }
};

/* v12: запрос порта на не-листовой натив (для native_args).  site/order
 * заполняются после сопоставления с записью bdd0_native.  last_label — id метки
 * тела, последней посещённой перед этим вызовом (-1 в не-trace сборке). */
struct NativeCallLogEnt {
    int order, site, va, slot, ecx, nargs, args[8];
    int call_seq;              /* глобальный номер запроса (leaf+native) */
    int last_label;
    NativeCallLogEnt()
        : order(0), site(0), va(0), slot(0), ecx(0), nargs(0), call_seq(0),
          last_label(-1)
    {
        for (int i = 0; i < 8; i++) args[i] = 0;
    }
};

struct TickRecord {
    int seq, tick, unit_index;
    int p1, p2, p3;            /* bdd0_in p1/p2/p3 (= frame+0x15c/0x160/0x164) */
    int u170_in;
    unsigned gate114;
    int has_frame, has_unit, has_ai, has_band, has_gs;
    int band_n;
    int has_chain;             /* e0/e0_44/tree присутствуют в записи */
    unsigned e0, e0_44, tree;
    int has_gctx0;
    unsigned gctx0;            /* *(int*)ai_ctx — первый dword game_ctx */
    unsigned self_raw;         /* bdd0_in self = адрес unit в игре */
    unsigned band_raw;         /* bdd0_in band_begin = адрес ленты */
    unsigned char frame[BDD0_FRAME_LEN];
    unsigned char unit[BDD0_UNIT_LEN];
    unsigned char ai[BDD0_AI_LEN];
    unsigned char band[BDD0_BAND_MAX * BDD0_BAND_ELEM];
    unsigned char gs[BDD0_GS_LEN];
    int trap;                  /* bdd0_in `"trap"`: снимок снят при сбое */
    /* записанный исход пары bdd0_res (только --impl native / отчёт) */
    int has_res;
    int res_trap;              /* bdd0_res `"trap"`: исход недостоверен */
    int res_al, res_out, res_u170;
    unsigned res_m[3];
    /* окно листьев этого seq (индексы в Scenario::leaves) */
    int leaf_first, leaf_n;
    /* окно bdd0_native этого seq (индексы в Scenario::natives) */
    int native_first, native_n;
    int native_eee0_no_ww;     /* 45eee0 record without post-state writes */
    int incomplete_tail;       /* seq is the last in the capture and has no res */
    int res_missing_nontail;   /* no res below the last seq: capture anomaly */
    TickRecord()
        : seq(0), tick(0), unit_index(0), p1(0), p2(0), p3(0), u170_in(0),
          gate114(0), has_frame(0), has_unit(0), has_ai(0), has_band(0),
          has_gs(0), band_n(0), has_chain(0), e0(0), e0_44(0), tree(0),
          has_gctx0(0), gctx0(0), self_raw(0), band_raw(0), trap(0),
          has_res(0), res_trap(0), res_al(0), res_out(0), res_u170(0),
          leaf_first(0), leaf_n(0), native_first(0), native_n(0),
           native_eee0_no_ww(0), incomplete_tail(0), res_missing_nontail(0)
    {
        for (int i = 0; i < BDD0_FRAME_LEN; i++) frame[i] = 0;
        for (int i = 0; i < BDD0_UNIT_LEN; i++) unit[i] = 0;
        for (int i = 0; i < BDD0_AI_LEN; i++) ai[i] = 0;
        for (int i = 0; i < (int)sizeof(band); i++) band[i] = 0;
        for (int i = 0; i < BDD0_GS_LEN; i++) gs[i] = 0;
        res_m[0] = res_m[1] = res_m[2] = 0;
    }
};

struct Scenario {
    std::string name;
    std::vector<TickRecord> ticks;
    std::vector<LeafRec> leaves;
    std::vector<NativeRec> natives;
    int snap_records;          /* сколько bdd0_in прочитано */
    int leaf_records;
    int native_records;
    int truncated;             /* файл оборван (захват убит): хвост отброшен */
    int records;               /* сколько объектов прочитано вообще */
    int leaf_missing_n;        /* маркеры leaf_missing: сайт не захукался */
    std::string leaf_missing_why;
    int native_missing_n;      /* native_site_skipped: RECORD-сайт не захукался */
    std::string native_missing_why;
    int input_trap;            /* bdd0_in с trap=1 (снимок недостоверен) */
    int outcome_trap;          /* bdd0_res с trap=1 (исход недостоверен) */
    int incomplete_tail;       /* тиков, у которых seq без res и он последний */
    int res_missing_nontail;   /* res отсутствует ниже последнего seq: аномалия */
    Scenario() : snap_records(0), leaf_records(0), native_records(0),
                 truncated(0), records(0), leaf_missing_n(0),
                 native_missing_n(0), input_trap(0), outcome_trap(0),
                 incomplete_tail(0), res_missing_nontail(0) {}
};

/* Загрузчик (scenario.cpp). Ошибка формата → std::runtime_error (rc 2). */
Scenario *load_capture(const char *path);
void scenario_free(Scenario *s);

/* ---------------------------- области состояния --------------------------- */
struct RegionView {
    const char *name;
    const unsigned char *base;
    unsigned len;
};

struct CallLogEnt {
    int order;                 /* 0-based внутри тика */
    int va;
    int ecx;
    int nargs;
    int args[6];
    int ret;
    int call_seq;              /* глобальный номер запроса (leaf+native) */
    int last_label;            /* id метки тела перед вызовом (-1 не-trace) */
};

/* --------------------------- контракт проверок ---------------------------- */
enum {
    NEED_DECISIONS  = 1u << 0, /* al/out/u170/m0..m2 тика */
    NEED_POST_STATE = 1u << 1, /* снапшоты v12: frame/unit/ai/band */
    NEED_CALLLOG    = 1u << 2, /* поток листьев bdd0_leaf */
    NEED_TIMING     = 1u << 3, /* ядро: wall-clock */
    NEED_HANDLES    = 1u << 4, /* карта указателей снимка */
    NEED_GAMESTATE  = 1u << 5, /* gs_b64 (объект 442570) */
    NEED_BODY       = 1u << 6  /* нужен реально исполненный тик */
};

struct TickContext {
    int seq, tick, unit_index;
    const char *scenario, *impl;
    int al, out_slot, u170;
    int out_known;             /* p3 != NULL: исход наблюдаем */
    long long wall_ms, leaf_calls;
    int ran_body;              /* реализация исполняла порт (не replay исхода) */
    int hang;                  /* watchdog сработал раньше реализации */
    int trapped;
    const char *trap_why;
    unsigned long trap_addr;
    int trap_unbound;            /* trap at an unbound materialisation pointer */
    const RegionView *regions;       /* пост-состояние тика */
    int region_n;
    const RegionView *regions_before;/* входной снимок (тот же порядок имён) */
    int region_before_n;
    const CallLogEnt *log;
    int log_n;
    const TickRecord *rec;
    unsigned avail;
    /* механические факты ядра, которые читают проверки */
    int write_outside;                /* запись вне объявленных областей */
    const char *write_outside_why;
    int handle_miss;                  /* unmapped handle снимка */
    const char *handle_miss_why;
    int leaf_violation;               /* расхождение потока листьев */
    const char *leaf_violation_why;
    int native_violation;             /* расхождение потока bdd0_native */
    const char *native_violation_why;
    int native_calls;                 /* запросов не-листовых нативов в тике */
    char *why_buf;
    unsigned why_cap;

    unsigned field(const char *name) const;
    const unsigned char *region(const char *name) const;
    const unsigned char *region_before(const char *name) const;
};

/* --path-diff diagnostic (not a check): recorded vs consumed call order. */
void bdd0_path_diff(const Scenario *sc, const TickRecord *t,
                    const TickContext &tc, FILE *json);

/* CheckResult: why — стабильная строка проверки (ядро её только печатает).
 * skip=1 — проверке нечем мерить (ядро считает SKIP, а не pass). */
struct CheckResult {
    int ok;
    const char *why;
    int skip;
};

typedef CheckResult (*CheckFn)(const TickContext &);

struct CheckDef {
    const char *name;
    CheckFn fn;
    unsigned needs;
};

/* Реестр проверок (registry.cpp): единственный способ добавить проверку.
 * Пример добавления (ядро НЕ трогается — одна строка):
 *   register_check({ "target_in_grid", target_in_grid, NEED_DECISIONS }); */
void register_check(const CheckDef &def);
int check_count(void);
const CheckDef *check_at(int i);
const char *need_name(unsigned bit);
const char *whyf(const TickContext &tc, const char *fmt, ...);
int field_count(void);
const char *field_name(int i);

/* Встроенные проверки (checks_builtin.cpp): no_trap, bounds, budget,
 * contract_min + механические leaf_seq и handles. */
void register_builtin_checks(void);
/* Политика бюджета (CLI --budget-ms/--budget-calls); домены — в проверке. */
void bdd0_budget_set(int ms, int calls);
int bdd0_budget_ms(void);
int bdd0_budget_calls(void);

/* ---------------------------- реестр реализаций --------------------------- */
struct Arena;                 /* непрозрачная арена (arena.cpp) */

typedef int (*TickFn)(void *unit, void *frame);

struct ImplDef {
    const char *name;
    void (*prepare)(Arena *a);   /* связать синтетику с областями (опц.) */
    TickFn tick;                 /* NULL — тело не исполняется (replay исхода) */
    void (*release)(Arena *a);   /* (опц.) */
    int runs_body;               /* 1 — тик реально исполняет порт */
};

void register_impl(const ImplDef &def);
const ImplDef *find_impl(const char *name);
int impl_count(void);
const ImplDef *impl_at(int i);
void register_builtin_impls(void);

/* ------------------------------ арена тика -------------------------------- */
/* Сборка арены из записи: материализация областей + карта указателей снимка
 * (см. таблицу k_fixups в arena.cpp). Возврат копии pre-state для bounds. */
Arena *arena_build(const TickRecord *rec);
void arena_release(Arena *a);
void arena_set_current(Arena *a);
Arena *arena_current(void);

const RegionView *arena_regions(const Arena *a);
int arena_region_n(const Arena *a);
const RegionView *arena_views(const Arena *a);
const RegionView *arena_pre_views(const Arena *a);
unsigned char *arena_region_w(Arena *a, const char *name);
const unsigned char *arena_region_r(const Arena *a, const char *name);
const unsigned char *arena_region_pre(const Arena *a, const char *name);
unsigned char *arena_out_slot(Arena *a);
int arena_out_known(const Arena *a);
void arena_set_out_slot(Arena *a, int value);

/* Мэппинг адреса сбоя в `region+0xNN` (или NULL). */
const char *arena_why_addr(const Arena *a, unsigned long addr, char *buf, int cap);
/* Изменённые после тика байты вне объявленных областей / канарейки.
 * Возврат 1 при нарушении, why — стабильное описание. */
int arena_scan_writes(Arena *a, char *why, int cap);
/* Пропущенные handle'ы снимка (указатель не был материализован). */
int arena_handle_misses(const Arena *a);
const char *arena_handle_miss_why(const Arena *a);
/* 1 — адрес ловушки пришёл от неcвязанного указателя (см. handles). */
int arena_fault_unbound(Arena *a, unsigned long addr);
/* Перевод указателя из пространства игры в shadow-адрес (нативный слой).
 * arena_resolve_ptr_at знает сайт вызова (VA call site + VA натива) и привязывает
 * по идентичности пары из k_identity_rets (arena.cpp); arena_resolve_ptr — без
 * сайта, привязка по идентичности неприменима. */
void *arena_resolve_ptr(unsigned raw);
void *arena_resolve_ptr_at(unsigned raw, unsigned site, unsigned va);
/* Сколько leaf rets привязано по идентичности (sentinels из k_identity_rets). */
int arena_leaf_identity(const Arena *a);
/* Сколько opaque-узлов выдано листам вместо материализации (диагностика:
 * непривязанный адрес даёт сентинел, а не живой объект). */
int arena_leaf_nodes(const Arena *a);
/* Проверка «адрес лежит внутри объявленной области (len байт от p)». */
int arena_addr_ok(const void *p, unsigned len);
/* Запись поля по имени: оффсеты живут только в таблице полей (registry.cpp),
 * ядро адресов не считает. */
int field_write(Arena *a, const char *name, unsigned value);

/* Нативный слой (native_replay.cpp) для replay-режима:
 * поток листьев текущего seq + журнал вызовов. */
void bdd0_leaf_begin(int seq, const LeafRec *v, int n);
int bdd0_leaf_calls(void);
int bdd0_leaf_violations(void);
const char *bdd0_leaf_violation_why(void);
const CallLogEnt *bdd0_calllog(int *n);
void bdd0_calllog_reset(void);
const LeafRec *bdd0_leaf_record_at(int i);

/* v12: поток bdd0_native текущего seq (replay не-листовых RECORD-нативов). */
void bdd0_native_begin(int seq, const NativeRec *v, int n);
int bdd0_native_calls(void);
int bdd0_native_violations(void);
const char *bdd0_native_violation_why(void);
int bdd0_native_consumed(void);
const NativeRec *bdd0_native_pending(void);
const NativeCallLogEnt *bdd0_native_calllog(int *n);
const NativeRec *bdd0_native_record_at(int i);

/* Счётчики «не смоделировано» (native_stubs.cpp): печатаются в сводке прогона,
 * чтобы no-op'ы не выглядели проверенными. */
int bdd0_unmodelled_calls(void);
int bdd0_unmodelled_unit_ai_tick(void);

/* v12: uninitialised native arguments skipped by leaf_args/native_args. */
void bdd0_uninit_reset(void);
int bdd0_uninit_skipped(void);

/* v12: аргументы, которые НЕ сравнимы (by-value область кадра вызывающего),
 * skips по нативным сайтам 0x44bfa7/0x44c109/0x44c9d5/0x44cee1. */
void bdd0_noncomp_reset(void);
int bdd0_noncomp_skipped(void);

#ifdef B0_TRACE_LABELS
/* Label trace (tools/ldhook/battle/tick.cpp): существует только в trace-сборке
 * харнесса (-DB0_TRACE_LABELS=1).  События кольца тика: id метки
 * (< B0_EXIT_BASE), id выхода (B0_EXIT_BASE..B0_COND_NAME_BASE), событие
 * условия (бит 31 — событие условия, бит 30 — ветка взята, младшие биты —
 * номер гейта).  Имена меток/условий — b0_label_names[]/b0_cond_names[]. */
#define B0_EXIT_BASE 128
#define B0_COND_NAME_BASE 256
#define B0_TRACE_RING 2048
extern volatile int b0_trace_on;
extern volatile unsigned b0_last_label;
extern volatile int b0_tick_len;
extern volatile int b0_ring_i;
extern const unsigned *b0_trace_ring(void);
extern const char *b0_label_names[];
extern const char *b0_cond_names[];
#endif

#endif /* BDD0_HARNESS_H */
