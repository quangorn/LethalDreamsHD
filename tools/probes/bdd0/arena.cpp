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
 *   Плюс шестая (лист 45c6f0): `[*(unit+0x138)+0xc]` — указатель на pos-тройку
 *   внутри объекта ctx. Сам объект лежит в куче игры и в снимок не попадает,
 *   поэтому область "ctx_pos" (12 Б) заполняется независимым снимком
 *   `[*(unit+0x138)+0xc]` (bdd0_in "ctx_pos", v15), а у файлов без ключа —
 *   из потока (w записи 45c6f0, native_replay.cpp bdd0_leaf_begin); ctx+0xc
 *   указывает на неё.
 *
 * Каждое звено получает shadow-объект (node_e0 0x240, node_e044 0x240, tree
 * 0x40, ctx 0x20) и поле указателя ПЕРЕПИСЫВАЕТСЯ на него; сырой адрес звена
 * ПРИВЯЗЫВАЕТСЯ к своему shadow-объекту (binds), чтобы он не считался
 * неразвязанным. Таблица k_fixups — данные, а не разбросанный код; поле лежит
 * в области, где его реально читает тело (`ai` — view на unit+0x138, отдельной
 * области "ai" нет), отсутствующие в захвате поля помечаются как unmapped
 * handle (проверка handles), а не подставляются тихо.
 *
 * Блоки frame/unit/ai/band/gs копируются из base64-снимков как есть (мусор
 * стека тоже), плюс `self` из записи связывает адреса пространства игры с
 * shadow-адресами: возвращаемый листом указатель, попавший внутрь [self,
 * self+0x1b8), переводится в unit-блок со сдвигом (band_head = self+0x174).
 *
 * --------------------- идентичность неразвязанных указателей --------------
 * Указатели вне материализованных объектов (листьевые возвраты, поля снимка,
 * возвращённые нативами) получают IDENTITY-CELL: один незарезервированный
 * адрес на каждый сырой адрес, привязанный memoизацией по сырому значению.
 * Равные сырые адреса дают одну и ту же ячейку, поэтому сравнения
 * `leaf_ret == снимкового dword` (`cmp 0x14(edi),eax` @0x44c3ff, курсор ленты)
 * воспроизводятся достоверно. Разыменование ячейки бьёт (страница зарезервана
 * без коммита) и приписывается `handles`. Подстановки содержимого нет: тело,
 * прочитавшее dword из такой ячейки, получает access violation, а не мусор.
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "../../ldhook/battle/structs.h"

#define ARENA_PAGE 0x1000u
#define ARENA_CANARY 64u
#define ARENA_CANARY_BYTE 0xCDu
/* +2 элемента запаса: 45b3e0 читает LDPos* на elem[count]+0x10 (floor), при
 * band_n == BDD0_BAND_MAX это индекс BDD0_BAND_MAX — без запаса читалось бы за
 * концом области. Ёмкость = (BDD0_BAND_MAX + 2) элементов, floor материализуется. */
#define ARENA_BAND_MAX_BYTES ((BDD0_BAND_MAX + 2) * BDD0_BAND_ELEM)
/* Неразыменовываемый сентинел указателя из пространства игры (см.
 * arena_resolve_ptr_at ниже): весь диапазон сентинелов лежит в одной странице
 * 0xDEAD0000..0xDEAD0FFF, поэтому разыменование сентинела ловится по старшим
 * 20 битам (arena_fault_unbound). */
#define ARENA_SENTINEL 0xDEAD0000u
/* Ячейки идентичности: резерв без коммита, по странице на сырой адрес. */
#define ARENA_ID_MAX 64
#define ARENA_ID_POOL_PAGES ARENA_ID_MAX

struct Reg {
    char name[24];
    unsigned char *base;        /* данные */
    unsigned len;               /* объявленная длина */
    unsigned char *alloc;
    unsigned alloc_len;
    unsigned char *pre;         /* копия на входе тика */
    int fixed;                  /* v16: выделена по фикс. адресу (grid_node) */
};

struct Bind {
    unsigned raw;
    unsigned char *base;
    unsigned size;
    int synth;      /* v18: raw — синтетический адрес модели (уровень сетки из
                     * rec->tree+i*0x30), а не адрес из захвата: обратный перевод
                     * shadow→raw (arena_raw_of) для него не определён */
};

struct Ident {
    unsigned raw;
    unsigned char *cell;
};

#define ARENA_REGS_MAX 48        /* области фиксированного набора + leafN */
/* 64 LDPos + базовые привязки (self/band/e0/e0_44/tree/gs/ctx/grid_node) + запас:
 * 256 покрывает band_n == BDD0_BAND_MAX (64) с большим отрывом. Переполнение
 * (bind_overflow) печатается в сводке харнесса — молчаливой деградации нет. */
#define ARENA_BINDS_MAX 256
static_assert(ARENA_BINDS_MAX >= BDD0_BAND_MAX + 16,
              "bind capacity must cover BDD0_BAND_MAX LDPos + base binds");

struct Arena {
    /* фиксированные массивы, а не vector: RegionView.name ссылается на
     * regs[i].name, и добавление новой области во время тика (opaque-объект
     * листа) не должно переносить имена в памяти. */
    Reg regs[ARENA_REGS_MAX];
    int reg_n;
    Bind binds[ARENA_BINDS_MAX];
    int bind_n;
    Ident ids[ARENA_ID_MAX];
    int id_n;
    unsigned char *id_pool;       /* резерв без коммита: разыменование бьёт */
    std::vector<RegionView> views;
    std::vector<RegionView> pre_views;
    unsigned char *out_slot;
    int out_known;
    int handle_miss;
    char miss_why[192];
    unsigned miss_raw[16];
    int miss_raw_n;
    int bind_overflow;      /* v16: bind_add отброшен из-за переполнения (диагностика) */
    const TickRecord *rec;
    const GridModel *grid;      /* v16: модель сетки (NULL — нет grid-записи) */
    int grid_node_ok;       /* v16: 0 — узел 458050 (unit+0x74) не удалось
                             * материализовать (DOM занял фикс. страницу): тик
                             * реплеит 458050, fail-closed, без trap */
    int leaf_nodes;
    int leaf_identity;      /* leaf rets bound by identity (см. k_identity_rets) */
    PathCall path;          /* v17: материализованный вход листа 0x45e1d0 */
};

