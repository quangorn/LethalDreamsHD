/* tools/probes/bdd0/checks_builtin.cpp — встроенный набор проверок.
 *
 * Критерий приёмки зафиксирован один: нет падения, нет зависания, нет записи
 * вне объявленных областей состояния. Поведенческие сверки добавляются
 * отдельно; единственная такая в этом файле — leaf_args (аргументы запроса
 * листа против записи нативного прогона), остальные проверки механики.
 *
 * Каждая проверка видит только read-only TickContext. Если данных нет, ядро
 * ставит SKIP по битам needs; если проверке нечем мерить внутри тика, она
 * возвращает skip=1 с причиной. Молчаливого pass нет.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"

#define OK()        do { CheckResult r_ = { 1, "", 0 }; return r_; } while (0)
#define FAIL(s)     do { CheckResult r_ = { 0, (s), 0 }; return r_; } while (0)
#define SKIP(s)     do { CheckResult r_ = { 1, (s), 1 }; return r_; } while (0)

static int bits_finite(unsigned bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return (_finite(f) != 0) ? 1 : 0;
}

/* no_trap — реализация не сфальтила: ловушки ловит VEH ядра (первый шанс,
 * до SEH-рамки тела), адрес переводится в `region+0xNN` по карте областей. */
static CheckResult chk_no_trap(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body (recorded outcome only)");
    }
    if (tc.hang) {
        /* тик не доехал: состояние неизвестно, «нет ловушки» не доказано */
        SKIP("hang: tick did not finish, state unknown");
    }
    if (tc.trapped) {
        if (tc.trap_unbound) {
            SKIP("trap at unbound materialisation pointer (attributed to handles)");
        }
        return CheckResult{ 0, tc.trap_why, 0 };
    }
    OK();
}

/* bounds — ни одной записи вне объявленных областей: канарейки+guard-страницы
 * (ловушка на охраняемой странице) и пост-скан изменённых байт. */
static CheckResult chk_bounds(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body (recorded outcome only)");
    }
    if (tc.hang) {
        SKIP("hang: tick did not finish, post-state not scanned");
    }
    if (tc.write_outside) {
        return CheckResult{ 0, tc.write_outside_why, 0 };
    }
    if (tc.trapped) {
        /* trap уже отдан no_trap; bounds судит только по адресу записи */
        SKIP("trap reported by no_trap");
    }
    OK();
}

/* budget — тик уложился в wall-clock и бюджет вызовов. Значения бюджета —
 * политика ядра (CLI --budget-ms/--budget-calls); факт превышения — здесь. */
#define BDD0_BUDGET_MS_DEFAULT    100
#define BDD0_BUDGET_CALLS_DEFAULT 5000

static int g_budget_ms = BDD0_BUDGET_MS_DEFAULT;
static int g_budget_calls = BDD0_BUDGET_CALLS_DEFAULT;

void bdd0_budget_set(int ms, int calls)
{
    if (ms > 0) g_budget_ms = ms;
    if (calls > 0) g_budget_calls = calls;
}

int bdd0_budget_ms(void) { return g_budget_ms; }
int bdd0_budget_calls(void) { return g_budget_calls; }

static CheckResult chk_budget(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body (recorded outcome only)");
    }
    if (tc.hang) {
        return CheckResult{ 0, whyf(tc, "hang: tick %d exceeded wall budget, "
                                        "leaf_calls=%lld", tc.tick,
                                    tc.leaf_calls), 0 };
    }
    if (tc.wall_ms > (long long)g_budget_ms) {
        return CheckResult{ 0, whyf(tc, "wall %lld ms > %d ms",
                                    tc.wall_ms, g_budget_ms), 0 };
    }
    if (tc.leaf_calls > (long long)g_budget_calls) {
        return CheckResult{ 0, whyf(tc, "leaf_calls %lld > %d",
                                    tc.leaf_calls, g_budget_calls), 0 };
    }
    OK();
}

/* contract_min — значения в документированных доменах. Домены (а не ядро):
 * out ∈ {-1,0,1} (−1 = param_3 не наблюдался и не писался), al ∈ {0,1},
 * u170 ∈ {0,1,2,3,4}, pos-поля цели конечны (не NaN/inf). */
static CheckResult chk_contract_min(const TickContext &tc)
{
    if (tc.hang) {
        /* тик не доехал: al/out/u170 — промежуточные, а не итоговые */
        SKIP("hang: tick did not finish, decisions incomplete");
    }
    if ((tc.avail & NEED_GAMESTATE) == 0) {
        /* фазы/позиции игрового состояния в захвате нет: решения тика
         * получены на ЗАНУЛЁННОЙ shadow-области — домены проверяемы, а вот
         * «тик вёлся по тому же состоянию» — нет. Не выдаём это за проверку. */
        CheckResult r = { 1, "gs_b64 absent: game_state shadow is zeroed",
                          1 };
        return r;
    }
    unsigned al = tc.field("al");
    unsigned u170 = tc.field("u170");
    unsigned out = tc.field("out");

    if (al > 1u) {
        return CheckResult{ 0, whyf(tc, "al=%u outside {0,1}", al), 0 };
    }
    if (u170 > 4u) {
        return CheckResult{ 0, whyf(tc, "u170=%u outside {0,1,2,3,4}", u170), 0 };
    }
    if (tc.out_known) {
        int o = (int)out;
        if (!(o == -1 || o == 0 || o == 1)) {
            return CheckResult{ 0, whyf(tc, "out=%d outside {-1,0,1}", o), 0 };
        }
    }
    if (!bits_finite(tc.field("target_x")) ||
        !bits_finite(tc.field("target_y"))) {
        return CheckResult{ 0, whyf(tc, "target pos is NaN/inf "
                                        "(unit+0xa8=0x%08x unit+0xac=0x%08x)",
                                    tc.field("target_x"), tc.field("target_y")),
                            0 };
    }
    OK();
}

