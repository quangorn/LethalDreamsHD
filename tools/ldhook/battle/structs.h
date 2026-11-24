/* battle/structs.h — именованные структуры боевого AI и поиска пути
 * (PLAN_BATTLE_AI_CPP.md, п. 0.2). Имена полей = смысл по дизасму Ghidra;
 * поле с неизвестным смыслом = reserved_XX с фиксированным offset.
 * Раскладка BattleAI (+0x6c/+0x74) противоречива (п. 1.0 плана): окончательный
 * размер объекта НЕ фиксируется — уточняется Фазой 1.
 */
#ifndef BATTLE_STRUCTS_H
#define BATTLE_STRUCTS_H

#include <stddef.h>

/* Оффсеты (подтверждены дизасмом; x86, little-endian), исторические константы site6. */
#define S6_OFF_filter   0x08     /* Candidate.filter */
#define S6_OFF_x        0x0c     /* Candidate.x (int32) */
#define S6_OFF_y        0x10     /* Candidate.y (int32) */
#define S6_OFF_metric   0x24     /* Candidate.metric (int, min в скане ce80) */
#define S6_OFF_head     0x6c     /* BattleAI: TreeNode* sentinel (std::map) */

/* Сетка pathfinding'а (доступ: FUN_00456ed0 / FUN_004570d0 / FUN_00456a70). */
#define S6_GRID_CELL_SIZE 0x34   /* шаг GridCell в массиве cells (FUN_00456ed0) */
#define S6_OFF_chain_next 0x28   /* GridCell: link цепочки обхода (FUN_00456a70) */

/* Forward — LDPos ссылается на уровень сетки. */
struct GridLevel;

/* LDPos — объект позиции (на него ссылается Candidate->pos, [obj+0x00]).
 * Координаты +0x0c/+0x10 читают c480 (distance²) и c880 (предикат дистанции),
 * а dd70 берёт их как центр сетки pathfinding. +0x1c — receiver FUN_00456b20
 * (п. 2.5: [pos+0x1c] = уровень, [pos+0xc]/[pos+0x10] = координаты записи). */
struct LDPos {
    int pad0;                   /* +0x00 */
    int pad4;                   /* +0x04 */
    int pad8;                   /* +0x08 */
    int x;                      /* +0x0c X (int32) */
    int y;                      /* +0x10 Y (int32) */
    int f14;                    /* +0x14 */
    unsigned char reserved_18[4];  /* +0x18..+0x1b */
    GridLevel *level;           /* +0x1c — уровень сетки записи (FUN_00456b20) */
};

static_assert(offsetof(LDPos, x) == S6_OFF_x, "LDPos.x");
static_assert(offsetof(LDPos, y) == S6_OFF_y, "LDPos.y");
static_assert(offsetof(LDPos, level) == 0x1c, "LDPos.level");

/* Candidate (кандидат; историческое имя в коде site6 — LDObject) — node->value1
 * дерева this+0x6c. Разметка MSVC x86. [obj+0x00] — указатель на LDPos
 * (в e0a0 сравнивается с arg1, в c880 идёт в c480 как объект с координатами);
 * [obj+0x24] — метрика (минимум в ce80). */
struct Candidate {
    LDPos *pos;                 /* +0x00 → LDPos (координаты +0x0c/+0x10) */
    int reserved_04;            /* +0x04 */
    int filter;                 /* +0x08 фильтр/тип (скан ce80: [obj+8]==filter) */
    int reserved_0c;            /* +0x0c (не координаты — координаты в pos) */
    int reserved_10;            /* +0x10 */
    int reserved_14;            /* +0x14 */
    int visit_count;            /* +0x18 счётчик визитов (инкремент в dd70) */
    int reserved_1c;            /* +0x1c */
    int reserved_20;            /* +0x20 */
    int metric;                 /* +0x24 метрика (минимум в ce80) */
};

/* Историческое имя site6 (код перенесён 1:1). */
typedef Candidate LDObject;

static_assert(offsetof(Candidate, pos) == 0x00, "Candidate.pos");
static_assert(offsetof(Candidate, filter) == S6_OFF_filter, "Candidate.filter");
static_assert(offsetof(Candidate, visit_count) == 0x18, "Candidate.visit_count");
static_assert(offsetof(Candidate, metric) == S6_OFF_metric, "Candidate.metric");
static_assert(sizeof(Candidate) == 0x28, "Candidate size");

