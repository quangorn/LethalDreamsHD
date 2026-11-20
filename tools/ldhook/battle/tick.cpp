/* battle/tick.cpp — верхний уровень боевого AI (FUN_0044bdd0, тик).
 *
 * Фаза 5: детур FUN_0044bdd0 (state machine по 1.3a, все листья — нативные
 * обёртки native::…). Реализация трассировки тика (LDHOOK_BATTLE_TRACE) и
 * наблюдатели H1/H2/H7 живут в battle/trace.cpp (trace.h — API и формат).
 * Здесь пока нет кода: точка подключения детура — Фаза 5.
 */
#include "../ldhook_internal.h"
#include "battle.h"
