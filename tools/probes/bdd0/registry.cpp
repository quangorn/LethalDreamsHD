/* tools/probes/bdd0/registry.cpp — реестр проверок, таблица полей, why-буферы.
 *
 * Единственное место, где живёт знание об оффсетах: таблица k_fields.
 * Проверка читает значение ПО ИМЕНИ (tc.field("u170")), никогда не считает
 * адрес. Новое поле — строка таблицы, не код ядра.
 *
 * Таблица проверок: глобальный вектор, наполняется register_check() из
 * register_builtin_checks(); ядро (bdd0_harness.cpp) реестр не трогает, оно
 * только фильтрует по CLI-имени и считает pass/skip/fail.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "harness.h"

/* ------------------------------ поля тика -------------------------------- */
/* type: 0 = i32, 1 = f32 (биты), 2 = u32. region == NULL → специальное поле,
 * которое ядро кладёт в TickContext (оно не лежит в области состояния). */
struct FieldDef {
    const char *name;
    const char *region;
    int off;
    int type;
};

static const FieldDef k_fields[] = {
    /* решения тика (спец-поля TickContext) */
    { "al",      NULL,   0,     2 },  /* возврат реализации (AL) */
    { "out",     NULL,   0,     0 },  /* *param_3 через frame+0x164 */
    { "u170",    "unit", 0x170, 0 },  /* код тика */
    /* документированные домены contract_min */
    { "gate114", "unit", 0x114, 1 },  /* float-гейт пригодности тика */
    { "m0",      "unit", 0xa4,  1 },  /* pos-тройка R14 (float-биты) */
    { "m1",      "unit", 0xa8,  1 },
    { "m2",      "unit", 0xac,  1 },
    { "target_x","unit", 0xa8,  1 },  /* = m1: цель по X */
    { "target_y","unit", 0xac,  1 }   /* = m2: цель по Y */
};

int field_count(void) { return (int)(sizeof(k_fields) / sizeof(k_fields[0])); }

static const FieldDef *field_find(const char *name)
{
    for (int i = 0; i < field_count(); i++) {
        if (strcmp(k_fields[i].name, name) == 0) return &k_fields[i];
    }
    return NULL;
}

/* ------------------------- read-only доступ проверок --------------------- */
const unsigned char *TickContext::region(const char *name) const
{
    for (int i = 0; i < region_n; i++) {
        if (strcmp(regions[i].name, name) == 0) return regions[i].base;
    }
    return NULL;
}

const unsigned char *TickContext::region_before(const char *name) const
{
    for (int i = 0; i < region_before_n; i++) {
        if (strcmp(regions_before[i].name, name) == 0) {
            return regions_before[i].base;
        }
    }
    return NULL;
}

unsigned TickContext::field(const char *name) const
{
    if (name == NULL) return 0;
    if (strcmp(name, "al") == 0) return (unsigned)al;
    if (strcmp(name, "out") == 0) return (unsigned)out_slot;
    const FieldDef *f = field_find(name);
    if (f == NULL) return 0;
    const unsigned char *p = region(f->region);
    if (p == NULL) return 0;
    unsigned v;
    memcpy(&v, p + f->off, 4);
    return v;
}

/* Запись поля по имени: вызывающему (ядру) не нужен оффсет. */
int field_write(Arena *a, const char *name, unsigned value)
{
    const FieldDef *f = field_find(name);
    if (f == NULL || f->region == NULL) return 0;
    unsigned char *p = arena_region_w(a, f->region);
    if (p == NULL) return 0;
    memcpy(p + f->off, &value, 4);
    return 1;
}

/* ------------------------------- реестр ---------------------------------- */
#define BDD0_CHECK_MAX 32
static CheckDef g_checks[BDD0_CHECK_MAX];
static int g_check_n = 0;

void register_check(const CheckDef &def)
{
    if (g_check_n >= BDD0_CHECK_MAX) return;   /* молча терять нельзя — рвём */
    for (int i = 0; i < g_check_n; i++) {
        if (strcmp(g_checks[i].name, def.name) == 0) {
            g_checks[i] = def;                  /* перерегистрация = замена */
            return;
        }
    }
    g_checks[g_check_n++] = def;
}

int check_count(void) { return g_check_n; }

const CheckDef *check_at(int i)
{
    if (i < 0 || i >= g_check_n) return NULL;
    return &g_checks[i];
}

const char *need_name(unsigned bit)
{
    switch (bit) {
    case NEED_DECISIONS:  return "decisions";
    case NEED_POST_STATE: return "post_state";
    case NEED_CALLLOG:    return "calllog";
    case NEED_TIMING:     return "timing";
    case NEED_HANDLES:    return "handles";
    case NEED_GAMESTATE:  return "gamestate";
    case NEED_BODY:       return "body";
    default:              return "?";
    }
}

/* Стабильные строки why: 4 кольцевых буфера на тик (ядро печатает сразу). */
const char *whyf(const TickContext &tc, const char *fmt, ...)
{
    static char ring[4][192];
    static int idx = 0;
    char *dst = (tc.why_buf != NULL) ? tc.why_buf
                                     : ring[idx = (idx + 1) & 3];
    unsigned cap = (tc.why_buf != NULL) ? tc.why_cap : sizeof(ring[0]);
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    dst[cap - 1] = '\0';
    return dst;
}