static Arena *g_cur = NULL;
static const GridModel *g_grid_model = NULL;
/* v16: генератор синтетики ставит 1, чтобы во время ЗАПИСИ файла гейты
 * bdd0_grid_level_modelled/bdd0_band_geometry_modelled читали 0 (запись ведёт
 * модельный натив, не порт); арена всё равно материализует области. Харнесс
 * читает файл с этим флагом 0 → гейты 1 → порт исполняется. */
static int g_model_gates_off = 0;

void arena_set_grid_model(const GridModel *g) { g_grid_model = g; }

void arena_set_model_gates_off(int off) { g_model_gates_off = off ? 1 : 0; }

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
    r.fixed = 0;
    DWORD old;
    VirtualProtect(r.alloc, ARENA_PAGE, PAGE_NOACCESS, &old);
    VirtualProtect(r.alloc + ARENA_PAGE + body, ARENA_PAGE, PAGE_NOACCESS, &old);
    a->regs[a->reg_n++] = r;
    return 1;
}

/* v16: область по ФИКСИРОВАННОМУ адресу (grid_node = сырой a4n). Узел 458050
 * разыменовывается напрямую (node+0x238), поэтому он обязан лежать по сырому
 * адресу, а сам node остаётся сырым (arg[3] записи совпадает). Без canary/guard:
 * порт только читает узел. Возврат 0 — адрес занят. */