/* TreeNode (MSVC std::_Tree node, 0x18 байт). */
struct TreeNode {
    TreeNode *left;             /* +0x00 */
    TreeNode *parent;           /* +0x04 */
    TreeNode *right;            /* +0x08 */
    int       key;              /* +0x0c ключ (value0) */
    void     *value;            /* +0x10 значение (= Candidate*) */
    unsigned char color;        /* +0x14 (0=чёрный,1=красный) */
    unsigned char is_nil;       /* +0x15 (0=узел,1=sentinel) */
};

static_assert(offsetof(TreeNode, left)   == 0x00, "TreeNode.left");
static_assert(offsetof(TreeNode, parent) == 0x04, "TreeNode.parent");
static_assert(offsetof(TreeNode, right)  == 0x08, "TreeNode.right");
static_assert(offsetof(TreeNode, value)  == 0x10, "TreeNode.value");
static_assert(offsetof(TreeNode, is_nil) == 0x15, "TreeNode.is_nil");
static_assert(sizeof(TreeNode) == 0x18, "TreeNode size");

/* Запись ключа дерева мира (ключ читается FUN_00449160 по leaf-указателю узла):
 * [rec+0x00] = ключ (0x45c3e3: MOV EAX,[ECX] перед CALL 0x449160).
 * +0x18/+0x1c доказаны потребителем FUN_00457e20 (re/leaf_457e20.md §7):
 * геттер 0x456e70 `mov eax,[ecx+0x18]; ret`, обход цепочки `[rec+0x1c]`
 * (@0x457f8d/0x457ff9). Смысл value вне этого сравнения — unknown. */
struct KeyRecord {
    int key;                      /* +0x00 = (Y<<16)|X */
    unsigned char reserved_04[0x14];  /* +0x04..+0x17 — потребителей нет */
    int value;                    /* +0x18 — 0x456e70; сравнивается с arg4 457e20 */
    KeyRecord *next;              /* +0x1c — 0x457f8d/0x457ff9 */
};

static_assert(offsetof(KeyRecord, key) == 0x00, "KeyRecord.key");
static_assert(offsetof(KeyRecord, value) == 0x18, "KeyRecord.value");
static_assert(offsetof(KeyRecord, next) == 0x1c, "KeyRecord.next");
static_assert(sizeof(KeyRecord) == 0x20, "KeyRecord size");

/* GridCell — ячейка сетки (FUN_00456ed0 возвращает cells + idx*0x34, шаг 0x34).
 * Поля доказаны потребителем FUN_0045c3a0 (п. 1.1.8/1.1.14-4 артефакта разведки):
 * +0x14/+0x18 читаются x87 (float), +0x20 — указатель на запись ключа
 * (0x45c3e0/0x45c3e3), +0x24 — next цепочки (0x45c420), +0x28 — link цепочки
 * (FUN_00456a70). Операнд финального гейта — *(float*)p3 (FSUB float ptr [EAX]
 * @0x45c45c, EAX = p3), НЕ поле ячейки; +0x00..+0x03 ничем не читаются. */
struct GridCell {
    unsigned char reserved_00[4]; /* +0x00..+0x03 — consumer'ов нет */
    unsigned char reserved_04[0x10];  /* +0x04..+0x13 */
    float min_metric;             /* +0x14 — FLD [EDI+0x14] @0x45c400, запись min-слота @0x45c414 */
    float max_metric;             /* +0x18 — FCOMP [ESI+0x18] @0x45c403, запись max-слота @0x45c41a */
    unsigned char reserved_1c[4]; /* +0x1c..+0x1f */
    KeyRecord *key_rec;           /* +0x20 — запись ключа (ключ = [key_rec+0]) */
    GridCell *next;               /* +0x24 — следующий узел цепочки (0x45c420) */
    GridCell *chain_next;         /* +0x28 — link цепочки обхода (FUN_00456a70) */
    unsigned char reserved_2c[8]; /* +0x2c..+0x33 */
};

static_assert(offsetof(GridCell, min_metric) == 0x14, "GridCell.min_metric");
static_assert(offsetof(GridCell, max_metric) == 0x18, "GridCell.max_metric");
static_assert(offsetof(GridCell, key_rec)    == 0x20, "GridCell.key_rec");
static_assert(offsetof(GridCell, next)       == 0x24, "GridCell.next");
static_assert(offsetof(GridCell, chain_next) == S6_OFF_chain_next, "GridCell.chain_next");
static_assert(sizeof(GridCell) == S6_GRID_CELL_SIZE, "GridCell size");