/* leaf_seq — механическая: поток листьев тика обязан быть израсходован ЦЕЛИКОМ.
 * Проверяются обе стороны:
 *   - запрос порта не соответствует следующей записи (другой va/order) или
 *     запросов больше, чем записей (исчерпание потока) — нарушение потока
 *     ставит replay-слой (tc.leaf_violation), включая trap-записи;
 *   - тик вернулся, а записи остались непотреблёнными: порт сделал МЕНЬШЕ
 *     вызовов, чем натив (другой/более короткий путь) — без этой ветки
 *     «сокращённый» тик проходил бы как pass, потому что сверять было нечего.
 * Ноль записей и ноль вызовов — законный pass (логических вызовов нет),
 * поэтому бит NEED_CALLLOG здесь не запрошен: входы проверки — окно записей
 * тика (tc.rec) и счётчик потребления replay-слоя, они есть всегда. */
const LeafRec *bdd0_leaf_pending(void);        /* native_replay.cpp */
int bdd0_leaf_consumed(void);                  /* native_replay.cpp */

static CheckResult chk_leaf_seq(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no leaf requests");
    }
    if (tc.hang) {
        SKIP("hang: leaf stream incomplete");
    }
    if (tc.leaf_violation) {
        /* поток разошёлся: другой va/order, исчерпание (запросов БОЛЬШЕ, чем
         * записей), trap-запись или неполный захват */
        return CheckResult{ 0, tc.leaf_violation_why, 0 };
    }
    int recorded = (tc.rec != NULL) ? tc.rec->leaf_n : 0;
    int consumed = bdd0_leaf_consumed();
    if (consumed > recorded) {
        return CheckResult{ 0, whyf(tc, "consumed %d leaf records of %d — "
                                        "port made more calls than native",
                                    consumed, recorded), 0 };
    }
    if (consumed < recorded) {
        const LeafRec *u = bdd0_leaf_pending();
        return CheckResult{ 0, whyf(tc, "unconsumed leaf #%d va=0x%x (order %d)"
                                        " - port made fewer calls than native "
                                        "(consumed %d of %d)",
                                    consumed, (u != NULL) ? (unsigned)u->va : 0u,
                                    (u != NULL) ? u->order : -1,
                                    consumed, recorded), 0 };
    }
    OK();
}

/* handles — механическая: каждый указатель, прочитанный телом из снимка,
 * материализован (иначе bounds/contract_min прогоняются на мусоре).
 * Привязанный по идентичности leaf-ret (пара site+va из k_identity_rets в
 * arena.cpp) — привязан: handle_miss не ставится. Разыменование такого
 * сентинела ловится trap'ом при 0xDEAD… и приписывается handles
 * (arena_fault_unbound), поэтому здесь оно даёт FAIL, а не «проверено». */
static CheckResult chk_handles(const TickContext &tc)
{
    if (tc.trap_unbound) {
        return CheckResult{ 0, whyf(tc, "unbound pointer dereference at "
                                        "0x%08x (trap attributed to handles)",
                                    tc.trap_addr), 0 };
    }
    if (tc.handle_miss) {
        return CheckResult{ 0, tc.handle_miss_why, 0 };
    }
    if (tc.trapped) {
        SKIP("trap not attributable to unbound materialisation (no_trap)");
    }
    if (tc.hang) {
        SKIP("hang: pointer use not completed");
    }
    OK();
}

/* ------------------------- uninitialised native args --------------------- *
 * Native stack slots that the called code does not initialise.  Comparing the
 * port's deliberate zeros with native stack junk is meaningless.  Minimal list,
 * evidence from the listing:
 *   0x44c71b  `LEA EAX,[ESP+0x28]; PUSH EAX; CALL *0x38(EDX)` then
 *             0x44c72b stores the returned world triple; the callee is not
 *             required to write all three dwords, and 0x44c730 passes
 *             arg[0] from that triple to 44bbf0.
 */
struct UninitArg {
    int site, va, arg;
    const char *why;
};

/* Same shape as UninitArg: an argument that must NOT be compared. */
struct NoncompArg {
    int site, va, arg;
    const char *why;
};

static const UninitArg k_uninit_args[] = {
    { 0x44c730, 0x44bbf0, 0, "arg[0] uninitialised in native (0x44c71b)" },
    { 0x44c730, 0x44bbf0, 1, "arg[1] uninitialised in native (0x44c71b)" },
    { 0x44c730, 0x44bbf0, 2, "arg[2] uninitialised in native (0x44c71b)" },
    /* 0x44be15..0x44be55 (`SUB ESP,0x20` + 32-byte snapshot copy at [ESP],
     * `CALL *[EDX+0x28]`): the copy writes dwords at buffer offsets 0x00..0x17
     * and a dword at 0x1c, single bytes at 0x18/0x19.  Bytes 0x1a, 0x1b — the
     * high half of arg[6] — are never written, so arg[6] carries stack junk
     * there (native 0x0ffa0100 vs the port's 0x00000100). */
    { 0x44be55, 0x4491b0, 6, "arg[6] uninitialised in native (0x44be15..0x44be55: "
                              "bytes 0x1a/0x1b of the snapshot buffer are never "
                              "written)" }
};

static int g_uninit_skipped = 0;

void bdd0_uninit_reset(void) { g_uninit_skipped = 0; }
int bdd0_uninit_skipped(void) { return g_uninit_skipped; }

