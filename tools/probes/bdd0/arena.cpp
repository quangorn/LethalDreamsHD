/* tools/probes/bdd0/arena.cpp — материализация областей состояния тика.
 *
 * Каждая область объявлена (имя + длина) и лежит в собственной аллокации:
 *
 *      [PAGE_NOACCESS][canary_pre 64B][pad][ данные ][PAGE_NOACCESS]
 *
 * Данные кончаются РОВНО на границе страницы, поэтому запись за конец области
 * упирается в PAGE_NOACCESS → EXCEPTION_ACCESS_VIOLATION (VEH ядра, первый
 * шанс; SEH-рамка самого тела 44bdd0 её проглатывает и возвращает 0). Канарейка
 * перед данными ловит запись до начала области. Вся прочая память арены
 * (shadow-объекты) тоже отдельные такие аллокации — «свободной» памяти нет.
 *
 * --------------------- карта указателей снимка (единая таблица) ------------
 * Тело 44bdd0 разыменовывает из снимка ровно четыре указателя:
 *
 *   1. frame+0x164            — param_3 (out-код). Аргумент вызывающего, не
 *                               объект снимка: любое ненулевое значение означает
 *                               «пиши сюда». Переписывается на слот "out".
 *   2. unit+0xe0              — объект мира; R12 (`0x44c759`) и R17 (`0x44cb40`).
 *   3. [unit+0xe0]+0x44       — промежуточное звено той же цепочки.
 *   4. [[unit+0xe0]+0x44]+0x238 — grid-дерево (уходит в record+8 и ECX 458050;
 *                               дальше тело его НЕ разыменовывает).
 *   Плюс скрытая пятая: unit+0x138 — первый dword встроенного BattleAI
 *   (game_ctx); тело читает `*(u32)game_ctx` (бит3) в B43/B50.
 *
 * Каждое звено получает shadow-объект (node_e0 0x240, node_e044 0x240, tree
 * 0x40, ctx 0x20) и поле указателя ПЕРЕПИСЫВАЕТСЯ на него — так та же цепочка
 * разыменований в порте даёт записанное значение. Таблица k_fixups — данные,
 * а не разбросанный код; отсутствующие в захвате поля помечаются как
 * unmapped handle (проверка handles), а не подставляются тихо.
 *
 * Блоки frame/unit/ai/band/gs копируются из base64-снимков как есть (мусор
 * стека тоже), плюс `self` из записи связывает адреса пространства игры с
 * shadow-адресами: возвращаемый листом указатель, попавший внутрь [self,
 * self+0x1b8), переводится в unit-блок со сдвигом (band_head = self+0x174).
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"

#define ARENA_PAGE 0x1000u
#define ARENA_CANARY 64u
#define ARENA_CANARY_BYTE 0xCDu
#define ARENA_BAND_MAX_BYTES (BDD0_BAND_MAX * BDD0_BAND_ELEM)

struct Reg {
    char name[24];
    unsigned char *base;        /* данные */
    unsigned len;               /* объявленная длина */
    unsigned char *alloc;
    unsigned alloc_len;
    unsigned char *pre;         /* копия на входе тика */
};

struct Bind {
    unsigned raw;
    unsigned char *base;
    unsigned size;
};

#define ARENA_REGS_MAX 32        /* области фиксированного набора + leafN */
#define ARENA_BINDS_MAX 32

struct Arena {
    /* фиксированные массивы, а не vector: RegionView.name ссылается на
     * regs[i].name, и добавление новой области во время тика (opaque-объект
     * листа) не должно переносить имена в памяти. */
    Reg regs[ARENA_REGS_MAX];
    int reg_n;
    Bind binds[ARENA_BINDS_MAX];
    int bind_n;
    std::vector<RegionView> views;
    std::vector<RegionView> pre_views;
    unsigned char *out_slot;
    int out_known;
    int handle_miss;
    char miss_why[192];
    int leaf_nodes;
};

static Arena *g_cur = NULL;

void arena_set_current(Arena *a) { g_cur = a; }
Arena *arena_current(void) { return g_cur; }

static unsigned round_up(unsigned v, unsigned a)
{
    return (v + a - 1) & ~(a - 1);
}

