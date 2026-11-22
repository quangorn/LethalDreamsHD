/* tools/probes/bdd0/checks_builtin.cpp — встроенный набор проверок.
 *
 * Критерий приёмки зафиксирован один: нет падения, нет зависания, нет записи
 * вне объявленных областей состояния. Поведенческих сравнений (равенство
 * решений/потока листьев/состояния AI) здесь нет: такие проверки добавляются
 * отдельным файлом через register_check(), ядро при этом не меняется.
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

/* leaf_seq — механическая: replay-слой обязан отдать ровно ту последовательность
 * листьев, что записана (va + порядок). Расхождение = поток не воспроизведён. */
static CheckResult chk_leaf_seq(const TickContext &tc)
{
    if (!tc.ran_body) {
        SKIP("impl does not execute the body: no leaf requests");
    }
    if (tc.hang) {
        SKIP("hang: leaf stream incomplete");
    }
    if (tc.leaf_violation) {
        return CheckResult{ 0, tc.leaf_violation_why, 0 };
    }
    OK();
}

/* handles — механическая: каждый указатель, прочитанный телом из снимка,
 * материализован (иначе bounds/contract_min прогоняются на мусоре). */
static CheckResult chk_handles(const TickContext &tc)
{
    if (tc.handle_miss) {
        return CheckResult{ 0, tc.handle_miss_why, 0 };
    }
    if (tc.hang) {
        SKIP("hang: pointer use not completed");
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
                     NEED_CALLLOG | NEED_BODY });
    register_check({ "handles",      chk_handles,
                     NEED_HANDLES });
}