/* ------------------------- noncomparable native args -------------------- *
 * Sites 0x44bfa7 / 0x44c109 / 0x44c9d5 / 0x44cee1 call `ai_time_update`
 * (0x446a80, `ret $0x3c`).  The recorder ABI arg[i] = [esp+8+4i] indexes the
 * 60-byte region opened by `sub $0x3c,%esp`: the caller fills it ONLY by the
 * self-copy of 0x4040f0 from R+4 (caller-frame stack residue), the callee runs
 * a destructor loop over R, and arg[10] is overwritten by the callee itself
 * (`fstps 0x50(%esp)` @0x446ac3).  Nothing in arg[0..14] at these sites is
 * stable between implementations ⇒ comparing it is noise.  The last pushed
 * dword sits at [esp+4], is never indexed by the formula, and the callee
 * zeroes it before reading — it is not a comparison candidate either.
 * Entries are explicit per (site, index); sites 0x44c109/0x44c9d5/0x44cee1
 * are declared from the same shape (`sub $0x3c` + 0x4040f0 + call 0x446a80)
 * and do not occur in the current capture.  The recorded args[] carries arg[0]
 * only; indices 1..14 declare the rest of the region.
 * No leaf site has this shape (leaf targets 45c6f0..44bbf0 receive computed
 * values, not a by-value region), so k_noncomparable_args has no leaf entries
 * and leaf_args never matches it.
 */
#define BDD0_NONCOMP_REASON "byvalue-60B-caller-frame"

static const NoncompArg k_noncomparable_args[] = {
    /* site 0x44bfa7 */
    { 0x44bfa7, 0x446a80, 0,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 1,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 2,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 3,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 4,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 5,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 6,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 7,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 8,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 9,  BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 10, BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 11, BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 12, BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 13, BDD0_NONCOMP_REASON },
    { 0x44bfa7, 0x446a80, 14, BDD0_NONCOMP_REASON },
    /* site 0x44c109 */
    { 0x44c109, 0x446a80, 0,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 1,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 2,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 3,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 4,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 5,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 6,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 7,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 8,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 9,  BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 10, BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 11, BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 12, BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 13, BDD0_NONCOMP_REASON },
    { 0x44c109, 0x446a80, 14, BDD0_NONCOMP_REASON },
    /* site 0x44c9d5 */
    { 0x44c9d5, 0x446a80, 0,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 1,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 2,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 3,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 4,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 5,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 6,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 7,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 8,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 9,  BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 10, BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 11, BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 12, BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 13, BDD0_NONCOMP_REASON },
    { 0x44c9d5, 0x446a80, 14, BDD0_NONCOMP_REASON },
    /* site 0x44cee1 (no rep movs source at this site) */
    { 0x44cee1, 0x446a80, 0,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 1,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 2,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 3,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 4,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 5,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 6,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 7,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 8,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 9,  BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 10, BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 11, BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 12, BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 13, BDD0_NONCOMP_REASON },
    { 0x44cee1, 0x446a80, 14, BDD0_NONCOMP_REASON }
};

static int g_noncomp_skipped = 0;

void bdd0_noncomp_reset(void) { g_noncomp_skipped = 0; }
int bdd0_noncomp_skipped(void) { return g_noncomp_skipped; }

/* Exact (site, va, arg) match: NO site wildcard — the region is defined by the
 * call site, so a record without a site is not skipped. */
static const NoncompArg *noncomp_find(int site, int va, int arg)
{
    int i;
    for (i = 0; i < (int)(sizeof(k_noncomparable_args) /
                           sizeof(k_noncomparable_args[0])); i++) {
        const NoncompArg *u = &k_noncomparable_args[i];
        if (u->site == site && u->va == va && u->arg == arg) return u;
    }
    return NULL;
}

/* site may be 0 in older bdd0_leaf records; then match target+arg and let the
 * check report the reduced certainty instead of silently passing. */
static const UninitArg *uninit_find(int site, int va, int arg)
{
    int i;
    for (i = 0; i < (int)(sizeof(k_uninit_args) / sizeof(k_uninit_args[0]));
         i++) {
        const UninitArg *u = &k_uninit_args[i];
        if (u->va != va || u->arg != arg) continue;
        if (site == 0 || u->site == 0 || u->site == site) return u;
    }
    return NULL;
}

/* ------------------------------- leaf_args -------------------------------- */
/* Сверка аргументов, которые ПОРТ вычислил в запросе листа, с аргументами,
 * записанными нативным прогоном (`bdd0_leaf`). Ничего не переисполняется:
 * сторона порта — CallLogEnt (ecx/args пишет request() в native_replay.cpp),
 * сторона натива — сопоставленная запись потока (bdd0_leaf_record_at).
 *
 * Пространства адресов разные (порт видит shadow арены, запись — адреса игры),
 * поэтому указатели сравниваются символьно: (база, смещение), где базы — те же,
 * что в привязках arena.cpp (self→unit, band_begin, e0, e0_44, tree). Адреса
 * кадра вызывающего в захвате нет (bdd0_in его не хранит), поэтому кадровые
 * указатели сравниваются смещениями от базы кадра: первое такое поле тика
 * ЗАДАЁТ сдвиг кадра и за pass не считается, остальные обязаны дать тот же
 * сдвиг. Запись с адресом вне привязок (куча игры, чужой стек) не сравнима ⇒
 * SKIP с причиной, а не pass. w[] сверяется с dword'ами, реально записанными
 * обёрткой (в ветке «w есть в записи» порт их копирует — там сверка не значит
 * ничего; содержательна ветка, где w в записи нет).
 *
 * Возврат листа НЕ сравнивается: в replay он сам берётся из записи — сравнение
 * было бы тавтологией. trap-записи уже отсекает leaf_seq (здесь не дублируем).
 */