/* GridLevel — уровень сетки pathfinding'а (receiver FUN_00456ed0 и FUN_004570d0;
 * root лежит в [ctx+0x08]). Цепочка +0x1c ведёт от корня вниз к листу; лист
 * хранит массив ячеек +0x34 (шаг GridCell) и массив рядов +0x38.
 * +0x2c/+0x30 — границы по X/Y: FUN_004570d0 сравнивает x/y знаково (JGE/JL),
 * FUN_0045cb40/0x45dd70 используют их как clamp через FILD (целые).
 * +0x0c/+0x10 (int) и scale +0x20/+0x24/+0x28 (float) читает FUN_00456b20
 * (п. 2.5): scale-поля листа, FILD-операнды — координаты записи. */
struct GridLevel {
    unsigned char reserved_00[12];    /* +0x00..+0x0b */
    int rec_x;                        /* +0x0c — FILD [ECX+0xc] @0x456b31 */
    int rec_y;                        /* +0x10 — MOV [ECX+0x10] @0x456b27, FILD slot @0x456b40 */
    unsigned char reserved_14[8];     /* +0x14..+0x1b */
    GridLevel *next_level;            /* +0x1c — следующий уровень вниз (NULL = лист);
                                         FUN_00456b20 берёт его как лист напрямую (@0x456b21) */
    float scale_add;                  /* +0x20 — FADD [EAX+0x20] @0x456b37 */
    float scale_load;                 /* +0x24 — FLDS [EAX+0x24] @0x456b24: ST2-операнд
                                         FADD ST0,ST2 @0x456b46 → слагается в out[1] */
    float scale_mul;                  /* +0x28 — FMUL [EAX+0x28] @0x456b34/0x456b44 */
    int size_x;                       /* +0x2c — граница по X (сравнение FUN_004570d0) */
    int size_y;                       /* +0x30 — граница по Y */
    GridCell *cells;                  /* +0x34 — массив ячеек листа, шаг 0x34 */
    int **rows;                       /* +0x38 — массив рядов: rows[x] → row[y] = индекс */
};

static_assert(offsetof(GridLevel, rec_x) == 0x0c, "GridLevel.rec_x");
static_assert(offsetof(GridLevel, rec_y) == 0x10, "GridLevel.rec_y");
static_assert(offsetof(GridLevel, next_level) == 0x1c, "GridLevel.next_level");
static_assert(offsetof(GridLevel, scale_add) == 0x20, "GridLevel.scale_add");
static_assert(offsetof(GridLevel, scale_load) == 0x24, "GridLevel.scale_load");
static_assert(offsetof(GridLevel, scale_mul) == 0x28, "GridLevel.scale_mul");
static_assert(offsetof(GridLevel, size_x) == 0x2c, "GridLevel.size_x");
static_assert(offsetof(GridLevel, size_y) == 0x30, "GridLevel.size_y");
static_assert(offsetof(GridLevel, cells) == 0x34, "GridLevel.cells");
static_assert(offsetof(GridLevel, rows) == 0x38, "GridLevel.rows");

/* PathState (PathfinderState, 0x58 байт) — узел поиска пути; конструктор
 * FUN_0045c2e0 (0x45c2e0..0x45c32e, 79 байт). Раскладка — артефакт разведки
 * re/battle_ai_disasm3.md п. (a2) с поправкой п. 1.1.14-4; тот же тип, что
 * Candidate (первые 13 полей +0x00..+0x30 — таблица бит маски FUN_0045c4b0,
 * п. 1.1.5). Подтверждение раскладки — листинг конструктора: 0x45c2e0 PUSH 0x58
 * + CALL 0x4cfa11 (игровой operator new); 0x45c2f3..0x45c310 обнуление
 * +0x00..+0x24 и +0x30; 0x45c31b/0x45c322 сентинелы; 0x45c313 LEA EDI,[EDX+0x34]
 * + 0x45c316 MOV ECX,9 + 0x45c329 REP STOSD — 9 соседей.
 * Расхождение float/int: п. (a2) подписывает +0x14/+0x18 как int; x87-чтения
 * c3a0 (FLD [node+0x14] @0x45c400, FCOMP [node+0x18] @0x45c403, запись
 * @0x45c414/0x45c417) читают их float'ом (п. 1.1.14-4). По дизасму здесь:
 * +0x14 — float (float-метрика), +0x18 — int g (dd70: MOV @0x45dff9, INC
 * @0x45e002, ADD @0x45e032 — целая арифметика); float-чтение +0x18 относится
 * к цепочке c3a0 (раскладка GridCell), не к PathState. */