static Reg *reg_find(Arena *a, const char *name)
{
    for (int i = 0; i < a->reg_n; i++) {
        if (strcmp(a->regs[i].name, name) == 0) return &a->regs[i];
    }
    return NULL;
}

/* Создаёт область: данные в конце страницы, канарейка перед ними, guard-страницы
 * с обеих сторон. Возврат 0 — VirtualAlloc не смог. */
static int reg_add(Arena *a, const char *name, unsigned len)
{
    if (len == 0 || a->reg_n >= ARENA_REGS_MAX) return 0;
    Reg r;
    memset(&r, 0, sizeof(r));
    _snprintf_s(r.name, sizeof(r.name), _TRUNCATE, "%s", name);
    unsigned body = round_up(ARENA_CANARY + len, ARENA_PAGE);
    r.alloc_len = ARENA_PAGE + body + ARENA_PAGE;
    r.alloc = (unsigned char *)VirtualAlloc(NULL, r.alloc_len,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
    if (r.alloc == NULL) return 0;
    memset(r.alloc, 0, r.alloc_len);
    memset(r.alloc + ARENA_PAGE, ARENA_CANARY_BYTE, ARENA_CANARY);
    r.base = r.alloc + ARENA_PAGE + body - len;
    r.len = len;
    r.pre = (unsigned char *)malloc(len);
    if (r.pre == NULL) {
        VirtualFree(r.alloc, 0, MEM_RELEASE);
        return 0;
    }
    memset(r.pre, 0, len);
    DWORD old;
    VirtualProtect(r.alloc, ARENA_PAGE, PAGE_NOACCESS, &old);
    VirtualProtect(r.alloc + ARENA_PAGE + body, ARENA_PAGE, PAGE_NOACCESS, &old);
    a->regs[a->reg_n++] = r;
    return 1;
}

unsigned char *arena_region_w(Arena *a, const char *name)
{
    if (a == NULL) return NULL;
    Reg *r = reg_find(a, name);
    return (r != NULL) ? r->base : NULL;
}

const unsigned char *arena_region_r(const Arena *a, const char *name)
{
    if (a == NULL) return NULL;
    for (int i = 0; i < a->reg_n; i++) {
        if (strcmp(a->regs[i].name, name) == 0) return a->regs[i].base;
    }
    return NULL;
}

const unsigned char *arena_region_pre(const Arena *a, const char *name)
{
    if (a == NULL) return NULL;
    for (int i = 0; i < a->reg_n; i++) {
        if (strcmp(a->regs[i].name, name) == 0) return a->regs[i].pre;
    }
    return NULL;
}

const RegionView *arena_regions(const Arena *a)
{
    return (a == NULL || a->views.empty()) ? NULL : &a->views[0];
}

int arena_region_n(const Arena *a)
{
    return (a == NULL) ? 0 : (int)a->views.size();
}

unsigned char *arena_out_slot(Arena *a)
{
    return (a == NULL) ? NULL : a->out_slot;
}

int arena_out_known(const Arena *a)
{
    return (a == NULL) ? 0 : a->out_known;
}

void arena_set_out_slot(Arena *a, int value)
{
    if (a != NULL && a->out_slot != NULL) memcpy(a->out_slot, &value, 4);
}

int arena_leaf_nodes(const Arena *a)
{
    return (a == NULL) ? 0 : a->leaf_nodes;
}

int arena_handle_misses(const Arena *a)
{
    return (a == NULL) ? 0 : a->handle_miss;
}

const char *arena_handle_miss_why(const Arena *a)
{
    return (a == NULL) ? "" : a->miss_why;
}

static void miss(Arena *a, const char *what, unsigned raw)
{
    if (a->handle_miss == 0) {
        _snprintf_s(a->miss_why, sizeof(a->miss_why), _TRUNCATE,
                    "unmapped handle 0x%08x at %s", raw, what);
    }
    a->handle_miss = 1;
}

static void bind_add(Arena *a, unsigned raw, unsigned char *base, unsigned size)
{
    if (raw == 0 || base == NULL || size == 0) return;
    if (a->bind_n >= ARENA_BINDS_MAX) return;
    Bind b;
    b.raw = raw;
    b.base = base;
    b.size = size;
    a->binds[a->bind_n++] = b;
}

/* Перевод указателя из пространства игры в shadow-адрес:
 *   - попадание в известную привязку → base + смещение;
 *   - иначе НИЧЕГО не подставляем: возвращается неразыменовываемый сентинел
 *     `0xDEAD0000 | idx` и регистрируется unmapped handle (проверка `handles`
 *     даст FAIL). Живой нулевой объект сделал бы такую запись «проверенной». */
#define ARENA_SENTINEL 0xDEAD0000u

void *arena_resolve_ptr(unsigned raw)
{
    Arena *a = g_cur;
    if (a == NULL || raw == 0) return NULL;
    for (int i = 0; i < a->bind_n; i++) {
        const Bind &b = a->binds[i];
        if (raw >= b.raw && raw < b.raw + b.size) {
            return b.base + (raw - b.raw);
        }
    }
    miss(a, "leaf ret (no binding)", raw);
    return (void *)(size_t)(ARENA_SENTINEL | (unsigned)(a->leaf_nodes++ & 0xfffu));
}

/* ------------------------- применение таблицы fixup ---------------------- */
enum FixTarget { FX_OUT, FX_E0, FX_E044, FX_TREE, FX_CTX };

struct PtrFixup {
    const char *what;      /* строка для отчёта */
    const char *region;    /* область, где лежит поле-указатель */
    int off;
    const char *src;       /* поле захвата с сырым значением */
    FixTarget target;
};

/* Единственный список разыменований снимка (см. блок в шапке файла). */
static const PtrFixup k_fixups[] = {
    { "frame+0x164 (param_3, out)", "frame", 0x164, "p3",     FX_OUT },
    { "unit+0xe0",                  "unit",  0xe0,  "e0",     FX_E0 },
    { "[unit+0xe0]+0x44",           "node_e0", 0x44, "e0_44", FX_E044 },
    { "[[unit+0xe0]+0x44]+0x238",   "node_e044", 0x238, "tree", FX_TREE },
    { "unit+0x138 (BattleAI.game_ctx)", "ai", 0x00, "ai_ctx", FX_CTX }
};

static unsigned read_u32(const unsigned char *p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

static void write_u32(unsigned char *p, unsigned v)
{
    memcpy(p, &v, 4);
}

Arena *arena_build(const TickRecord *rec)
{
    Arena *a = new Arena;
    a->reg_n = 0;
    a->bind_n = 0;
    a->out_slot = NULL;
    a->out_known = 0;
    a->handle_miss = 0;
    a->miss_why[0] = '\0';
    a->leaf_nodes = 0;

    /* 1. области: unit-блок 0x1b8 — объявленная область unit 0x180 плюс
     *    встроенный BattleAI (unit+0x138..+0x1b7, отдельная view "ai"). */
    int ok = 1;
    ok &= reg_add(a, "frame", BDD0_FRAME_LEN);
    ok &= reg_add(a, "unit", BDD0_UNIT_LEN + 0x38);   /* 0x1b8: 0x180 + хвост BattleAI */
    ok &= reg_add(a, "gs", BDD0_GS_LEN);
    ok &= reg_add(a, "ctx", 0x20);
    ok &= reg_add(a, "player", 0x10);   /* shadow player_get: not modelled */
    ok &= reg_add(a, "out", 4);
    ok &= reg_add(a, "node_e0", 0x240);
    ok &= reg_add(a, "node_e044", 0x240);
    ok &= reg_add(a, "tree", 0x40);
    unsigned band_len = (rec->band_n > 0)
        ? (unsigned)rec->band_n * BDD0_BAND_ELEM : 0;
    if (band_len > ARENA_BAND_MAX_BYTES) band_len = ARENA_BAND_MAX_BYTES;
    if (band_len == 0) band_len = BDD0_BAND_ELEM;      /* место под один элемент */
    ok &= reg_add(a, "band", band_len);
    if (!ok) {
        arena_release(a);
        return NULL;
    }

    /* 2. блоки снимка */
    if (rec->has_frame) memcpy(arena_region_w(a, "frame"), rec->frame, BDD0_FRAME_LEN);
    if (rec->has_unit) memcpy(arena_region_w(a, "unit"), rec->unit, BDD0_UNIT_LEN);
    if (rec->has_ai) {
        memcpy(arena_region_w(a, "unit") + BDD0_AI_OFF, rec->ai, BDD0_AI_LEN);
    }
    if (rec->has_gs) memcpy(arena_region_w(a, "gs"), rec->gs, BDD0_GS_LEN);
    if (rec->band_n > 0) {
        memcpy(arena_region_w(a, "band"), rec->band,
               (unsigned)rec->band_n * BDD0_BAND_ELEM);
    }

    a->out_slot = arena_region_w(a, "out");

    /* 3. привязки сырых адресов: self покрывает весь unit-блок (band_head
     *    вернёт self+0x174 = &BattleAI[0x3c]). */
    bind_add(a, (unsigned)rec->self_raw, arena_region_w(a, "unit"),
             BDD0_UNIT_LEN + BDD0_AI_LEN);
    bind_add(a, rec->band_raw, arena_region_w(a, "band"), band_len);
    bind_add(a, rec->e0, arena_region_w(a, "node_e0"), 0x240);
    bind_add(a, rec->e0_44, arena_region_w(a, "node_e044"), 0x240);
    bind_add(a, rec->tree, arena_region_w(a, "tree"), 0x40);

    /* 4. таблица переписывания указателей */
    for (int i = 0; i < (int)(sizeof(k_fixups) / sizeof(k_fixups[0])); i++) {
        const PtrFixup &fx = k_fixups[i];
        Reg *r = reg_find(a, fx.region);
        if (r == NULL) continue;
        unsigned char *slot = r->base + fx.off;

        if (fx.target == FX_OUT) {
            /* param_3 — аргумент, а не объект: NULL остаётся NULL (натив не
             * пишет out), ненулевое значение переписывается на слот "out". */
            if (rec->p3 != 0) {
                write_u32(slot, (unsigned)(size_t)a->out_slot);
                a->out_known = 1;
            } else {
                write_u32(slot, 0);
            }
            continue;
        }
        if (fx.target == FX_CTX) {
            /* game_ctx: NULL guard в теле (B43/B50) — 0 остаётся 0. */
            unsigned raw = read_u32(slot);
            if (raw != 0) {
                unsigned char *ctx = arena_region_w(a, "ctx");
                write_u32(slot, (unsigned)(size_t)ctx);
                if (rec->has_gctx0) write_u32(ctx, rec->gctx0);
            } else {
                write_u32(slot, 0);
            }
            continue;
        }
        /* цепочка unit+0xe0: без записи в захвате поле не материализуем —
         * тело упрётся в guard-страницу/мусор, а handles назовёт причину. */
        if (!rec->has_chain) {
            miss(a, fx.what, read_u32(slot));
            continue;
        }
        const char *dst = (fx.target == FX_E0) ? "node_e0"
                        : (fx.target == FX_E044) ? "node_e044" : "tree";
        unsigned char *o = arena_region_w(a, dst);
        /* сырое значение — из ЗАПИСИ захвата (слот shadow-объекта ещё пуст) */
        unsigned raw = (fx.target == FX_E0) ? rec->e0
                     : (fx.target == FX_E044) ? rec->e0_44 : rec->tree;
        if (o == NULL || raw == 0) {
            /* нулевое звено цепочки в нативе невозможно (тело разыменовало бы
             * NULL): shadow не создаём, 0 остаётся 0, handles даёт причину. */
            write_u32(slot, 0);
            miss(a, fx.what, raw);
            continue;
        }
        write_u32(slot, (unsigned)(size_t)o);
    }
    /* вторая deref той же цепочки: [node_e0+0x44] — тот же shadow-объект */
    if (rec->has_chain && rec->e0 != 0 && rec->e0_44 != 0) {
        write_u32(arena_region_w(a, "node_e0") + 0x44,
                  (unsigned)(size_t)arena_region_w(a, "node_e044"));
    }

    /* 5. views + копия входа (pre-state) */
    /* out-слот стартует с сентинела, чтобы «не писано» отличалось от «= 0» */
    arena_set_out_slot(a, (int)0x7fffffffu);
    for (int i = 0; i < a->reg_n; i++) {
        Reg &r = a->regs[i];
        memcpy(r.pre, r.base, r.len);
        RegionView v;
        v.name = r.name;
        v.base = r.base;
        v.len = r.len;
        a->views.push_back(v);
        v.base = r.pre;
        a->pre_views.push_back(v);
    }
    /* "ai" — view встроенного BattleAI (unit+0x138, 0x80 Б) */
    RegionView ai;
    ai.name = "ai";
    ai.base = arena_region_w(a, "unit") + BDD0_AI_OFF;
    ai.len = BDD0_AI_LEN;
    a->views.push_back(ai);
    RegionView ai_pre = ai;
    ai_pre.base = arena_region_pre(a, "unit") + BDD0_AI_OFF;
    a->pre_views.push_back(ai_pre);
    return a;
}

void arena_release(Arena *a)
{
    if (a == NULL) return;
    for (int i = 0; i < a->reg_n; i++) {
        if (a->regs[i].alloc != NULL) {
            VirtualFree(a->regs[i].alloc, 0, MEM_RELEASE);
        }
        free(a->regs[i].pre);
    }
    if (g_cur == a) g_cur = NULL;
    delete a;
}

const RegionView *arena_views(const Arena *a);
const RegionView *arena_views(const Arena *a)
{
    return (a == NULL || a->views.empty()) ? NULL : &a->views[0];
}
const RegionView *arena_pre_views(const Arena *a);
const RegionView *arena_pre_views(const Arena *a)
{
    return (a == NULL || a->pre_views.empty()) ? NULL : &a->pre_views[0];
}

/* `region+0xNN` для адреса сбоя: сначала точное попадание в данные, затем —
 * guard-страница перед/после области (смещение может быть отрицательным или
 * выходить за объявленную длину). */
const char *arena_why_addr(const Arena *a, unsigned long addr, char *buf, int cap)
{
    if (a == NULL || buf == NULL || cap <= 0) return NULL;
    for (int i = 0; i < a->reg_n; i++) {
        const Reg &r = a->regs[i];
        if (addr >= (unsigned long)r.base && addr < (unsigned long)r.base + r.len) {
            _snprintf_s(buf, cap, _TRUNCATE, "%s+0x%x", r.name,
                        (unsigned)(addr - (unsigned long)r.base));
            return buf;
        }
    }
    for (int i = 0; i < a->reg_n; i++) {
        const Reg &r = a->regs[i];
        unsigned long lo = (unsigned long)r.base;
        unsigned long hi = (unsigned long)r.alloc + r.alloc_len;
        if (addr >= lo - ARENA_CANARY && addr < hi) {
            long off = (long)(addr - lo);
            if (off >= 0) {
                _snprintf_s(buf, cap, _TRUNCATE, "%s+0x%x (len 0x%x)",
                            r.name, (unsigned)off, r.len);
            } else {
                _snprintf_s(buf, cap, _TRUNCATE, "%s-0x%x (before region)",
                            r.name, (unsigned)(-off));
            }
            return buf;
        }
    }
    _snprintf_s(buf, cap, _TRUNCATE, "0x%08x (outside all declared regions)",
                (unsigned)addr);
    return buf;
}

/* Адрес + len целиком внутри одной объявленной области? Нужно нативному слою,
 * чтобы модельные чтения не уходили в неизвестную память. */
int arena_addr_ok(const void *p, unsigned len)
{
    Arena *a = g_cur;
    if (a == NULL || p == NULL) return 0;
    unsigned long lo = (unsigned long)p;
    for (int i = 0; i < a->reg_n; i++) {
        const Reg &r = a->regs[i];
        unsigned long b = (unsigned long)r.base;
        if (lo >= b && lo + len <= b + r.len) return 1;
    }
    return 0;
}

/* Изменённые канарейки = запись до начала области (запись за конец ловит
 * guard-страница). Возврат 1 при нарушении. */
int arena_scan_writes(Arena *a, char *why, int cap)
{
    if (a == NULL) return 0;
    for (int i = 0; i < a->reg_n; i++) {
        const Reg &r = a->regs[i];
        const unsigned char *c = r.alloc + ARENA_PAGE;
        for (unsigned k = 0; k < ARENA_CANARY; k++) {
            if (c[k] != ARENA_CANARY_BYTE) {
                _snprintf_s(why, cap, _TRUNCATE,
                            "write before region '%s' (canary byte %u = 0x%02x)",
                            r.name, k, c[k]);
                return 1;
            }
        }
    }
    return 0;
}