const LeafRec *bdd0_leaf_record_at(int i);      /* native_replay.cpp */
int bdd0_leaf_applied_w(int i, int *w3);        /* native_replay.cpp */

/* Типы аргументов по ABI листьев (native.h): 1 — указатель (символьное
 * сравнение), 0 — dword/float-биты (точное равенство битов, без допуска). */
struct LeafAbi {
    int va, nargs;
    int ptr[6];
};

static const LeafAbi k_leaf_abi[] = {
    { 0x45c6f0, 1, { 1, 0, 0, 0, 0, 0 } },   /* ctx_pos_get: out */
    { 0x45c370, 0, { 0, 0, 0, 0, 0, 0 } },   /* band_head */
    { 0x45af50, 0, { 0, 0, 0, 0, 0, 0 } },   /* band_count */
    { 0x45c730, 0, { 0, 0, 0, 0, 0, 0 } },   /* band_cursor */
    { 0x45b150, 3, { 0, 0, 0, 0, 0, 0 } },   /* band_interp_gate: bits/int/bits */
    { 0x45b3e0, 3, { 0, 0, 0, 0, 0, 0 } },   /* band_target_moved: bits/int/bits */
    { 0x403e80, 1, { 1, 0, 0, 0, 0, 0 } },   /* pos3_equal: b */
    { 0x458050, 6, { 0, 0, 0, 0, 1, 1 } },   /* grid_anchor: flag_slot, world */
    { 0x45e1d0, 2, { 1, 1, 0, 0, 0, 0 } },   /* grid_recompute: pos, req */
    { 0x44bbf0, 4, { 0, 0, 0, 0, 0, 0 } }    /* grid_query: 4 dword */
};

static const LeafAbi *leaf_abi_find(int va)
{
    for (int i = 0; i < (int)(sizeof(k_leaf_abi) / sizeof(k_leaf_abi[0])); i++) {
        if (k_leaf_abi[i].va == va) return &k_leaf_abi[i];
    }
    return NULL;
}

enum { NK_NUL = 0, NK_GS, NK_UNIT, NK_BAND, NK_NODE_E0, NK_NODE_E044, NK_TREE, NK_RAW };

/* Области арены в порядке привязок arena.cpp (перекрывающийся view "ai" не
 * участвует: он подмножество "unit"). */
static const char *const k_ptr_prio[] = {
    "unit", "band", "node_e0", "node_e044", "tree",
    "frame", "gs", "ctx", "player", "flags", "out"
};

/* shadow-адрес порта → (имя области, смещение); NULL — вне областей. */
static const char *port_ptr_region(const TickContext &tc, unsigned pv, int *off)
{
    if (pv == 0 || tc.regions == NULL) return NULL;
    for (int n = 0; n < (int)(sizeof(k_ptr_prio) / sizeof(k_ptr_prio[0])); n++) {
        for (int i = 0; i < tc.region_n; i++) {
            const RegionView &r = tc.regions[i];
            if (r.name == NULL || strcmp(r.name, k_ptr_prio[n]) != 0) continue;
            unsigned long b = (unsigned long)r.base;
            if ((unsigned long)pv >= b && (unsigned long)pv < b + r.len) {
                *off = (int)((unsigned long)pv - b);
                return r.name;
            }
        }
    }
    return NULL;
}

/* Сырой адрес записи → (база, смещение). Размеры баз — как в привязках
 * arena.cpp; всё остальное (куча игры, стек кадра) — NK_RAW. */
static int native_ptr_base(const TickRecord *rec, unsigned nv, int *off)
{
    if (rec == NULL || nv == 0) return NK_NUL;
    struct Base { unsigned raw; unsigned len; int kind; };
    unsigned band_len = (rec->band_n > 0)
        ? (unsigned)rec->band_n * BDD0_BAND_ELEM : BDD0_BAND_ELEM;
    const Base k_base[] = {
        /* gs идёт раньше unit: объект состояния лежит внутри блока unit
         * (возврат 442570 = self+0x64 в захватах), и указатель на gs-поле
         * обязан попадать в gs, а не в unit. */
        { rec->gs_raw,   BDD0_GS_LEN,                                NK_GS },
        { rec->self_raw, (unsigned)(BDD0_UNIT_LEN + BDD0_AI_LEN), NK_UNIT },
        { rec->band_raw, band_len,                                 NK_BAND },
        { rec->e0,       0x240u,                                   NK_NODE_E0 },
        { rec->e0_44,    0x240u,                                   NK_NODE_E044 },
        { rec->tree,     0x40u,                                    NK_TREE }
    };
    for (int i = 0; i < (int)(sizeof(k_base) / sizeof(k_base[0])); i++) {
        if (k_base[i].raw == 0) continue;
        if (nv >= k_base[i].raw && nv < k_base[i].raw + k_base[i].len) {
            *off = (int)(nv - k_base[i].raw);
            return k_base[i].kind;
        }
    }
    return NK_RAW;
}

static int kind_of_region_name(const char *n)
{
    if (n == NULL) return -1;
    if (strcmp(n, "unit") == 0) return NK_UNIT;
    if (strcmp(n, "gs") == 0) return NK_GS;
    if (strcmp(n, "band") == 0) return NK_BAND;
    if (strcmp(n, "node_e0") == 0) return NK_NODE_E0;
    if (strcmp(n, "node_e044") == 0) return NK_NODE_E044;
    if (strcmp(n, "tree") == 0) return NK_TREE;
    return -1;
}

