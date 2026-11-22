/* tools/probes/bdd0/impl_registry.cpp — реестр реализаций тика.
 *
 * ImplDef = { name, prepare, tick, release, runs_body }; ядро знает реализацию
 * только как имя + функцию. Сравнение реализаций между собой — не здесь и не в
 * ядре (это колонка отчёта).
 *
 * Будущая rewrite добавляется ОДНОЙ строкой в register_builtin_impls():
 *
 *     register_impl({ "new", NULL, (TickFn)&b_new_tick_body, NULL, 1 });
 *
 * (плюс новый .cpp в списке сборки). Ни ядро, ни проверки при этом не меняются.
 */
#include <string.h>

#include "harness.h"
#include "../../ldhook/battle/battle.h"

#define IMPL_MAX 8
static ImplDef g_impls[IMPL_MAX];
static int g_impl_n = 0;

void register_impl(const ImplDef &def)
{
    if (g_impl_n >= IMPL_MAX) return;
    for (int i = 0; i < g_impl_n; i++) {
        if (strcmp(g_impls[i].name, def.name) == 0) {
            g_impls[i] = def;
            return;
        }
    }
    g_impls[g_impl_n++] = def;
}

int impl_count(void) { return g_impl_n; }

const ImplDef *impl_at(int i)
{
    if (i < 0 || i >= g_impl_n) return NULL;
    return &g_impls[i];
}

const ImplDef *find_impl(const char *name)
{
    if (name == NULL) return NULL;
    for (int i = 0; i < g_impl_n; i++) {
        if (strcmp(g_impls[i].name, name) == 0) return &g_impls[i];
    }
    return NULL;
}

void register_builtin_impls(void)
{
    /* port — реальное портированное тело (battle/tick.cpp под BATTLE_REPLAY);
     * все 234 нативных сайта идут через native::*-обёртки replay-слоя. */
    register_impl({ "port", NULL, (TickFn)&b0_battle_ai_dispatch, NULL, 1 });
    /* native — тело НЕ исполняется: отчёт = записанный исход (калибровка
     * проверок; любые «проверено» на нём помечаются как не-проверенные). */
    register_impl({ "native", NULL, NULL, NULL, 0 });
}