struct PathState {
    LDPos *pos;               /* +0x00 — объект позиции (коорд. +0x0c/+0x10):
                                 cb40 @0x45cb4d берёт pos = spec[0], @0x45cb4f/0x45cb52
                                 читает pos->[0x0c]/[0x10]; dd70 @0x45df05 кладёт grid-узел */
    PathState *parent;        /* +0x04 — родитель в сетке (dd70 @0x45df4a: b+0x04 = cur;
                                 mask bit1 c4b0) */
    int filter;               /* +0x08 — состояние (1=кандидат, 2=текущий, 3=посещён);
                                 гейт dd70 @0x45dfb0, mask bit2 */
    int reserved_0c;          /* +0x0c — ctor обнуляет @0x45c2fb; потребитель не найден */
    float out_metric;         /* +0x10 — out4-слот c3a0 (*out4 = world+0x120 + best+0x14,
                                 0x45c430..0x45c43d); cb40 передаёт &spec+0x10 @0x45cb62 */
    float metric_cand;        /* +0x14 — float-метрика: FLD [node+0x14] @0x45c400,
                                 запись spec @0x45c414 */
    int g_cost;               /* +0x18 — g пути (dd70: INC @0x45e002, ADD @0x45e032;
                                 mask bit6) */
    int dist2;                /* +0x1c — dist² к цели (dd70 @0x45e025; mask bit7 —
                                 тест знакомости, TEST DL,DL) */
    int h10000;               /* +0x20 — h·10000 (IMUL 0x2710 @0x45dffc; mask bit8) */
    int metric;               /* +0x24 = h10000+dist²+g — минимум скана ce80 (0x45ceab) */
    int min_sentinel;         /* +0x28 = 0xcb18967f (float −1e7), ctor @0x45c31b; mask bit10 */
    int max_sentinel;         /* +0x2c = 0x4b18967f (float +1e7), ctor @0x45c322; mask bit11 */
    int reserved_30;          /* +0x30 — обнуляется ctor'ом @0x45c310; mask bit12 */
    PathState *nbr[9];        /* +0x34..+0x54 — соседи 3×3, индекс 3·(row−row0)+(col−col0)
                                 (dd70: чтение @0x45de71, запись @0x45e04c, обратная
                                 ссылка nbr[8−idx] @0x45e050) */
};

static_assert(offsetof(PathState, pos)           == 0x00, "PathState.pos");
static_assert(offsetof(PathState, parent)        == 0x04, "PathState.parent");
static_assert(offsetof(PathState, filter)        == 0x08, "PathState.filter");
static_assert(offsetof(PathState, reserved_0c)   == 0x0c, "PathState.reserved_0c");
static_assert(offsetof(PathState, out_metric)    == 0x10, "PathState.out_metric");
static_assert(offsetof(PathState, metric_cand)   == 0x14, "PathState.metric_cand");
static_assert(offsetof(PathState, g_cost)        == 0x18, "PathState.g_cost");
static_assert(offsetof(PathState, dist2)         == 0x1c, "PathState.dist2");
static_assert(offsetof(PathState, h10000)        == 0x20, "PathState.h10000");
static_assert(offsetof(PathState, metric)        == 0x24, "PathState.metric");
static_assert(offsetof(PathState, min_sentinel)  == 0x28, "PathState.min_sentinel");
static_assert(offsetof(PathState, max_sentinel)  == 0x2c, "PathState.max_sentinel");
static_assert(offsetof(PathState, reserved_30)   == 0x30, "PathState.reserved_30");
static_assert(offsetof(PathState, nbr)           == 0x34, "PathState.nbr");
static_assert(sizeof(PathState::nbr) == 0x24, "PathState.nbr size");
static_assert(sizeof(PathState) == 0x58, "PathState size");

/* BattleAI — состояние боевого AI (this во всех слоях: e0a0/ce80/dd70/cb40).
 * Поля зафиксированы только там, где их читает дизасм. Окончательная раскладка
 * (размер объекта, +0x68/+0x6c и +0x74) — решение Фазы 1 (п. 1.0 плана). */