enum { AC_NE = 0, AC_EQ = 1, AC_NOCMP = -1, AC_ANCHOR = 2 };

/* Кадр тика: база кадра вызывающего, выведенная из первого кадрового
 * указателя (в захвате адреса кадра нет — см. шапку проверки). */
struct FrameAnchor {
    int base, set;
};

/* Сравнение указателя: pv — shadow порта, nv — сырой адрес записи. */
static int cmp_ptr(const TickContext &tc, FrameAnchor &fa, unsigned pv,
                   unsigned nv)
{
    if (pv == 0 || nv == 0) return (pv == nv) ? AC_EQ : AC_NE;  /* NULL ≠ объект */
    int poff = 0;
    const char *pn = port_ptr_region(tc, pv, &poff);
    int noff = 0;
    int nk = native_ptr_base(tc.rec, nv, &noff);
    if (nk == NK_RAW) {
        /* адрес вне привязок: кадр вызывающего либо чужой объект */
        if (pn != NULL && strcmp(pn, "frame") == 0) {
            int base = (int)(nv - (unsigned)poff);
            if (!fa.set) {
                /* первое кадровое поле задаёт сдвиг кадра: само по себе оно
                 * ничего не доказывает (базы кадра в захвате нет) */
                fa.base = base;
                fa.set = 1;
                return AC_ANCHOR;
            }
            return (base == fa.base) ? AC_EQ : AC_NE;
        }
        return AC_NOCMP;
    }
    if (pn == NULL) return AC_NE;                /* порт дал не тот объект */
    if (kind_of_region_name(pn) != nk) return AC_NE;
    return (poff == noff) ? AC_EQ : AC_NE;
}

static CheckResult chk_leaf_args(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no leaf requests");
    }
    if (tc.hang) {
        SKIP("hang: leaf calls incomplete");
    }
    if (tc.leaf_violation) {
        /* поток не сопоставлен: аргументы нечем сверять (это leaf_seq) */
        SKIP("leaf_seq reports the leaf stream mismatch");
    }
    if (tc.log == NULL || tc.log_n <= 0) {
        SKIP("no leaf requests in this tick");
    }

    FrameAnchor fa;
    fa.base = 0;
    fa.set = 0;
    static char first[192];
    first[0] = '\0';
    static char nocmp[96];
    nocmp[0] = '\0';
    static char uninit[160];
    uninit[0] = '\0';
    static char noncomp[128];
    noncomp[0] = '\0';
    int mism = 0, ncmp = 0, nno = 0, nuninit = 0, nnoncomp = 0;

    for (int i = 0; i < tc.log_n; i++) {
        const CallLogEnt &e = tc.log[i];
        const LeafRec *r = bdd0_leaf_record_at(i);
        if (r == NULL) {
            SKIP("call not matched to a capture record");
        }
        const LeafAbi *abi = leaf_abi_find(e.va);
        if (abi == NULL) {
            return CheckResult{ 1, whyf(tc, "leaf #%d va=0x%x: argument ABI "
                                            "unknown to the check", i, e.va), 1 };
        }
        /* запись без args[] (старый формат bdd0_leaf) — сравнивать нечем */
        if (r->nargs == 0 && abi->nargs > 0) {
            SKIP("recorded record has no args[] (older bdd0_leaf format)");
        }
        if (e.nargs != abi->nargs || r->nargs != abi->nargs) {
            mism++;
            ncmp++;
            if (first[0] == '\0') {
                _snprintf_s(first, sizeof(first), _TRUNCATE,
                            "leaf #%d va=0x%x argc port=%d native=%d abi=%d "
                            "(seq %d)", i, e.va, e.nargs, r->nargs,
                            abi->nargs, tc.seq);
            }
        }

        /* поля: ecx (указатель), затем каждый записанный аргумент */
        for (int f = -1; f < abi->nargs; f++) {
            unsigned pv = (f < 0) ? (unsigned)e.ecx : (unsigned)e.args[f];
            unsigned nv = (f < 0) ? (unsigned)r->ecx : (unsigned)r->args[f];
            int ptr = (f < 0) ? 1 : abi->ptr[f];
            if (f >= 0) {
                const UninitArg *u = uninit_find(r->site, e.va, f);
                if (u != NULL) {
                    g_uninit_skipped++;
                    nuninit++;
                    if (uninit[0] == '\0') {
                        _snprintf_s(uninit, sizeof(uninit), _TRUNCATE,
                                    "%s; %s", u->why,
                                    (r->site == 0) ? "site not recorded"
                                                   : "site matched");
                    }
                    continue;
                }
                /* Тот же путь, что и native_args. Листевых записей в
                 * k_noncomparable_args нет: ни один leaf-сайт не открывает
                 * by-value 60-байтной области (см. шапку списка), поэтому
                 * поиск здесь всегда NULL. */
                const NoncompArg *nc = noncomp_find(r->site, e.va, f);
                if (nc != NULL) {
                    g_noncomp_skipped++;
                    nnoncomp++;
                    if (noncomp[0] == '\0') {
                        _snprintf_s(noncomp, sizeof(noncomp), _TRUNCATE,
                                    "%s; site 0x%x arg[%d]", nc->why,
                                    (unsigned)r->site, f);
                    }
                    continue;
                }
            }
            int res = ptr ? cmp_ptr(tc, fa, pv, nv)
                          : ((pv == nv) ? AC_EQ : AC_NE);
            if (res == AC_EQ) {
                ncmp++;
            } else if (res == AC_ANCHOR) {
                /* сдвиг кадра задан; поле этим не проверено и за pass не
                 * считается (см. шапку проверки) */
            } else if (res == AC_NE) {
                mism++;
                ncmp++;
                if (first[0] == '\0') {
                    if (f < 0) {
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "leaf #%d va=0x%x ecx port=0x%08x "
                                    "native=0x%08x (seq %d)", i, e.va, pv, nv,
                                    tc.seq);
                    } else {
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "leaf #%d va=0x%x arg[%d] port=0x%08x "
                                    "native=0x%08x (seq %d)", i, e.va, f, pv,
                                    nv, tc.seq);
                    }
                }
            } else {
                /* адрес натива не привязывается ни к одной базе */
                nno++;
                if (nocmp[0] == '\0') {
                    _snprintf_s(nocmp, sizeof(nocmp), _TRUNCATE,
                                "leaf #%d va=0x%x %s=0x%08x", i, e.va,
                                (f < 0) ? "ecx" : "arg", nv);
                }
            }
        }

        /* w[] — dword'ы, записанные листом ЧЕРЕЗ указатель: сравнение с тем,
         * что обёртка реально записала (отсутствие ≠ нули). */
        int aw[3];
        int anw = bdd0_leaf_applied_w(i, aw);
        if (r->nw != anw) {
            mism++;
            ncmp++;
            if (first[0] == '\0') {
                _snprintf_s(first, sizeof(first), _TRUNCATE,
                            "leaf #%d va=0x%x w count port=%d native=%d "
                            "(seq %d)", i, e.va, anw, r->nw, tc.seq);
            }
        } else {
            for (int k = 0; k < anw; k++) {
                if (aw[k] == r->w[k]) {
                    ncmp++;
                } else {
                    mism++;
                    ncmp++;
                    if (first[0] == '\0') {
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "leaf #%d va=0x%x w[%d] port=0x%08x "
                                    "native=0x%08x (seq %d)", i, e.va, k,
                                    (unsigned)aw[k], (unsigned)r->w[k],
                                    tc.seq);
                    }
                }
            }
        }
    }

    if (mism > 0) {
        /* первое расхождение — в отчёт ядра; остальные — счётчиком в сводку */
        if (mism > 1) {
            fprintf(stderr, "leaf_args: seq %d: %d mismatched fields "
                            "(first reported)\n", tc.seq, mism);
        }
        return CheckResult{ 0, first, 0 };
    }
    if (nuninit > 0) {
        return CheckResult{ 1, uninit, 1 };
    }
    if (nnoncomp > 0) {
        return CheckResult{ 1, noncomp, 1 };
    }
    if (nno > 0) {
        /* ни одного расхождения, но часть адресов сравнить не с чем */
        return CheckResult{ 1, whyf(tc, "%d address field(s) not comparable "
                                        "(%s)", nno, nocmp), 1 };
    }
    if (ncmp == 0) {
        SKIP("no comparable argument fields");
    }
    OK();
}

