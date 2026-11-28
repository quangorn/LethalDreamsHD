/* battle/occ.cpp — наблюдатель занятости клеток мобов (LDHOOK_MOB_OCC).
 * Формат строки, семантика полей и sentinel'ы — battle/occ.h.
 *
 * Гейт разбирается в mob_occ_init() (initldhook); выключенный флаг — единственный
 * branch в mob_occ_enabled(), ни чтений игровой памяти, ни строк лога.
 * Все разыменования — под __try/__except (идиома battle/trace.cpp): при сбое
 * поля строки становятся sentinel'ами, игра не тронута.
 * Счётчик выборки — фиксированная таблица 128 слотов (линейный поиск по
 * указателю юнита, без аллокаций); счётчик сбрасывается только на переполнение
 * таблицы — юнит выпадает из выборки, остальные продолжают.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "occ.h"
#include "trace.h"

#define OCC_UNIT_OFF_AI      0x138   /* unit → BattleAI (= game_ctx) */
#define OCC_UNIT_OFF_MOVE    0x68    /* unit → тройка движения (+0x68..+0x70) */
#define OCC_UNIT_OFF_TARGET  0xa4    /* unit → тройка цели (+0xa4..+0xac) */
#define OCC_CTX_OFF_TRI      0x0c    /* game_ctx+0xc → указатель на тройку */
#define OCC_AI_OFF_S1        0x70    /* BattleAI → размер map1 */
#define OCC_AI_OFF_S2        0x7c    /* BattleAI → размер map2 */

#define OCC_UNITS            128     /* слотов таблицы счётчиков */
#define OCC_EVERY_DEF        10      /* K по умолчанию: каждый 10-й тик юнита */

#define OCC_SHARED_MAX       128     /* слотов реестра общей занятости */

typedef struct {
    void *unit;                      /* NULL = слот пуст */
    long  n;                         /* тиков юнита с момента появления */
} occ_slot;

typedef struct {
    void *self;                      /* BattleAI (unit+0x138); NULL = слот пуст */
    int   x;                         /* клетка скана (x = edi) */
    int   y;                         /* клетка скана (y = ebp) */
} occ_shared_slot;

static int      g_on = 0;            /* 1 = наблюдение включено */
static long     g_every = OCC_EVERY_DEF;
static occ_slot g_tbl[OCC_UNITS];
static int      g_shared_on = 0;     /* реестр: 1 пропуск, 2 штраф */
static float    g_shared_pen = 10.0f;
static occ_shared_slot g_shared[OCC_SHARED_MAX];

/* env-флаг: непустое числовое значение != 0 либо ведущий `t`/`T`
 * (atoi("true") == 0). */
static int occ_env_flag(const char *name)
{
    const char *s = getenv(name);

    if (s == NULL || *s == '\0') {
        return 0;
    }
    if (*s == 't' || *s == 'T') {
        return 1;
    }
    return (atoi(s) != 0);
}

void mob_occ_init(void)
{
    const char *s;
    long k;

    g_on = 0;
    g_every = OCC_EVERY_DEF;
    memset(g_tbl, 0, sizeof(g_tbl));
    if (!occ_env_flag(MOB_OCC_ENV)) {
        return;                  /* инертен: ни строк, ни таблицы */
    }
    s = getenv(MOB_OCC_EVERY_ENV);
    if (s != NULL && *s != '\0') {
        k = strtol(s, NULL, 10);
        g_every = (k > 0) ? k : 1;   /* K <= 0 эквивалентно 1 */
    }
    g_on = 1;
    ld_info("mob_occ: %s=1 every=%ld (строка OCC на выборке тиков юнита)",
            MOB_OCC_ENV, g_every);
    g_shared_on = 0;
    g_shared_pen = 10.0f;
    memset(g_shared, 0, sizeof(g_shared));
    s = getenv(MOB_OCC_SHARED_ENV);
    if (s != NULL && *s != '\0') {
        long m = strtol(s, NULL, 10);
        if (m == 't' || m == 'T') {
            m = 1;                       /* ведущий t/T = пропуск */
        }
        if (m == 1 || m == 2) {
            g_shared_on = (int)m;
            s = getenv(MOB_OCC_PEN_ENV);
            if (s != NULL && *s != '\0') {
                g_shared_pen = (float)atof(s);
            }
            ld_info("mob_occ: %s=%d pen=%g (%s)", MOB_OCC_SHARED_ENV, g_shared_on,
                    g_shared_pen,
                    g_shared_on == 1 ? "радиальный скан cb40 пропускает клетки других юнитов"
                                     : "радиальный скан cb40 штрафует клетки других юнитов");
        }
    }
}