static int reg_add_fixed(Arena *a, const char *name, unsigned addr, unsigned len)
{
    if (len == 0 || a->reg_n >= ARENA_REGS_MAX) return 0;
    unsigned body = round_up(len, ARENA_PAGE);
    unsigned char *p = (unsigned char *)VirtualAlloc((void *)(size_t)addr, body,
                          MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL) return 0;
    memset(p, 0, body);
    Reg r;
    memset(&r, 0, sizeof(r));
    _snprintf_s(r.name, sizeof(r.name), _TRUNCATE, "%s", name);
    r.base = p;
    r.len = len;
    r.alloc = p;
    r.alloc_len = body;
    r.fixed = 1;
    r.pre = (unsigned char *)malloc(len);
    if (r.pre == NULL) { VirtualFree(p, 0, MEM_RELEASE); return 0; }
    memset(r.pre, 0, len);
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

const TickRecord *arena_record(const Arena *a)
{
    return (a == NULL) ? NULL : a->rec;
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

static void bind_add(Arena *a, unsigned raw, unsigned char *base, unsigned size);

const PathCall *arena_path_call(const Arena *a)
{
    if (a == NULL || !a->path.ok) return NULL;
    return &a->path;
}

/* v17: привязать записанный сырой адрес к занулённому блоку (size). Сначала
 * reg_add_fixed по адресу, при коллизии — shadow + bind_add (identity). NULL —
 * не удалось. Использует replay-слой для аллокаций листа (4cfa11/45c2e0) и для
 * PathState-кандидатов скана (v18). name обязан быть уникальным на арену: блок
 * возвращается напрямую (arena_region_w по неуникальному имени отдал бы первый). */
void *arena_bind_alloc(const char *name, unsigned raw, unsigned size)
{
    Arena *a = g_cur;
    unsigned char *p;
    if (a == NULL || raw == 0 || size == 0 || name == NULL) return NULL;
    if (reg_add_fixed(a, name, raw, size)) {
        p = a->regs[a->reg_n - 1].base;
    } else {
        if (!reg_add(a, name, size)) return NULL;
        p = a->regs[a->reg_n - 1].base;
    }
    memset(p, 0, size);
    bind_add(a, raw, p, size);
    return p;
}

/* v18: блок арены по имени — создаётся при первом обращении, затем переиспользуется
 * с привязкой нового сырого адреса (PathState-кандидаты скана: объекты разные,
 * живой нужен только текущий, а число областей арены ограничено). */
void *arena_bind_reuse(const char *name, unsigned raw, unsigned size)
{
    Arena *a = g_cur;
    unsigned char *p;

    if (a == NULL || raw == 0 || size == 0 || name == NULL) return NULL;
    p = arena_region_w(a, name);
    if (p == NULL) return arena_bind_alloc(name, raw, size);
    memset(p, 0, size);
    bind_add(a, raw, p, size);
    return p;
}

int arena_leaf_identity(const Arena *a)
{
    return (a == NULL) ? 0 : a->leaf_identity;
}

int arena_handle_misses(const Arena *a)
{
    return (a == NULL) ? 0 : a->handle_miss;
}

int arena_bind_overflow(const Arena *a)
{
    return (a == NULL) ? 0 : a->bind_overflow;
}

/* v17: перевод shadow-указателя обратно в сырой адрес игры (по bind-карте).
 * 0 — указатель не из арены. Для символьного сравнения pf-полей. */
unsigned arena_raw_of(const void *p)
{
    Arena *a = g_cur;
    unsigned long v = (unsigned long)p;
    if (a == NULL || p == NULL) return 0;
    for (int i = 0; i < a->bind_n; i++) {
        const Bind &b = a->binds[i];
        if (b.synth) continue;      /* модель: raw не адрес игры */
        if (v >= (unsigned long)b.base && v < (unsigned long)b.base + b.size)
            return b.raw + (unsigned)(v - (unsigned long)b.base);
    }
    return 0;
}

const char *arena_handle_miss_why(const Arena *a)
{
    return (a == NULL) ? "" : a->miss_why;
}

/* Trap attribution: 1 iff `addr` is a pointer the materialisation did not bind.
 * Bound if it lies in any raw binding range; unbound if it is an opaque sentinel
 * (`0xDEAD…`, dereferenceable never), equals a recorded miss, or is a raw pointer
 * dword present in the input snapshot.  Sentinels bound for identity are in this
 * list too: identity binds the pointer's `!= NULL`/store/pass-through use, never
 * its contents — dereference must stay loud. */
int arena_fault_unbound(Arena *a, unsigned long addr)
{
    int i;
    const TickRecord *r;
    unsigned v = (unsigned)addr;

    if (a == NULL || v == 0) return 0;
    if ((v & 0xfffff000u) == ARENA_SENTINEL) return 1;
    if (a->id_pool != NULL && v >= (unsigned)(size_t)a->id_pool
        && v < (unsigned)(size_t)a->id_pool + ARENA_ID_POOL_PAGES * ARENA_PAGE) {
        return 1;   /* yacheika identichnosti: razymenovanie belly */
    }
    for (i = 0; i < a->bind_n; i++) {
        if (v >= a->binds[i].raw && v < a->binds[i].raw + a->binds[i].size) {
            return 0;
        }
    }
    for (i = 0; i < a->miss_raw_n; i++) {
        if (v == a->miss_raw[i]) return 1;
    }
    r = a->rec;
    if (r == NULL) return 0;
    {
        const struct { const unsigned char *p; int n; } blk[] = {
            { r->unit, BDD0_UNIT_LEN },
            { r->frame, BDD0_FRAME_LEN },
            { r->ai, BDD0_AI_LEN },
            { r->gs, BDD0_GS_LEN },
            { r->band, r->band_n * BDD0_BAND_ELEM }
        };
        for (int b = 0; b < (int)(sizeof(blk) / sizeof(blk[0])); b++) {
            for (i = 0; i + 4 <= blk[b].n; i += 4) {
                unsigned d;
                memcpy(&d, blk[b].p + i, 4);
                if (d == v) return 1;
            }
        }
    }
    return 0;
}

static void raw_note(Arena *a, unsigned raw)
{
    if (a->miss_raw_n < (int)(sizeof(a->miss_raw) / sizeof(a->miss_raw[0]))) {
        a->miss_raw[a->miss_raw_n++] = raw;
    }
}

static void miss(Arena *a, const char *what, unsigned raw)
{
    raw_note(a, raw);
    if (a->handle_miss == 0) {
        _snprintf_s(a->miss_why, sizeof(a->miss_why), _TRUNCATE,
                    "unmapped handle 0x%08x at %s", raw, what);
    }
    a->handle_miss = 1;
}

static void bind_add_ex(Arena *a, unsigned raw, unsigned char *base,
                        unsigned size, int synth)
{
    if (raw == 0 || base == NULL || size == 0) return;
    if (a->bind_n >= ARENA_BINDS_MAX) { a->bind_overflow++; return; }
    Bind b;
    b.raw = raw;
    b.base = base;
    b.size = size;
    b.synth = synth;
    a->binds[a->bind_n++] = b;
}

static void bind_add(Arena *a, unsigned raw, unsigned char *base, unsigned size)
{
    bind_add_ex(a, raw, base, size, 0);
}

/* ------------------------- применение таблицы fixup ---------------------- */
enum FixTarget { FX_OUT, FX_E0, FX_E044, FX_TREE, FX_CTX, FX_IDCELL };

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
    { "unit+0x138 (BattleAI.game_ctx)", "unit", BDD0_AI_OFF, "ai_ctx", FX_CTX },
    /* gs+0x14 — курсор ленты: тело сравнивает его (`cmp 0x14(edi),eax`
     * @0x44c3ff) с возвратом листа 0x45c730 @0x44c3fa и НЕ разыменовывает;
     * привязка по идентичности делает сравнение достоверным. */
    { "gs+0x14 (band cursor)", "gs", 0x14, "gs+0x14", FX_IDCELL }
};

/* ------------------- привязка leaf-ret по идентичности ------------------ *
 * Указатель, возвращённый листом/нативом, привязывается НЕ к объекту, а по
 * идентификатору: он получает неразыменовываемый сентинел, а `handles` считает
 * его привязанным. Правомерно ровно когда потребительский набор сайта =
 * NULL-тест + запись в write-only слот + pass-through в буферы нативов,
 * replay-уемых из захвата (порт не читает ни один dword объекта).
 * РАЗЫМЕНЕНИЕ ТАКОГО СЕНТИНЕЛА ПО-ПРЕЖНЕМУ БЬЁТ: trap при 0xDEAD…
 * приписывается `handles` (arena_fault_unbound) и проверка даёт FAIL, поэтому
 * политика не способна тихо изменить решение.
 * ЭТО ТОЧКА РАСШИРЕНИЯ: новую пару добавляют только с тем же доказательством по
 * сайту; пары «на всякий случай» не пишутся. */
struct IdentRet {
    unsigned site;   /* VA вызова в теле */
    unsigned va;     /* VA целевого натива */
};

static const IdentRet k_identity_rets[] = {
    /* 0x44c7da → 0x458050 (grid_anchor): `CMP EAX,EDI` @0x44c7df (NULL-тест),
     * запись в [ESP+0x98] @0x44c7e1 (слот write-only: 2 записи, 0 чтений во
     * всём теле), и &frame+0x88 → нативы 45e1d0/4040f0 (replay из захвата). */
    { 0x44c7dau, 0x458050u },
    /* 0x44c3fa → 0x45c730 (band_cursor): возврат → `MOV EAX,EDI` @0x44c3f8 и
     * только два равенства: `CMP 0x14(EDI),EAX` @0x44c3ff (gs-поле курсора) и
     * `CMP EDI,EAX` @0x44c7df (EDI живёт до join B77). Разыменования нет. */
    { 0x44c3fau, 0x45c730u },
    /* 0x44c36f → 0x45c730 (band_cursor, B47): возврат читает ТОЛЬКО `TEST EAX,EAX`
     * @0x44c374 / `JE` @0x44c376 (NULL-тест курсора), разыменования нет; порт —
     * `head = native::band_cursor(...); if (head == NULL) goto b75` (tick.cpp). */
    { 0x44c36fu, 0x45c730u },
    /* 0x44c37e → 0x45c730 (band_cursor, B48): возврат идёт в `native::player_get`
     * (сайт 0x44c387, слот u+0x40) как получатель (this). Живой `MOV EDX,[EAX]` /
     * `CALL *[EDX+0x40]` (vtable+0x40) офлайн НЕ моделируется — порт заменяет его
     * прямым вызовом, а replay player_get (native_replay.cpp) отвечает из
     * NativeRec / shadow-области "player" и аргумент НЕ разыменовывает. */
    { 0x44c37eu, 0x45c730u }
};

static int identity_hit(unsigned site, unsigned va)
{
    for (int i = 0;
         i < (int)(sizeof(k_identity_rets) / sizeof(k_identity_rets[0])); i++) {
        if (site == k_identity_rets[i].site && va == k_identity_rets[i].va) {
            return 1;
        }
    }
    return 0;
}

/* Ячейка идентичности для сырого адреса: один и тот же сырой адрес → одна и та
 * же ячейка, разыменование бьёт (резерв без коммита). Возврат NULL — пул кончен
 * (ячейка не заведена, счётчик leaf_nodes не растёт). */
static unsigned char *arena_idcell(Arena *a, unsigned raw)
{
    int i;
    if (raw == 0) return NULL;
    for (i = 0; i < a->id_n; i++) {
        if (a->ids[i].raw == raw) return a->ids[i].cell;
    }
    if (a->id_pool == NULL || a->id_n >= ARENA_ID_MAX) return NULL;
    unsigned char *cell = a->id_pool + (unsigned)a->id_n * ARENA_PAGE;
    a->ids[a->id_n].raw = raw;
    a->ids[a->id_n].cell = cell;
    a->id_n++;
    a->leaf_nodes++;
    raw_note(a, raw);
    return cell;
}

/* Перевод указателя из пространства игры в shadow-адрес:
 *   - попадание в известную привязку → base + смещение;
 *   - иначе НИЧЕГО не подставляем: неразыменовываемый сентинел
 *     `0xDEAD0000 | idx`. Пары из k_identity_rets: сентинел привязан по
 *     идентичности (handles pass, leaf_identity++), сырой адрес всё равно
 *     записан как unbound. Иначе — unmapped handle (handles FAIL). Живой
 *     нулевой объект сделал бы такую запись «проверенной». */
void *arena_resolve_ptr_at(unsigned raw, unsigned site, unsigned va)
{
    Arena *a = g_cur;
    if (a == NULL || raw == 0) return NULL;
    for (int i = 0; i < a->bind_n; i++) {
        const Bind &b = a->binds[i];
        if (raw >= b.raw && raw < b.raw + b.size) {
            return b.base + (raw - b.raw);
        }
    }
    unsigned char *cell = arena_idcell(a, raw);
    if (identity_hit(site, va)) {
        a->leaf_identity++;    /* привязан по идентичности: handles не FAIL */
        return (void *)(size_t)(cell != NULL ? cell
                        : (unsigned char *)(size_t)(ARENA_SENTINEL |
                          (unsigned)(a->leaf_nodes & 0xfffu)));
    }
    miss(a, "leaf ret (no binding)", raw);
    return (void *)(size_t)(cell != NULL ? cell
                    : (unsigned char *)(size_t)(ARENA_SENTINEL |
                      (unsigned)(a->leaf_nodes & 0xfffu)));
}

void *arena_resolve_ptr(unsigned raw)
{
    /* сайт неизвестен — привязка по идентичности неприменима */
    return arena_resolve_ptr_at(raw, 0u, 0u);
}

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

/* v16: материализация модели сетки (grid record) и геометрии ленты (band_pos).
 * Создаёт shadow-области, заполняет их из модели, переписывает указатели
 * снимка (band[i].pos, tree.next_level/cells/rows) на shadow и привязывает
 * сырые адреса (bind_add). Без grid/band_pos (захваты v12..v15) ничего не
 * делает — регресс сохраняется. */
static void materialize_models(Arena *a)
{
    const TickRecord *rec = a->rec;
    const GridModel *gm = a->grid;
    unsigned char *tree = arena_region_w(a, "tree");
    unsigned char *band = arena_region_w(a, "band");
    int grid_ok = (gm != NULL && gm->valid && gm->nlev >= 1 &&
                   gm->leaf >= 0 && gm->leaf < gm->nlev && tree != NULL);
    /* Инвариант: tree/grid_levels у элементов ленты берём ТОЛЬКО если уровни
     * сетки реально записаны — тот же предикат, что у grid-блока ниже.
     * grid_node_ok==0 → узел 458050 реплеит натив, уровни не заполнены
     * (триплет 0), поэтому лента использует собственные band_level-триплеты. */
    int grid_levels_done = grid_ok && a->grid_node_ok;
    int lsx = grid_ok ? gm->lev[gm->leaf].sx : 0;
    int lsy = grid_ok ? gm->lev[gm->leaf].sy : 0;
    int ncell = grid_ok ? gm->ncell : 0;

    /* grid_node_ok=0 (узел не материализуем) → 458050 реплеит натив, grid-области
     * не нужны: не тратим 8-МБ alloc/memset на тик (см. bdd0_grid_level_modelled). */
    if (grid_levels_done) {
        unsigned char *glevels = NULL, *gcells = NULL, *gkeys = NULL;
        unsigned char *grows = NULL, *growdata = NULL;
        int i;

        if (gm->nlev > 1) {
            reg_add(a, "grid_levels", (unsigned)(gm->nlev - 1) * 0x40);
            glevels = arena_region_w(a, "grid_levels");
        }
        reg_add(a, "grid_rows", (unsigned)lsx * 4);
        grows = arena_region_w(a, "grid_rows");
        reg_add(a, "grid_rowdata", (unsigned)ncell * 4);
        growdata = arena_region_w(a, "grid_rowdata");
        reg_add(a, "grid_cells", (unsigned)ncell * S6_GRID_CELL_SIZE);
        gcells = arena_region_w(a, "grid_cells");
        reg_add(a, "grid_keys", (unsigned)ncell * 0x20);
        gkeys = arena_region_w(a, "grid_keys");
        /* grid_node (fixed по unit+0x74) зарезервирован в arena_build ПЕРВЫМ
         * (до OS-chosen областей); здесь только ячейки/ключи/уровни/rows.
         * Материализуем per-tick: статические области содержат указатели на
         * shadow-адреса (gkeys/gcells), которые меняются от тика к тику, а
         * кэш 8-МБ committed-памяти (OS-chosen) конфликтует с per-tick фикс.
         * grid_node (куча игры) на unpredict. адресах. ~4 с на cap_v16 (971
         * тик × 90000 ячеек) — приемлемо для офлайн-инструмента. */
        /* ячейки + ключи */
        for (i = 0; i < ncell; i++) {
            unsigned char *c = gcells + i * S6_GRID_CELL_SIZE;
            unsigned char *kr = gkeys + i * 0x20;
            write_u32(kr + 0x00, (unsigned)gm->key[i]);   /* key_rec->key */
            write_u32(kr + 0x18, 0);                       /* value */
            write_u32(kr + 0x1c, 0);                       /* next */
            write_u32(c + 0x14, (unsigned)gm->minm[i]);   /* min_metric */
            write_u32(c + 0x20, (unsigned)(size_t)kr);    /* key_rec */
            write_u32(c + 0x24, (gm->nextc[i] >= 0 && gm->nextc[i] < ncell)
                                 ? (unsigned)(size_t)(gcells + gm->nextc[i] * S6_GRID_CELL_SIZE) : 0);
            write_u32(c + 0x28, 0);                       /* chain_next */
        }
        /* rows[x][y] = индекс */
        for (i = 0; i < ncell; i++) write_u32(growdata + i * 4, (unsigned)gm->rows[i]);
        for (i = 0; i < lsx; i++)
            write_u32(grows + i * 4, (unsigned)(size_t)(growdata + i * lsy * 4));
        /* уровни: 0 = tree (root), i>0 = grid_levels+(i-1)*0x40 */
        for (i = 0; i < gm->nlev; i++) {
            unsigned char *L = (i == 0) ? tree : glevels + (i - 1) * 0x40;
            const GridLevelModel &m = gm->lev[i];
            unsigned char *cells = gcells;   /* все уровни → один массив ячеек */
            write_u32(L + 0x0c, (unsigned)m.rx);
            write_u32(L + 0x10, (unsigned)m.ry);
            write_u32(L + 0x1c, (i + 1 < gm->nlev)
                         ? (unsigned)(size_t)(glevels + i * 0x40) : 0); /* next_level */
            write_u32(L + 0x20, m.sa);
            write_u32(L + 0x24, m.sl);
            write_u32(L + 0x28, m.sm);
            write_u32(L + 0x2c, (unsigned)m.sx);   /* размеры СВОЕГО уровня (не листа) */
            write_u32(L + 0x30, (unsigned)m.sy);
            write_u32(L + 0x34, (unsigned)(size_t)cells);  /* cells */
            write_u32(L + 0x38, (unsigned)(size_t)grows);   /* rows */
            if (rec->tree != 0) bind_add_ex(a, rec->tree + i * 0x30, L, 0x40, 1);
        }
        /* v18: идентичность массива ячеек — по нему порт листа 0x45e1d0 отдаёт
         * pf+0x28/+0x2c (GridCell*), и path_post_match переводит shadow-указатель
         * в сырой адрес записи. Без привязки такие поля не сравнимы. */
        if (rec->grid_cells_raw != 0 && gcells != NULL)
            bind_add(a, rec->grid_cells_raw, gcells,
                     (unsigned)ncell * S6_GRID_CELL_SIZE);
    }

    if (rec->has_band_pos && rec->band_pos_n > 0 && band != NULL) {
        int n = rec->band_pos_n;
        unsigned char *ldpos = NULL, *blevel = NULL;
        int i;

        /* вектор ленты (BattleAI+0x3c): begin/end — сырые адреса игры; порт
         * 45b150/45b3e0 индексирует их как указатели, поэтому при модели
         * геометрии переписываем begin/end на shadow-базу band (count/head
         * читают их как значения — разность/нуль сохраняются). */
        {
            unsigned char *unit = arena_region_w(a, "unit");
            unsigned char *bv = (unit != NULL) ? unit + BDD0_AI_OFF + 0x3c : NULL;
            if (bv != NULL) {
                unsigned begin_raw = 0, end_raw = 0;
                memcpy(&begin_raw, bv + 0x20, 4);
                memcpy(&end_raw, bv + 0x24, 4);
                if (begin_raw != 0 && rec->band_raw != 0 &&
                    begin_raw >= rec->band_raw) {
                    unsigned boff = begin_raw - rec->band_raw;
                    unsigned eoff = end_raw - begin_raw;
                    write_u32(bv + 0x20, (unsigned)(size_t)band + boff);
                    write_u32(bv + 0x24, (unsigned)(size_t)band + boff + eoff);
                }
            }
        }

        reg_add(a, "band_ldpos", (unsigned)n * 0x20);
        ldpos = arena_region_w(a, "band_ldpos");
        if (!grid_levels_done) {
            reg_add(a, "band_level", (unsigned)n * 0x40);
            blevel = arena_region_w(a, "band_level");
        }
        for (i = 0; i < n; i++) {
            unsigned char *elem = band + i * BDD0_BAND_ELEM;
            unsigned raw_pos;
            unsigned char *P;
            unsigned char *L;
            if (!rec->band_pos_ok[i]) continue;
            memcpy(&raw_pos, elem + 0x14, 4);
            if (raw_pos == 0) continue;
            P = ldpos + i * 0x20;
            /* уровень: общий с моделью сетки (i % nlev) либо свой */
            if (grid_levels_done) {
                int li = i % gm->nlev;
                L = (li == 0) ? tree : arena_region_w(a, "grid_levels") + (li - 1) * 0x40;
            } else {
                L = blevel + i * 0x40;
                write_u32(L + 0x20, rec->band_pos_sa[i]);
                write_u32(L + 0x24, rec->band_pos_sl[i]);
                write_u32(L + 0x28, rec->band_pos_sm[i]);
            }
            write_u32(P + 0x0c, (unsigned)rec->band_pos_x[i]);
            write_u32(P + 0x10, (unsigned)rec->band_pos_y[i]);
            write_u32(P + 0x1c, (unsigned)(size_t)L);      /* pos->level */
            write_u32(elem + 0x14, (unsigned)(size_t)P);   /* band[i].pos → shadow */
            bind_add(a, raw_pos, P, 0x20);
        }
        /* v16: 45b3e0 читает pos на begin+count*0x18+0x10 (elem[count]+0x10) —
         * кладём туда валидный LDPos (нулевой элемент), чтобы scale_compute не
         * разыменовал мусор. Запас в 2 элемента (ARENA_BAND_MAX_BYTES) гарантирует
         * попадание в область даже при band_n == BDD0_BAND_MAX (floor = индекс 64). */
        write_u32(band + n * BDD0_BAND_ELEM + 0x10, (unsigned)(size_t)ldpos);
        write_u32(band + n * BDD0_BAND_ELEM + 0x14, (unsigned)(size_t)ldpos);
    }
}

/* v17: регион для сырого адреса записи. Сначала reg_add_fixed по записанному
 * адресу (правило grid_node); при коллизии — shadow-регион + bind_add(raw→shadow):
 * identity указателя сохраняется (arena_resolve_ptr_at/cmp_ptr работают по
 * bind-карте), а порт получает читаемый объект. NULL — не удалось ни то, ни
 * другое (fail-closed: ok=0 у вызывающего). */
static unsigned char *path_alloc(Arena *a, const char *name, unsigned raw,
                                 unsigned len)
{
    unsigned char *p;
    if (raw == 0) return NULL;
    if (reg_add_fixed(a, name, raw, len)) {
        p = arena_region_w(a, name);
        bind_add(a, raw, p, len);
        return p;
    }
    if (!reg_add(a, name, len)) return NULL;
    p = arena_region_w(a, name);
    bind_add(a, raw, p, len);
    return p;
}

/* v17: материализация входа листа 0x45e1d0 из bdd0_leaf_pre (+ ctx120/ctx134 из
 * bdd0_site_pf, identity-check по ecx). Регионы — по записанным сырым адресам
 * (path_alloc: reg_add_fixed, при коллизии shadow+bind), всё резервируется ДО
 * OS-chosen областей (arena_build). При неудаче path.ok=0 (fail-closed, тело
 * порт не исполняет). pf+0x6c/+0x78 — головы карт: сентинели (0x18) и узлы
 * (pre пуст: n=0) материализуются по записанным адресам. rec/sub/ctx в pre NULL
 * (ленивая аллокация внутри листа — сайты 35/36/44): порт берёт их из
 * bdd0_nested, ctx120/ctx134 — из site_pf. */
static void materialize_path_call(Arena *a)
{
    const TickRecord *rec = a->rec;
    PathCall &pc = a->path;
    if (rec == NULL || rec->leaf_pre_idx < 0) return;
    /* order записи листа = order pre-записи (leaf_pre_idx указывает на неё в
     * Scenario::leaf_pre). Поток bdd0_leaf ещё не загружен (arena_build идёт
     * до bdd0_leaf_begin), поэтому order берём отсюда, а не из bdd0_tick_leaf_at. */
    const LeafStateRec *pre = bdd0_leaf_pre_at(rec->leaf_pre_idx);
    if (pre == NULL) return;
    int order = pre->order;
    const LeafStateRec *stp = bdd0_leaf_state_pre(rec->seq, order);
    if (stp == NULL || !stp->ok || stp->ecx == 0) return;
    const LeafStateRec &st = *stp;

    /* v17: site_pf (0x45e320) несёт ctx120/ctx134; identity — по ecx */
    {
        const LeafStateRec *spf = bdd0_leaf_state_site_pf(rec->seq, order);
        if (spf != NULL && spf->ok && spf->site == 0x45e320 &&
            spf->va == 0x45c8c0 && spf->ecx == st.ecx) {
            pc.ctx120 = spf->ctx120;
            pc.ctx134 = spf->ctx134;
        }
    }

    pc.ecx_raw = st.ecx;
    pc.nargs = st.nargs;
    for (int i = 0; i < 6; i++) pc.args[i] = st.args[i];
    pc.pf_head1 = st.h1.head;
    pc.pf_head2 = st.h2.head;
    pc.m1_n = st.m1_n;
    pc.m2_n = st.m2_n;
    for (int i = 0; i < st.m1_n; i++) pc.m1[i] = st.m1[i];
    for (int i = 0; i < st.m2_n; i++) pc.m2[i] = st.m2[i];

    /* pf: 0x78-байт блоб + головы карт на +0x6c/+0x78 (0x7c всего) */
    const unsigned pf_len = BDD0_LEAFPF_LEN + 4;
    unsigned char *pf = path_alloc(a, "path_pf", st.ecx, pf_len);
    if (pf == NULL) return;
    memset(pf, 0, pf_len);
    memcpy(pf, st.pf, BDD0_LEAFPF_LEN);
    pc.pf = pf;

    /* пред-карты: сентинели (0x18) по записанным адресам + узлы (pre пуст) */
    write_u32(pf + 0x6c, 0);
    write_u32(pf + 0x78, 0);
    if (st.h1.head != 0) {
        unsigned char *h = path_alloc(a, "path_m1_head", st.h1.head, 0x18);
        if (h == NULL) return;
        memset(h, 0, 0x18);
        write_u32(pf + 0x6c, (unsigned)(size_t)h);
    }
    if (st.h2.head != 0) {
        unsigned char *h = path_alloc(a, "path_m2_head", st.h2.head, 0x18);
        if (h == NULL) return;
        memset(h, 0, 0x18);
        write_u32(pf + 0x78, (unsigned)(size_t)h);
    }
    for (int i = 0; i < st.m1_n; i++) {
        const MapNodeRec &nd = st.m1[i];
        char nm[24];
        if (nd.addr == 0 || !nd.has_node) continue;
        _snprintf_s(nm, sizeof(nm), _TRUNCATE, "path_m1n%d", i);
        unsigned char *nb = path_alloc(a, nm, nd.addr, 0x18);
        if (nb == NULL) return;
        memcpy(nb, nd.node, 0x18);
    }
    for (int i = 0; i < st.m2_n; i++) {
        const MapNodeRec &nd = st.m2[i];
        char nm[24];
        if (nd.addr == 0 || !nd.has_node) continue;
        _snprintf_s(nm, sizeof(nm), _TRUNCATE, "path_m2n%d", i);
        unsigned char *nb = path_alloc(a, nm, nd.addr, 0x18);
        if (nb == NULL) return;
        memcpy(nb, nd.node, 0x18);
    }

    /* rec 0x1c по [pf+0x00] — в pre NULL (аллоцирует сайт 0x45e1e3) */
    if (st.rec != 0) {
        unsigned char *rc = path_alloc(a, "path_rec", st.rec, BDD0_LEAFREC_LEN);
        if (rc == NULL) return;
        if (st.has_rec) memcpy(rc, st.recb, BDD0_LEAFREC_LEN);
        write_u32(pf + 0x00, (unsigned)(size_t)rc);
        pc.rec = rc;
        /* sub 0x10 по [rec+0x0c] — в pre NULL (аллоцирует сайт 0x45e23c) */
        if (st.sub != 0) {
            unsigned char *sb = path_alloc(a, "path_sub", st.sub,
                                           BDD0_LEAFSUB_LEN);
            if (sb == NULL) return;
            if (st.has_sub) memcpy(sb, st.subb, BDD0_LEAFSUB_LEN);
            write_u32(rc + 0x0c, (unsigned)(size_t)sb);
            pc.sub = sb;
        }
        /* ctx ≥0x138 по [rec+0x04] — в pre NULL (аллоцирует сайт 0x45e340) */
        if (st.ctx != 0) {
            unsigned char *cx = path_alloc(a, "path_ctx", st.ctx,
                                           BDD0_LEAFCTX_LEN);
            if (cx == NULL) return;
            memset(cx, 0, BDD0_LEAFCTX_LEN);
            write_u32(cx + 0x120, pc.ctx120);
            write_u32(cx + 0x134, pc.ctx134);
            write_u32(rc + 0x04, (unsigned)(size_t)cx);
            pc.ctx = cx;
        }
    }
    /* bandrec 0x24 по args[1] */
    if (pc.nargs > 1 && pc.args[1] != 0) {
        unsigned char *bb = path_alloc(a, "path_band", (unsigned)pc.args[1],
                                       BDD0_LEAFBAND_LEN);
        if (bb == NULL) return;
        if (st.has_band) memcpy(bb, st.bandb, BDD0_LEAFBAND_LEN);
        pc.bandrec = bb;
    }
    pc.ok = 1;
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
    a->miss_raw_n = 0;
    a->bind_overflow = 0;
    a->rec = rec;
    a->grid = g_grid_model;
    a->grid_node_ok = 1;
    a->leaf_nodes = 0;
    a->leaf_identity = 0;
    a->id_n = 0;
    /* v16: grid_node (fixed по сырому unit+0x74 = arg4 узла 458050 в B5)
     * резервируем ПЕРВЫМ — до любых OS-chosen областей (base/id_pool/grid_cells),
     * иначе 8-МБ grid_cells или пул идентичности займут фикс. страницу узла на
     * другом тике, и reg_add_fixed провалится → node+0x238 читается из живой
     * кучи игры → trap. unit+0x74 берём из СЫРОГО снимка (rec->unit), не из арены. */
    {
        const GridModel *gm = a->grid;
        int gok = (gm != NULL && gm->valid && gm->nlev >= 1 &&
                   gm->leaf >= 0 && gm->leaf < gm->nlev);
        if (gok && rec->has_unit) {
            unsigned a4n;
            memcpy(&a4n, rec->unit + 0x74, 4);
            if (a4n != 0) {
                if (reg_add_fixed(a, "grid_node", a4n, 0x240))
                    bind_add(a, a4n, arena_region_w(a, "grid_node"), 0x240);
                else
                    a->grid_node_ok = 0;  /* фикс. страница занята (DOM): реплей */
            }
            /* a4n == 0 → узел NULL (ветка node==NULL), grid_node_ok=1 */
        }
    }

    /* v17: вход листа 0x45e1d0 (PathFinder/record/sub/ctx/bandrec) — резервируем
     * ДО OS-chosen областей (id_pool/grid_cells), иначе 8-МБ grid_cells займёт
     * фикс. страницу блока (heap-адреса игры 0x06xxxxxx) и reg_add_fixed
     * провалится. Порядок: grid_node → path-блоки → прочие области. */
    materialize_path_call(a);

    /* 1. области: unit-блок 0x1b8 — объявленная область unit 0x180 плюс
     *    встроенный BattleAI (unit+0x138..+0x1b7, отдельная view "ai"). */
    int ok = 1;
    ok &= reg_add(a, "frame", BDD0_FRAME_LEN);
    ok &= reg_add(a, "unit", BDD0_UNIT_LEN + 0x38);   /* 0x1b8: 0x180 + хвост BattleAI */
    ok &= reg_add(a, "gs", BDD0_GS_LEN);
    ok &= reg_add(a, "ctx", 0x20);
    ok &= reg_add(a, "ctx_pos", 12);    /* pos-тройка [ctx+0xc] (лист 45c6f0) */
    ok &= reg_add(a, "player", 0x10);   /* shadow player_get: not modelled */
    ok &= reg_add(a, "flags", 0x10);    /* shadow player_flag_get/player_flags */
    ok &= reg_add(a, "out", 4);
    ok &= reg_add(a, "node_e0", 0x240);
    ok &= reg_add(a, "node_e044", 0x240);
    ok &= reg_add(a, "tree", 0x40);
    unsigned band_len = (rec->band_n > 0)
        ? (unsigned)rec->band_n * BDD0_BAND_ELEM : 0;
    /* v16: 45b3e0 (target_moved) читает pos-указатель на elem[count-1]+0x28
     * (en1-4+0x14 = begin+count*0x18+0x10) — за пределами count элементов.
     * Даём запас в 2 элемента, чтобы чтение попало в материализованную память. */
    if (rec->has_band_pos && band_len > 0) band_len += 2 * BDD0_BAND_ELEM;
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

    /* пул идентичности — резерв без коммита: адрес валиден, разыменование бьёт
     * ACCESS_VIOLATION. Резервируется ПОСЛЕ grid_node (тот взят первым). */
    a->id_pool = (unsigned char *)VirtualAlloc(NULL,
                                               ARENA_ID_POOL_PAGES * ARENA_PAGE,
                                               MEM_RESERVE, PAGE_NOACCESS);

    /* 3. привязки сырых адресов: self покрывает весь unit-блок (band_head
     *    вернёт self+0x174 = &BattleAI[0x3c]). */
    bind_add(a, (unsigned)rec->self_raw, arena_region_w(a, "unit"),
             BDD0_UNIT_LEN + BDD0_AI_LEN);
    bind_add(a, rec->band_raw, arena_region_w(a, "band"), band_len);
    bind_add(a, rec->e0, arena_region_w(a, "node_e0"), 0x240);
    bind_add(a, rec->e0_44, arena_region_w(a, "node_e044"), 0x240);
    bind_add(a, rec->tree, arena_region_w(a, "tree"), 0x40);
    /* объект состояния (возврат 442570) — сырой адрес из нативного потока;
     * привязка делает сравниваемые указатели на gs-поля связными. */
    if (rec->has_gs && rec->gs_raw != 0) {
        bind_add(a, rec->gs_raw, arena_region_w(a, "gs"), BDD0_GS_LEN);
    }

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
            /* game_ctx: NULL guard в теле (B43/B50) — 0 остаётся 0.
             * Сырой адрес из unit+0x138 привязывается к shadow-объекту ctx,
             * первый dword shadow = gctx0 из захвата. Без gctx0 — miss
             * (handles FAIL): живой нулевой объект сделал бы разыменование
             * «проверенным». */
            unsigned raw = read_u32(slot);
            if (raw == 0) {
                write_u32(slot, 0);
                continue;
            }
            Reg *cr = reg_find(a, "ctx");
            if (cr == NULL || !rec->has_gctx0) {
                miss(a, fx.what, raw);      /* сырой указатель остаётся непривязанным */
                continue;
            }
            bind_add(a, raw, cr->base, cr->len);
            write_u32(cr->base, rec->gctx0);
            write_u32(slot, (unsigned)(size_t)cr->base);
            /* лист 45c6f0: `[ctx+0xc]` — указатель на pos-тройку. Содержимое
             * области "ctx_pos" — независимый снимок `[*(unit+0x138)+0xc]`
             * (v15, ключ "ctx_pos"); у файлов без ключа тройку кладёт
             * восстанавливающий слой (native_replay.cpp bdd0_leaf_begin /
             * модель синтетики), здесь только связка. */
            {
                unsigned char *cp = arena_region_w(a, "ctx_pos");

                if (cp != NULL) {
                    if (rec->has_ctx_pos) memcpy(cp, rec->ctx_pos, 12);
                    write_u32(cr->base + 0xc, (unsigned)(size_t)cp);
                }
            }
            continue;
        }
        if (fx.target == FX_IDCELL) {
            /* pole-identichnost': 0 ostetsya 0; syroi adres poluchayet svoyu
             * yacheiku (sravnenie dostoverno, razymenovanie belly). */
            unsigned raw = read_u32(slot);
            if (raw == 0) {
                write_u32(slot, 0);
                continue;
            }
            unsigned char *cell = arena_idcell(a, raw);
            if (cell == NULL) {
                miss(a, fx.what, raw);
                continue;
            }
            write_u32(slot, (unsigned)(size_t)cell);
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

    /* v16: модель сетки (grid) и геометрия ленты (band_pos) — до копии входа,
     * чтобы pre-state нёс материализованную модель. */
    materialize_models(a);

    /* v17: вход листа 0x45e1d0 материализован выше (до OS-chosen областей). */

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
    if (a->id_pool != NULL) VirtualFree(a->id_pool, 0, MEM_RELEASE);
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
    if (a->id_pool != NULL && addr >= (unsigned long)a->id_pool
        && addr < (unsigned long)a->id_pool + ARENA_ID_POOL_PAGES * ARENA_PAGE) {
        unsigned raw = 0;
        for (int i = 0; i < a->id_n; i++) {
            if (addr >= (unsigned long)a->ids[i].cell
                && addr < (unsigned long)a->ids[i].cell + ARENA_PAGE) {
                raw = a->ids[i].raw;
                break;
            }
        }
        _snprintf_s(buf, cap, _TRUNCATE,
                    "identity cell (unbound raw 0x%08x)", raw);
        return buf;
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

/* 1 — арена несёт модель объекта сетки (scale-триплет, size_x/size_y, rows,
 * cells и ячейки с цепочками), т.е. порт 0x458050 может прочитать свои входы.
 * Сегодня НИ ОДИН формат захвата (v12..v15) сетку не пишет: `bdd0_in.tree` —
 * только указатель, под него материализуется нулевая область "tree" (0x40 Б),
 * и порт на ней упирается в гейт `size_x <= xi` (возврат NULL там, где натив
 * вернул якорь). Поэтому 0: тик B82 реплеит записанный натив, а сводка печатает
 * `grid_anchor_replayed`. Захват с сеткой (v16) обязан снять ВСЁ СРАЗУ:
 *   (а) эту заглушку → 1,
 *   (б) оба nested-сайта из k_grid_nested_sites (scenario.cpp), иначе заявки
 *       0x456b60/0x457e20 не сопоставятся с вынесенными из окна записями;
 *   (в) 0x458050 — в b0_ported_leaf_va (battle/ported.h) с claim из тела порта.
 * Без (в) запись потока bdd0_leaf va=0x458050 не потребляет никто, и leaf_seq
 * падает на каждом тике: «unconsumed leaf #0 va=0x458050 … consumed 0 of 5».
 * Гейт читает тик (tick.cpp, BATTLE_REPLAY). */
int bdd0_grid_level_modelled(void)
{
    Arena *a = g_cur;
    if (g_model_gates_off) return 0;    /* генератор: запись ведёт модельный натив */
    return (a != NULL && a->grid != NULL && a->grid->valid &&
            a->grid_node_ok) ? 1 : 0;
}

/* 1 — арена несёт модель геометрии ленты: на каждый 24-байтный элемент ленты
 * известны его LDPos (`pos+0x0c/+0x10`, `[pos+0x1c]` → GridLevel с
 * `+0x20/+0x24/+0x28`), т.е. оба гейта ленты — 0x45b150 (s6_band_interp_gate,
 * сайты 0x44c4cb/0x44c5e0) и 0x45b3e0 (s6_band_target_moved, сайты 18/22 =
 * 0x44c554/0x44c666) — могут посчитать свои входы; одна v16-запись `band_pos`
 * обслуживает оба. Сегодня НИ ОДИН формат захвата (v12..v15) этого не
 * пишет: `band_b64` несёт только сами элементы, а `[elem+0x14]` — указатель в
 * кучу игры. Identity-cell привязывается только для FX_IDCELL (`gs+0x14`) и
 * leaf-возвратов k_identity_rets — `[elem+0x14]` не привязан. Поэтому 0: оба
 * листа реплеят записанный натив (tick.cpp b0_band_interp_gate /
 * b0_band_target_moved), claim портов офлайн не исполняется. Захват с
 * геометрией обязан снять ВСЁ СРАЗУ:
 *   (а) эту заглушку → 1,
 *   (б) ключ bdd0_in "band_pos": на элемент — pos x/y (int) и scale-триплет
 *        уровня (+ обвязку указателей в загрузчике),
 *   (в) 0x45b150 И 0x45b3e0 — в b0_ported_leaf_va (battle/ported.h): без (в)
 *        запись потока этих VA не потребляет никто и leaf_seq падает на каждом
 *        тике («unconsumed leaf»); без (б) порт читает `[elem+0x14]` —
 *        непривязанный указатель кучи игры: его разыменование в 0x456b20 даёт
 *        trap «адрес вне всех объявленных областей» (arena_why_addr) —
 *        FAIL `handles`, а не leaf_value FAIL.
 * Гейт читает тик (tick.cpp, BATTLE_REPLAY). */
int bdd0_band_geometry_modelled(void)
{
    Arena *a = g_cur;
    if (g_model_gates_off) return 0;    /* генератор: запись ведёт модельный натив */
    if (a == NULL || a->rec == NULL) return 0;
    return (a->rec->has_band_pos && a->rec->band_pos_n > 0) ? 1 : 0;
}

/* Изменённые канарейки = запись до начала области (запись за конец ловит
 * guard-страница). Возврат 1 при нарушении. */
int arena_scan_writes(Arena *a, char *why, int cap)
{
    if (a == NULL) return 0;
    for (int i = 0; i < a->reg_n; i++) {
        const Reg &r = a->regs[i];
        if (r.fixed) continue;      /* v16: grid_node без canary (только чтение) */
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