/* native_seq — v12: поток НЕ-листовых RECORD-нативов (`bdd0_native`) обязан
 * быть израсходован целиком и в том же порядке.  Если в захвате этого seq нет
 * bdd0_native записей (старые файлы), проверке нечем мерить — SKIP, не pass.
 * trap-запись и исчерпание потока ставит replay-слой (tc.native_violation). */
const NativeRec *bdd0_native_pending(void);    /* native_replay.cpp */
int bdd0_native_consumed(void);                /* native_replay.cpp */

static CheckResult chk_native_seq(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no native requests");
    }
    if (tc.hang) {
        SKIP("hang: native stream incomplete");
    }
    if (tc.rec == NULL || tc.rec->native_n == 0) {
        SKIP("capture has no bdd0_native records for this seq");
    }
    if (tc.native_violation) {
        return CheckResult{ 0, tc.native_violation_why, 0 };
    }
    int recorded = tc.rec->native_n;
    int consumed = bdd0_native_consumed();
    if (consumed > recorded) {
        return CheckResult{ 0, whyf(tc, "consumed %d native records of %d - "
                                        "port made more calls than native",
                                    consumed, recorded), 0 };
    }
    if (consumed < recorded) {
        const NativeRec *u = bdd0_native_pending();
        return CheckResult{ 0, whyf(tc, "unconsumed bdd0_native #%d site=0x%x "
                                        "va=0x%x (order %d) - port made fewer "
                                        "calls than native (consumed %d of %d)",
                                    consumed, (u != NULL) ? (unsigned)u->site : 0u,
                                    (u != NULL) ? (unsigned)u->va : 0u,
                                    (u != NULL) ? u->order : -1,
                                    consumed, recorded), 0 };
    }
    OK();
}

/* res_match — исход порта (al/out/u170) против записанного bdd0_res того же
 * seq.  Это первая проверка уровня РЕШЕНИЯ (не механики): любое расхождение
 * означает, что порт принял другой исход.  Нет res / res_trap — SKIP. */
static CheckResult chk_res_match(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no port outcome");
    }
    if (tc.rec == NULL || !tc.rec->has_res) {
        SKIP("capture has no bdd0_res for this seq");
    }
    if (tc.rec->res_trap) {
        SKIP("bdd0_res trap=1: recorded outcome not trusted");
    }
    int al_rec = tc.rec->res_al & 0xff;
    int al_port = tc.al & 0xff;
    int ok_al = (al_port == al_rec);
    int ok_out = (tc.out_slot == tc.rec->res_out);
    int ok_u170 = (tc.u170 == tc.rec->res_u170);
    if (!ok_u170 && tc.rec->native_eee0_no_ww && ok_al && ok_out) {
        SKIP("u170 writes not captured in bdd0_native (older 45eee0 record)");
    }
    if (!ok_al || !ok_out || !ok_u170) {
        return CheckResult{ 0, whyf(tc, "outcome mismatch: al port=%d "
                                        "native=%d; out port=%d native=%d; "
                                        "u170 port=%d native=%d",
                                    al_port, al_rec, tc.out_slot,
                                    tc.rec->res_out, tc.u170,
                                    tc.rec->res_u170), 0 };
    }
    OK();
}