int mob_occ_enabled(void)
{
    return g_on;
}

int occ_shared_on(void)
{
    return g_shared_on;
}

float occ_shared_pen(void)
{
    return g_shared_pen;
}

/* Клетка юнита при входе cb40: линейный поиск по self, вставка в первый пустой.
 * Реестр растёт до 128 юнитов; переполнение — слот не записывается. */
void occ_shared_note(void *self, int x, int y)
{
    int i;

    for (i = 0; i < OCC_SHARED_MAX; i++) {
        if (g_shared[i].self == self) {
            g_shared[i].x = x;
            g_shared[i].y = y;
            return;
        }
        if (g_shared[i].self == NULL) {
            g_shared[i].self = self;
            g_shared[i].x = x;
            g_shared[i].y = y;
            return;
        }
    }
}

int occ_shared_other(void *self, int x, int y)
{
    int i;

    for (i = 0; i < OCC_SHARED_MAX; i++) {
        if (g_shared[i].self != NULL && g_shared[i].self != self &&
            g_shared[i].x == x && g_shared[i].y == y) {
            return 1;
        }
    }
    return 0;
}

/* Слот счётчика юнита: линейный поиск, вставка в первый пустой.
 * NULL — таблица полна (юнит выпадает из выборки). */
static occ_slot *occ_slot_get(void *unit)
{
    int i;

    for (i = 0; i < OCC_UNITS; i++) {
        if (g_tbl[i].unit == unit) {
            return &g_tbl[i];
        }
        if (g_tbl[i].unit == NULL) {
            g_tbl[i].unit = unit;
            g_tbl[i].n = 0;
            return &g_tbl[i];
        }
    }
    return NULL;
}

void mob_occ_unit_tick(void *unit, int unit_id, long tick)
{
    occ_slot *sl;
    long n;
    int code;
    int fault = 0;
    int cx = -1, cy = -1, cz = -1;
    int px = -1, py = -1, pz = -1;
    unsigned int t0 = 0xFFFFFFFFu, t1 = 0xFFFFFFFFu, t2 = 0xFFFFFFFFu;
    long s1 = -1, s2 = -1;

    if (!g_on || unit == NULL || unit_id <= 0) {
        return;
    }
    sl = occ_slot_get(unit);
    if (sl == NULL) {
        return;                  /* таблица переполнена */
    }
    n = ++sl->n;
    if (n != 1 && (n % g_every) != 0) {
        return;                  /* выборка: первый тик юнита и каждый K-й */
    }
    ld_crash_enter(7);           /* stage 7 (H1/H3), no-op off */
    __try {
        const unsigned char *u = (const unsigned char *)unit;
        void *gctx = *(void * const *)(u + OCC_UNIT_OFF_AI);

        px = *(const int *)(u + OCC_UNIT_OFF_MOVE + 0);
        py = *(const int *)(u + OCC_UNIT_OFF_MOVE + 4);
        pz = *(const int *)(u + OCC_UNIT_OFF_MOVE + 8);
        t0 = *(const unsigned int *)(u + OCC_UNIT_OFF_TARGET + 0);
        t1 = *(const unsigned int *)(u + OCC_UNIT_OFF_TARGET + 4);
        t2 = *(const unsigned int *)(u + OCC_UNIT_OFF_TARGET + 8);
        if (gctx != NULL) {
            const void *tri = *(void * const *)(
                (const unsigned char *)gctx + OCC_CTX_OFF_TRI);

            if (tri != NULL) {
                cx = ((const int *)tri)[0];
                cy = ((const int *)tri)[1];
                cz = ((const int *)tri)[2];
            }
            s1 = *(const long *)((const unsigned char *)gctx + OCC_AI_OFF_S1);
            s2 = *(const long *)((const unsigned char *)gctx + OCC_AI_OFF_S2);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        /* Сбой любого разыменования — вся строка в sentinel'ах. */
        cx = cy = cz = -1;
        px = py = pz = -1;
        t0 = t1 = t2 = 0xFFFFFFFFu;
        s1 = s2 = -1;
        fault = 1;
    }
    code = fault ? -1 : battle_trace_current_code();
    ld_info("OCC tick=%ld unit=%d cx=%d cy=%d cz=%d px=%d py=%d pz=%d "
            "t0=%08x t1=%08x t2=%08x s1=%ld s2=%ld code=%d",
            tick, unit_id, cx, cy, cz, px, py, pz, t0, t1, t2, s1, s2, code);
    ld_crash_leave();
}