struct BattleAI {
    void *game_ctx;               /* +0x00; game_ctx = [this+0], world = game_ctx[4]
                                     (0x45c3e5/0x45c3e7, 1.0 п.(a)) */
    float timer_start_sec;      /* +0x04 старт таймера (сек) — читает c5a0 */
    float final_threshold;      /* +0x08 финальный порог cb40 (float) */
    float dist_threshold;       /* +0x0c порог дистанции c3a0 (float) */
    int y_radius;               /* +0x10 радиус Y (двойной цикл cb40) */
    int x_radius;               /* +0x14 радиус X */
    struct {                    /* +0x18..+0x27 — RECT (4 LONG): FUN_0045c690
                                 * `LEA ECX,[EBP+0x18]` @0x45c6c6 → PtInRect @0x45c6ca */
        int left;               /* +0x18 */
        int top;                /* +0x1c */
        int right;              /* +0x20 */
        int bottom;             /* +0x24 */
    } canvas_rect;
    unsigned char reserved_28[4];     /* +0x28..+0x2b */
    LDPos *anchor;              /* +0x2c якорь дистанции (LDPos*) */
    unsigned char tracking;     /* +0x30 флаг трекинга дистанции */
    unsigned char reserved_31[3];        /* +0x31..+0x33 */
    unsigned int distance_threshold;     /* +0x34 порог dist² (unsigned) */
    unsigned char reserved_38[0x30];     /* +0x38..+0x67 */
    void *map1;                 /* +0x68 — map1 (ключ 0); head map1 в +0x6c */
    TreeNode *cand_head;        /* +0x6c head std::map кандидатов (скан ce80) */
    int reserved_70;            /* +0x70 — размер map1 (map-ассистенты: [map+0x08]) */
    void *map2;                 /* +0x74 — map2 занятых ячеек; head в +0x78 */
    TreeNode *occ_head;         /* +0x78 — head map2 (end-итератор) */
    /* sizeof(BattleAI)=0x7c — нижняя граница; поля за +0x7c неизвестны. */
};

static_assert(offsetof(BattleAI, game_ctx) == 0x00, "BattleAI.game_ctx");
static_assert(offsetof(BattleAI, timer_start_sec) == 0x04, "BattleAI.timer_start_sec");
static_assert(offsetof(BattleAI, final_threshold) == 0x08, "BattleAI.final_threshold");
static_assert(offsetof(BattleAI, dist_threshold) == 0x0c, "BattleAI.dist_threshold");
static_assert(offsetof(BattleAI, y_radius) == 0x10, "BattleAI.y_radius");
static_assert(offsetof(BattleAI, x_radius) == 0x14, "BattleAI.x_radius");
static_assert(offsetof(BattleAI, canvas_rect) == 0x18, "BattleAI.canvas_rect");
static_assert(sizeof(BattleAI::canvas_rect) == 0x10, "BattleAI.canvas_rect size");
static_assert(offsetof(BattleAI, anchor) == 0x2c, "BattleAI.anchor");
static_assert(offsetof(BattleAI, tracking) == 0x30, "BattleAI.tracking");
static_assert(offsetof(BattleAI, distance_threshold) == 0x34, "BattleAI.distance_threshold");
static_assert(offsetof(BattleAI, map1) == 0x68, "BattleAI.map1");
static_assert(offsetof(BattleAI, cand_head) == S6_OFF_head, "BattleAI.cand_head");
static_assert(offsetof(BattleAI, reserved_70) == 0x70, "BattleAI.reserved_70");
static_assert(offsetof(BattleAI, map2) == 0x74, "BattleAI.map2");
static_assert(offsetof(BattleAI, occ_head) == 0x78, "BattleAI.occ_head");

/* Runtime-флаги layout (пишутся в лог первого вызова site6). static_assert
 * выше делает их константами; флаги сохранены как в исходном коде. */
static const char ld_off_ok =
    (offsetof(Candidate, filter) == S6_OFF_filter &&
     offsetof(Candidate, reserved_0c) == S6_OFF_x &&
     offsetof(Candidate, reserved_10) == S6_OFF_y &&
     offsetof(Candidate, metric) == S6_OFF_metric) ? 1 : 0;
static const char ld_off_ok2 =
    (offsetof(TreeNode, left)   == 0x00 &&
     offsetof(TreeNode, parent) == 0x04 &&
     offsetof(TreeNode, right)  == 0x08 &&
     offsetof(TreeNode, value)  == 0x10 &&
     offsetof(TreeNode, is_nil) == 0x15) ? 1 : 0;

/* timeGetTime из IAT EXE (PTR_timeGetTime_004ec27c) для предиката таймаута. */
typedef unsigned long (__stdcall *PFN_timeGetTime)(void);
#define S6_VA_timeGetTime 0x4ec27cUL

/* Кэш указателя timeGetTime (устанавливается install_site6). */
extern PFN_timeGetTime g_s6_gettime;

#endif /* BATTLE_STRUCTS_H */