/* state_match — пост-состояние pos-тройки unit+0xa4..0xac (m0..m2) против
 * записанных res_m того же seq.  Отдельно от res_match, чтобы расхождение
 * было атрибутировано: решение vs состояние. */
static CheckResult chk_state_match(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no post-state");
    }
    if (tc.rec == NULL || !tc.rec->has_res) {
        SKIP("capture has no bdd0_res for this seq");
    }
    if (tc.rec->res_trap) {
        SKIP("bdd0_res trap=1: recorded state not trusted");
    }
    unsigned m0 = tc.field("m0");
    unsigned m1 = tc.field("m1");
    unsigned m2 = tc.field("m2");
    if (m0 == tc.rec->res_m[0] && m1 == tc.rec->res_m[1] &&
        m2 == tc.rec->res_m[2]) {
        OK();
    }
    return CheckResult{ 0, whyf(tc, "state mismatch: m0 port=0x%08x native="
                                    "0x%08x; m1 port=0x%08x native=0x%08x; "
                                    "m2 port=0x%08x native=0x%08x",
                                m0, tc.rec->res_m[0], m1, tc.rec->res_m[1],
                                m2, tc.rec->res_m[2]), 0 };
}

/* ------------------------------- native_args ----------------------------- */
/* Сверка аргументов порта с записанными `bdd0_native`.  Указатели сравниваются
 * символьно (cmp_ptr), нецелые — побитово.  Несвязанный указатель с любой
 * стороны = SKIP (не pass), с счётчиком unbound.  trap/старый формат — SKIP. */

struct NativeAbi {
    int va, slot, ecx_ptr, nargs;
    int ptr[8];
};

/* ecx_ptr: 1 — ECX — указатель (this), сравнивать символьно; 0 — не сравнивать
 * (cdecl-цель 439ae0: ECX на сайте — остаток, не аргумент).  ptr[i]: 1 —
 * arg[i] указатель. */
static const NativeAbi k_native_abi[] = {
    { 0x442570, 0, 1, 1, { 0,0,0,0,0,0,0,0 } },  /* game_state_get */
    { 0x439ae0, 0, 0, 2, { 1,1,0,0,0,0,0,0 } },  /* string_obj_cmp (cdecl) */
    { 0x42da60, 0, 1, 1, { 0,0,0,0,0,0,0,0 } },  /* player_flag_get */
    { 0x435d50, 0, 1, 0, { 0,0,0,0,0,0,0,0 } },  /* player_flags */
    { 0x448c30, 0, 1, 1, { 0,0,0,0,0,0,0,0 } },  /* cursor_mode_update */
    { 0x446a80, 0, 1, 1, { 1,0,0,0,0,0,0,0 } },  /* ai_time_update msg */
    { 0x448ee0, 0, 1, 1, { 0,0,0,0,0,0,0,0 } },  /* state_flag_log */
    { 0x42d9c0, 0, 1, 1, { 1,0,0,0,0,0,0,0 } },  /* player_pos_set triple */
    { 0x45f430, 0, 1, 0, { 0,0,0,0,0,0,0,0 } },  /* pathfinder_init */
    { 0x45eee0, 0, 1, 1, { 1,0,0,0,0,0,0,0 } },  /* unit_ai_tick rec */
    { 0x000000, 1, 1, 8, { 0,0,0,0,1,1,0,0 } },  /* u+0x28 snapshot */
    { 0x000000, 2, 1, 0, { 0,0,0,0,0,0,0,0 } },  /* u+0x2c phase_time */
    { 0x000000, 3, 1, 1, { 1,0,0,0,0,0,0,0 } },  /* u+0x38 pointer */
    { 0x000000, 4, 1, 1, { 0,0,0,0,0,0,0,0 } }   /* u+0x40 player_get */
};

static const NativeAbi *native_abi_find(const NativeRec *r)
{
    int i;
    for (i = 0; i < (int)(sizeof(k_native_abi) / sizeof(k_native_abi[0])); i++) {
        const NativeAbi *a = &k_native_abi[i];
        if (r->slot != 0) {
            if (a->slot == r->slot) return a;
        } else if (a->slot == 0 && a->va == r->va) {
            return a;
        }
    }
    return NULL;
}

static CheckResult chk_native_args(const TickContext &tc)
{
    int log_n = 0;
    const NativeCallLogEnt *log;

    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no native requests");
    }
    if (tc.hang) {
        SKIP("hang: native calls incomplete");
    }
    if (tc.rec == NULL || tc.rec->native_n == 0) {
        SKIP("capture has no bdd0_native records for this seq");
    }
    if (tc.native_violation) {
        SKIP("native_seq reports the native stream mismatch");
    }
    log = bdd0_native_calllog(&log_n);
    if (log == NULL || log_n == 0) {
        SKIP("no native requests in this tick");
    }

    FrameAnchor fa;
    fa.base = 0;
    fa.set = 0;
    static char first[224];
    first[0] = '\0';
    static char uninit[160];
    uninit[0] = '\0';
    static char noncomp[128];
    noncomp[0] = '\0';
    int mism = 0, ncmp = 0, unbound = 0, nuninit = 0, nnoncomp = 0;
    char unb[128];
    unb[0] = '\0';

    for (int i = 0; i < log_n; i++) {
        const NativeCallLogEnt &e = log[i];
        const NativeRec *r = bdd0_native_record_at(i);
        if (r == NULL) {
            SKIP("native request not matched to a capture record");
        }
        if (r->trap) {
            SKIP("recorded bdd0_native trap=1");
        }
        const NativeAbi *abi = native_abi_find(r);
        if (abi == NULL) {
            return CheckResult{ 1, whyf(tc, "native #%d va=0x%x slot=%d: "
                                            "argument ABI unknown", i,
                                        (unsigned)r->va, r->slot), 1 };
        }
        if (r->nargs == 0 && abi->nargs > 0) {
            SKIP("recorded bdd0_native has no args[] (older format)");
        }
        if (e.nargs != abi->nargs || r->nargs != abi->nargs) {
            mism++;
            ncmp++;
            if (first[0] == '\0') {
                _snprintf_s(first, sizeof(first), _TRUNCATE,
                            "native #%d site=0x%x argc port=%d native=%d "
                            "abi=%d (seq %d)", i, (unsigned)r->site, e.nargs,
                            r->nargs, abi->nargs, tc.seq);
            }
        }
        for (int f = -1; f < abi->nargs; f++) {
            unsigned pv, nv;
            int ptr = 0;
            if (f < 0) {
                if (!abi->ecx_ptr) continue;
                pv = (unsigned)e.ecx;
                nv = (unsigned)r->ecx;
                ptr = 1;
            } else {
                if (f >= e.nargs || f >= r->nargs) continue;
                pv = (unsigned)e.args[f];
                nv = (unsigned)r->args[f];
                ptr = abi->ptr[f];
                const UninitArg *u = uninit_find(r->site, r->va, f);
                if (u != NULL) {
                    g_uninit_skipped++;
                    nuninit++;
                    if (uninit[0] == '\0') {
                        _snprintf_s(uninit, sizeof(uninit), _TRUNCATE,
                                    "%s; %s", u->why,
                                    (r->site == 0) ? "site not recorded"
                                                   : "site matched");
                    }
                    continue;
                }
                const NoncompArg *nc = noncomp_find(r->site, r->va, f);
                if (nc != NULL) {
                    g_noncomp_skipped++;
                    nnoncomp++;
                    if (noncomp[0] == '\0') {
                        _snprintf_s(noncomp, sizeof(noncomp), _TRUNCATE,
                                    "%s; site 0x%x arg[%d]", nc->why,
                                    (unsigned)r->site, f);
                    }
                    continue;
                }
            }
            int res;
            if (ptr) {
                res = cmp_ptr(tc, fa, pv, nv);
            } else {
                res = (pv == nv) ? AC_EQ : AC_NE;
            }
            if (res == AC_EQ) {
                ncmp++;
            } else if (res == AC_ANCHOR) {
                /* frame base anchored; not a verified field */
            } else if (res == AC_NE) {
                mism++;
                ncmp++;
                if (first[0] == '\0') {
                    if (f < 0) {
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "native #%d site=0x%x ecx port=0x%08x "
                                    "native=0x%08x (seq %d)", i,
                                    (unsigned)r->site, pv, nv, tc.seq);
                    } else {
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "native #%d site=0x%x arg[%d] port=0x%08x "
                                    "native=0x%08x (seq %d)", i,
                                    (unsigned)r->site, f, pv, nv, tc.seq);
                    }
                }
            } else {
                unbound++;
                if (unb[0] == '\0') {
                    _snprintf_s(unb, sizeof(unb), _TRUNCATE,
                                "native #%d site=0x%x %s=0x%08x", i,
                                (unsigned)r->site, (f < 0) ? "ecx" : "arg",
                                nv);
                }
            }
        }
    }

    if (mism > 0) {
        if (mism > 1) {
            fprintf(stderr, "native_args: seq %d: %d mismatched fields "
                            "(first reported)\n", tc.seq, mism);
        }
        return CheckResult{ 0, first, 0 };
    }
    if (nuninit > 0) {
        return CheckResult{ 1, uninit, 1 };
    }
    if (nnoncomp > 0) {
        return CheckResult{ 1, noncomp, 1 };
    }
    if (unbound > 0) {
        return CheckResult{ 1, whyf(tc, "%d unbound pointer field(s) (%s); "
                                        "not comparable", unbound, unb), 1 };
    }
    if (ncmp == 0) {
        SKIP("no comparable argument fields");
    }
    OK();
}

void register_builtin_checks(void)
{
    register_check({ "no_trap",      chk_no_trap,
                     NEED_POST_STATE | NEED_BODY });
    register_check({ "bounds",       chk_bounds,
                     NEED_POST_STATE | NEED_HANDLES | NEED_BODY });
    register_check({ "budget",       chk_budget,
                     NEED_TIMING | NEED_CALLLOG | NEED_BODY });
    register_check({ "contract_min", chk_contract_min,
                     NEED_DECISIONS | NEED_POST_STATE });
    register_check({ "leaf_seq",     chk_leaf_seq,
                     NEED_BODY });
    register_check({ "native_seq",   chk_native_seq,
                     NEED_BODY });
    register_check({ "native_args",  chk_native_args,
                     NEED_BODY });
    register_check({ "res_match",    chk_res_match,
                     NEED_DECISIONS | NEED_BODY });
    register_check({ "state_match",  chk_state_match,
                     NEED_POST_STATE | NEED_BODY });
    register_check({ "leaf_args",    chk_leaf_args,
                     NEED_CALLLOG | NEED_BODY });
    register_check({ "handles",      chk_handles,
                     NEED_HANDLES });
}
