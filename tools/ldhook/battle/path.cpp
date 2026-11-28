/* battle/path.cpp — поиск пути по сетке и std::map-ассистенты
 * (PLAN_BATTLE_AI_CPP.md, п. 0.1; порты FUN_0045cb40 / FUN_0045c3a0 /
 * FUN_00434bc0 / FUN_0047f800 / FUN_0045c2e0 / FUN_0045d810).
 * Код перенесён 1:1 из ldhook.cpp; имена обращений — по structs.h.
 * Статус (п. 2.4): FUN_0045cb40 — faithful (полный листинг game_dis.txt
 * 0x45cb40..0x45cd8c, ESP-проверка слотов); FUN_0045c3a0 — faithful (п. 1.1.8)
 * со store/numerator в p4 (`FSTPS [EDX]`@0x45c43d, `FLDS [ECX]`@0x45c45a —
 * `[ESP+0x20]` при ESP=A−16 = p4).
 * Капа обхода нет: native-циклы ограничены только своими границами (см. cb40),
 * произвольные кап-ограничения не введены (п. 2.4, п. 9).
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <intrin.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "occ.h"

#ifdef BATTLE_REPLAY
extern "C" int g_replay_override;
extern "C" void *replay_path_next(void);
extern "C" void *replay_note_path_res(void *result);
/* Входы финального гейта cb40 (0x45cd3e..0x45cd73) на каждом вызове:
 * spec_in/spec_out = 22 dword PathState (раскладка structs.h), gate = 16 dword
 * (поля гейта + агрегат цепочки tree_search). Харнесс кладёт запись в поток,
 * параллельный leaf.path, и печатает её на первом расхождении. */
extern "C" void replay_note_gate_state(const int *spec_in, const int *spec_out,
                                       const int *gate);

static int g_gate[16];          /* поля гейта текущего cb40-вызова */
static int g_chain[6];          /* агрегат последнего tree_search:
                                 * 0 = best->min_metric bits, 1 = best->max_metric
                                 * bits, 2 = best->key, 3 = число узлов цепочки,
                                 * 4 = p3 (guard гейта c3a0), 5 = p7 (делитель) */
extern "C" int replay_gate_index(void);   /* число cb40-вызовов порта к текущему */
extern "C" int replay_cell_index(void *cell);
/* Opt-in chain trace (LD_REPLAY_TRACE=2): one line per c3a0 call with the
 * reconstructed chain, its statuses and the slot writes. Behaviour-neutral. */
/* v8: restore the per-cb40 chain state (captured at the native cb40 entry)
 * before the port's own search runs; old captures are skipped by the harness. */
extern "C" void replay_apply_chain_state(int leaf_index);
extern "C" void replay_chain_trace(const char *what, const void *p1,
                                   int n_walked, const int *walk,
                                   int p1idx, int lastidx, int bestidx,
                                   int p5before, int p6before,
                                   int p5after, int p6after, int p3, int p7);
#endif
/* In-order successor по std::_Tree (порт FUN_0047f800, без вызова). */
TreeNode *s6_tree_next(TreeNode *node)
{
    TreeNode *next;
    if (node->is_nil) {
        return node;
    }
    next = node->right;
    if (!next->is_nil) {
        while (!next->left->is_nil) {
            next = next->left;
        }
        return next;
    }
    next = node->parent;
    while (!next->is_nil) {
        if (node != next->right) {
            break;
        }
        node = next;
        next = next->parent;
    }
    return next;
}

/* FUN_00434bc0 — std::map<uint32,V>::lower_bound(key) (thiscall, RET 8).
 * this = map: [this+0x04] head (sentinel), [this+0x08] size. Возврат: первый
 * узел с key >= ключа (или head = end, если такого нет). Ключ — TreeNode.key. */
TreeNode *s6_map_lower_bound(TreeNode *head, unsigned int key)
{
    TreeNode *cand;
    TreeNode *node;

    if (head == NULL) {
        return NULL;
    }
    cand = head;
    node = head->parent;                 /* root (== head если дерево пусто) */
    while (node != NULL && !node->is_nil) {
        if ((unsigned int)node->key >= key) {   /* jnc: key >= ключ (unsigned) */
            cand = node;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    if (cand != head && key >= (unsigned int)cand->key) {  /* jnc: key >= ключ */
        return cand;
    }
    return head;                         /* end-итератор */
}

/* FUN_0045c2e0 — конструктор PathState (0x45c2e0..0x45c32e, 79 байт).
 * Листинг baseline (LETHALDREAMS.EXE.pre4, md5 45e93d46…):
 *   0x45c2e0: PUSH 0x58; CALL 0x4cfa11 — игровой operator new; ADD ESP,4 @0x45c2eb;
 *   0x45c2e7..0x45c2f0: EDX = указатель; при NULL — RET 0 (0x45c2f0 JE 0x45c32e);
 *   0x45c2f3..0x45c310: обнуление +0x00,+0x04,+0x08,+0x0c,+0x10,+0x14,+0x18,
 *                       +0x1c,+0x20,+0x24,+0x30;
 *   0x45c313/0x45c316/0x45c329: EDI = EDX+0x34, ECX = 9, REP STOSD — nbr[9] = 0;
 *   0x45c31b: [EDX+0x28]=0xcb18967f (−1e7), 0x45c322: [EDX+0x2c]=0x4b18967f (+1e7);
 *   0x45c32b: EAX = EDX — результат. Callers: 0x45df2c (dd70), 0x45e340, 0x45f158.
 * Решения п. 2.2:
 *   — конструктор АЛЛОЦИРУЕТ сам (PUSH 0x58 + CALL 0x4cfa11 внутри тела), а не
 *     инициализирует блок вызывающего; Ghidra-подпись «void __thiscall(this)»
 *     (п. 1.1.1) — следствие авто-анализа: ECX в теле не читается;
 *   — центр +0x0c/+0x10 из cur->pos ctor НЕ ставит: ctor их обнуляет. Центр
 *     задаёт вызывающий через +0x00 (dd70 @0x45df05 кладёт grid-узел, cb40
 *     @0x45cb4d читает spec[0] = pos и pos->[0x0c]/[0x10] @0x45cb4f/0x45cb52),
 *     т.е. формулировка плана описывает вызывающего, а не ctor;
 *   — аллокация только игровым оператором new (malloc из .pyd запрещён);
 *   — остальные поля ctor не трогает: обнуление = запись нуля в каждое поле
 *     листинга, сентинелы — побитовые 0xcb18967f/0x4b18967f. */
PathState *s6_pfs_ctor(void)
{
    PathState *pfs = (PathState *)native::operator_new(0x58);   /* освобождение — dtor, ещё не перенесён */
    int i;

    if (pfs == NULL) {
        return NULL;                        /* 0x45c2f0: JE 0x45c32e — пустой возврат */
    }
    pfs->pos          = NULL;               /* 0x45c2f3 */
    pfs->parent       = NULL;               /* 0x45c2f5 */
    pfs->filter       = 0;                  /* 0x45c2f8 */
    pfs->reserved_0c  = 0;                  /* 0x45c2fb */
    pfs->out_metric   = 0.0f;               /* 0x45c2fe (+0x10 = 0) */
    pfs->metric_cand  = 0.0f;               /* 0x45c301 (+0x14 = 0) */
    pfs->g_cost       = 0;                  /* 0x45c304 */
    pfs->dist2        = 0;                  /* 0x45c307 */
    pfs->h10000       = 0;                  /* 0x45c30a */
    pfs->metric       = 0;                  /* 0x45c30d */
    pfs->reserved_30  = 0;                  /* 0x45c310 */
    pfs->min_sentinel = 0xcb18967f;         /* float −1e7 побитово; 0x45c31b */
    pfs->max_sentinel = 0x4b18967f;         /* float +1e7 побитово; 0x45c322 */
    for (i = 0; i < 9; i++) {               /* REP STOSD @0x45c329 (ECX=9, EAX=0) */
        pfs->nbr[i] = NULL;
    }
    return pfs;                             /* 0x45c32b: EAX = EDX */
}

/* Диагностикa path-слоя (FUN_0045cb40 / FUN_0045c3a0) — счётники пишутся всегда
 * (InterlockedIncrement нейтрален к поведению), лог только при LDHOOK_LOG=2,
 * троттлинг первые 4 + каждый 128-й. Исходы cb40: first (первый c3a0 = NULL),
 * sign/bound/grid/lookup (ранние выходы), recur (рекурсивный c3a0 = NULL),
 * gatefail (финальный порог), res (успех). */
static volatile LONG g_pfs_calls = 0;
static volatile LONG g_pfs_first_null = 0;
static volatile LONG g_pfs_sign_null = 0;
static volatile LONG g_pfs_bound_null = 0;
static volatile LONG g_pfs_grid_null = 0;
static volatile LONG g_pfs_recur_null = 0;
static volatile LONG g_pfs_gate_fail = 0;
static volatile LONG g_pfs_res = 0;
static volatile LONG g_ts_calls = 0;
static volatile LONG g_ts_null = 0;
static volatile LONG g_pfs_dbg = 0;

#define PFS_R_FIRST   1
#define PFS_R_SIGN    2
#define PFS_R_BOUND   3
#define PFS_R_GRID    4
#define PFS_R_RECUR   5
#define PFS_R_GATE    6
#define PFS_R_OK      7

#ifdef BATTLE_REPLAY
/* Выход cb40: снять поля, от которых зависит финальный гейт (0x45cd3e..0x45cd73)
 * — out_metric (spec+0x10, store c3a0 @0x45c43d), min/max-пары слотов
 * L[0x10]/L[0x2c] и L[0x14]/L[0x18], порог final_threshold (+0x8), исход. */
#define GATE_EXIT(reason, res) do {                                       \
    g_gate[0] = *(const int *)&spec->out_metric;                          \
    g_gate[1] = LS(0x10); g_gate[2] = LS(0x2c);                           \
    g_gate[3] = LS(0x14); g_gate[4] = LS(0x18);                           \
    memcpy(&g_gate[5], &self->final_threshold, sizeof(int));              \
    g_gate[6] = (reason); g_gate[7] = (res);                              \
    g_gate[8] = g_chain[0]; g_gate[9] = g_chain[1];                       \
    g_gate[10] = g_chain[2]; g_gate[11] = g_chain[3];                     \
    g_gate[12] = g_chain[4]; g_gate[13] = g_chain[5];                     \
    g_gate[14] = LS(0x40); g_gate[15] = LS(0x28);                         \
} while (0)
#else
#define GATE_EXIT(reason, res) do { } while (0)
#endif

static void s6_pfs_diag(int reason)
{
    switch (reason) {
        case PFS_R_FIRST: InterlockedIncrement(&g_pfs_first_null); break;
        case PFS_R_SIGN:  InterlockedIncrement(&g_pfs_sign_null);  break;
        case PFS_R_BOUND: InterlockedIncrement(&g_pfs_bound_null);  break;
        case PFS_R_GRID:  InterlockedIncrement(&g_pfs_grid_null);   break;
        case PFS_R_RECUR: InterlockedIncrement(&g_pfs_recur_null);  break;
        case PFS_R_GATE:  InterlockedIncrement(&g_pfs_gate_fail);   break;
        default:          InterlockedIncrement(&g_pfs_res);         break;
    }
    if (g_log_level < LD_DEBUG) {
        return;
    }
    if (ld_rate(&g_pfs_dbg, 4, 128)) {
        ld_debug("pfs: calls=%ld first=%ld sign=%ld bound=%ld grid=%ld recur=%ld "
                 "gatefail=%ld res=%ld ts_calls=%ld ts_null=%ld (reason=%d)",
                 (long)g_pfs_calls, (long)g_pfs_first_null, (long)g_pfs_sign_null,
                 (long)g_pfs_bound_null, (long)g_pfs_grid_null, (long)g_pfs_recur_null,
                 (long)g_pfs_gate_fail, (long)g_pfs_res, (long)g_ts_calls,
                 (long)g_ts_null, reason);
    }
}

void s6_pfs_snapshot(long *out7)
{
    out7[0] = (long)g_pfs_first_null;
    out7[1] = (long)g_pfs_sign_null;
    out7[2] = (long)g_pfs_bound_null;
    out7[3] = (long)g_pfs_grid_null;
    out7[4] = (long)g_pfs_recur_null;
    out7[5] = (long)g_pfs_gate_fail;
    out7[6] = (long)g_pfs_res;
}

/* ------------------------------------------------------------------ */
/* E0PATH — разложение µs внутри FUN_0045cb40 (план 9.2a).              */
/*                                                                      */
/* env LDHOOK_E0_DIAG=1 + порт site6. Счётчики interlocked; при env      */
/* не заданом ни одна ветка ниже не выполняется — поведение побайтно     */
/* прежнее (один предсказуемый branch на горячих точках).               */
/* Дизъюнктность: lower_bound, grid-lookup и 0x45c3a0 зовутся из         */
/* grid-цикла cb40 по одному разу за итерацию; их µs-суммы — подмножества */
/* us_path (E0COST), между собой не пересекаются. «прочее» = us_path −    */
/* (lb_us + grid_us + ts_us) = цикл окна, гейты и финальный порог.       */
/* Рекурсия считается верхним уровнем: 0x45c3a0 не зовёт ни cb40, ни      */
/* сам себя, вложенность всегда 1 → µs не умножаются на nesting.          */
/* Ключ повтора — (spec->pos = стартовая клетка, target вызова e0a0):     */
/* cb40 зависит от клетки и состояния карт; target в ключ входит, чтобы   */
/* не склеивать запросы разных целей одного тика.                        */
/* Таблица — фиксированная, 4096 слотов, open-addressing, без аллокаций;  */
/* сброс на входе тика (H1). Слот занят = cell != NULL.                 */
/* ------------------------------------------------------------------ */

#define E0P_TABLE   4096               /* слотов повторов (степень двойки) */
#define E0P_PROBE   8                  /* линейный пробой на вызов */

volatile LONG g_e0p_calls = 0, g_e0p_null = 0, g_e0p_lb = 0, g_e0p_grid = 0;
volatile LONG g_e0p_ts = 0, g_e0p_recur = 0, g_e0p_cells = 0;
volatile LONG g_e0p_dup_call = 0, g_e0p_dup_tick = 0, g_e0p_ovf = 0;
volatile LONG g_e0p_depth = 0, g_e0p_ts_depth = 0, g_e0p_depth_max = 0;
volatile LONG g_e0p_loop = 0;   /* 1 = grid-цикл cb40 открыт: входы 0x45c3a0 рекурсивные */
volatile LONGLONG g_e0p_lb_us = 0, g_e0p_grid_us = 0, g_e0p_ts_us = 0;

static void *g_e0p_key_cell[E0P_TABLE];        /* ключ занят: cell != NULL */
static void *g_e0p_key_target[E0P_TABLE];
static LONG  g_e0p_key_call[E0P_TABLE];        /* номер вызова e0a0 при вставке */
static LONG  g_e0p_cur_call = 0;
static void  *g_e0p_cur_target = NULL;

void e0path_target(void *target) { g_e0p_cur_target = target; }
void e0path_call(long call_no) { g_e0p_cur_call = (LONG)call_no; }
int  e0_path_inside(void) { return (g_e0p_depth > 0) ? 1 : 0; }

void e0path_tick_reset(void)
{
    if (!e0_diag_on()) {
        return;
    }
    memset(g_e0p_key_cell, 0, sizeof(g_e0p_key_cell));
    memset(g_e0p_key_target, 0, sizeof(g_e0p_key_target));
    memset(g_e0p_key_call, 0, sizeof(g_e0p_key_call));
}

/* Один path-вызов: ключ (cell, target) в таблицу тика. Первый sighting —
 * вставка; повтор → dup_tick, и dup_call, если вставлен тем же вызовом e0a0. */
static void e0path_repeat_note(void *cell)
{
    LONG j, i, k;
    void *tgt = g_e0p_cur_target;

    j = (LONG)(((UINT_PTR)cell >> 4) * 2654435761u) & (E0P_TABLE - 1);
    for (k = 0; k < E0P_PROBE; k++) {
        i = (j + k) & (E0P_TABLE - 1);
        if (g_e0p_key_cell[i] == NULL) {
            g_e0p_key_cell[i] = cell;
            g_e0p_key_target[i] = tgt;
            g_e0p_key_call[i] = g_e0p_cur_call;
            return;
        }
        if (g_e0p_key_cell[i] == cell && g_e0p_key_target[i] == tgt) {
            InterlockedIncrement(&g_e0p_dup_tick);
            if (g_e0p_key_call[i] == g_e0p_cur_call) {
                InterlockedIncrement(&g_e0p_dup_call);
            }
            return;
        }
    }
    InterlockedIncrement(&g_e0p_ovf);          /* пробой исчерпан — повторов не учтено */
}

/* FUN_0045c390 (15 байт, 0x45c390..0x45c39e) — знаковый max(a,b).
 *   0x45c390: MOV EAX,[ESP+0x4] ; 0x45c394: MOV ECX,[ESP+0x8]
 *   0x45c398: CMP EAX,ECX ; 0x45c39a: JG 0x45c39e      (a>b → вернуть a)
 *   0x45c39c: MOV EAX,ECX                              (иначе — b)
 * ABI: int cdecl(int a, int b), RET. Только знаковое целое, x87 нет. */
int s6_imax(int a, int b)
{
    if (a > b) {
        return a;                      /* 0x45c39a JG */
    }
    return b;                          /* 0x45c39c */
}

/* Целочисленный abs из идиомы `CQTD; XOR EDX,EAX; SUB EDX,EAX`
 * (cb40 @0x45cc8a/0x45cc94). При INT_MIN даёт INT_MIN (переполнение, как в
 * оригинале) — unsigned-арифметика ниже сохраняет этот результат. */
static int s6_iabs(int x)
{
    unsigned int m = (unsigned int)(x >> 31);   /* CQTD: 0 или 0xffffffff */
    return (int)((x ^ (int)m) - (int)m);
}

/* world+0x120 (float) — consumer FUN_0045c3a0 @0x45c430 (FLD float ptr [ECX+0x120]). */
static float s6_world_scale(void *world)
{
    float v;

    memcpy(&v, (unsigned char *)world + 0x120, sizeof(float));
    return v;
}

/* float ↔ целые биты слота (x87-операнды `FLDS/FCOMPS [ESP+off]` над int-слотами
 * cb40 — native трактует содержимое слота как float). */
static void s6_bits_set(int *slot, float f)
{
    memcpy(slot, &f, sizeof(int));
}

static float s6_bits_get(int slot)
{
    float f;
    memcpy(&f, &slot, sizeof(float));
    return f;
}

/* FUN_0045c3a0 (221 байт: 0x45c3a0..0x45c3dc, 0x45c3e0..0x45c47f) — обход цепочки
 * узлов пути (walk_chain). Единственный каллер — FUN_0045cb40 (два site), включая
 * рекурсивный вызов из grid-цикла (@0x45ccc1) — порт допускает рекурсию.
 * ABI: int __thiscall(BattleAI *this, p1..p7), RET 0x1c; ECX = this (0x45c3a8).
 * Аргументы на стеке; ESP входа = A; пролог: PUSH EBX @0x45c3a0, PUSH ESI @0x45c3a1
 * (ESP = A−8), PUSH EBP @0x45c3b4 (ESP = A−12), PUSH EDI @0x45c3c9 (ESP = A−16):
 *   p1 — `[ESP+0xc]`@0x45c3a2 (ESP=A−8): цепочный узел. Первый вызов = pos
 *       (@0x45cb6a PUSH EAX, EAX = spec[0]); рекурсия = grid-узел (@0x45ccbe PUSH EBX).
 *       Начало цепочки [p1+0x28] (0x45c3c6), спуск FUN_00456a70 (@0x45c3d0).
 *   p2 — значение; 0x45c3a0 p2 НЕ читает.
 *   p3 — `[ESP+0x1c]`@0x45c43f (ESP=A−16): guard гейта `TEST EAX,EAX`@0x45c443 и
 *       float-указатель `FSUB float ptr [EAX]`@0x45c45c. Первый вызов = 0
 *       (@0x45cb63 PUSH 0x0) → гейт пропускается. Рекурсия = ЦЕЛОЕ счётчика
 *       L[0x20] (посев @0x45cb71 = pos[0x10], перезапись @0x45cbca = −x_radius,
 *       инкремент внешний хвост) (@0x45cca8 `MOV ECX,0x30(%esp)` при ESP=Y−92 →
 *       push @0x45ccbc), гейт разыменовывает его как float-указатель — аварию ловит SEH-рамка 0x44bdd0 (граница
 *       harness'а — __try у s6_ai_update).
 *   p4 — `[ESP+0x20]`@0x45c436 (ESP=A−16): STORE-слот; `FSTPS [EDX]`@0x45c43d
 *       (при best!=0: *p4 = world[0x120] + best[0x14]) и numerator гейта
 *       `FLDS [ECX]`@0x45c45a — тот же [ESP+0x20] = p4. Первый вызов cb40:
 *       &spec[0x10] (@0x45cb62); рекурсия: &out-слота lower_bound (@0x45ccb5).
 *   p5 — `[ESP+0x20]`@0x45c3b5 (ESP=A−12) = EBP: MIN-слот; посев −1e7 @0x45c3b9;
 *       запись node[0x14] @0x45c414. Первый вызов = &spec[0x28] (@0x45cb5a),
 *       рекурсия — см. FUN_0045cb40.
 *   p6 — `[ESP+0x20]`@0x45c3b0 (ESP=A−8) = EAX, и `[ESP+0x28]`@0x45c410 (ESP=A−16):
 *       MAX-слот; посев +1e7 @0x45c3c0; запись node[0x18] @0x45c417/0x45c41a.
 *   p7 — `[ESP+0x2c]`@0x45c460 (ESP=A−16): делитель гейта (FIDIV): 1 в первом вызове
 *       (@0x45cb55), max(|dy|,|dx|) из FUN_0045c390 в рекурсии (@0x45cc9a/0x45cca2).
 * Обход (0x45c3e0..0x45c425): ключ = [[node+0x20]] (0x45c3e0/0x45c3e3), статус =
 * native::tree_lookup(world = game_ctx[4], key) (@0x45c3e5..0x45c3eb); status==1 →
 * кандидат (EDI = узел, 0x45c3f8); status==0 → min/max: при best==0 сравнивать
 * не с чем (0x45c3fc JZ 0x45c41e), иначе сравнение best[0x14] vs node[0x18]
 * (FLD@0x45c400 / FCOMP@0x45c403) — JP(маска 0x5) взят ⟺ NOT(a<b) (п. 1.1.7),
 * т.е. a>=b → сброс кандидата (0x45c41e); при строго a<b — min-слот = node[0x14]
 * (@0x45c414) и max-слот = node[0x18] (@0x45c410/0x45c417/0x45c41a); status>1
 * (DEC/JNZ @0x45c3f6) → узел пропущен. Переход: node = [node+0x24] (0x45c420),
 * цикл по node!=0 (0x45c423/0x45c425) — капа нет: native идёт до NULL.
 * Гейт (0x45c43f..0x45c46e): guard p3 (@0x45c443), best (@0x45c447) и бит1 флагов
 * game_ctx (SHR EDX,0x1 / TEST DL,0x1 @0x45c44f/0x45c451); величина
 * |*p5 − *(float*)p3| / p7 (0x45c456..0x45c460) сравнивается с this+0xc
 * (FCOMP @0x45c464); JNZ(маска 0x41) взят ⟺ NOT(a>b) → кандидата оставить; при
 * строго a>b → сброс (XOR EDI,EDI @0x45c46e).
 * SEH: в 0x45c3a0 своего фрейма нет, порт без собственного __try; граница
 * harness'а — __try у s6_ai_update (соответствует фрейму 0x44bdd0).
 */
GridCell *s6_path_tree_search(BattleAI *self, GridCell *p1, int p2, int p3,
                              float *p4, int *p5, int *p6, int p7)
{
    LD_CRASH_SCOPE(4);           /* LDHOOK_CRASHLOG: stage 4 (no-op off) */
    GridCell *cur;
    GridCell *best = NULL;
    /* E0PATH (план 9.2a) — локальные мерки этого входа. */
    const int diag = e0_diag_on();
    LONGLONG tp = -1;
    LONG tsdepth;
#ifdef BATTLE_REPLAY
    int n_walked = 0;               /* число узлов цепочки, прочитанных циклом */
    int p5before = (p5 != NULL) ? *p5 : 0, p6before = (p6 != NULL) ? *p6 : 0;
    int walk[16 * 5];               /* idx,key,status,min,max на узел (opt-in trace) */
#endif

    InterlockedIncrement(&g_ts_calls);
    /* E0PATH (план 9.2a): diag==0 → ни записей, ни замеров (env не задан).
     * Вход в 0x45c3a0: верхний (первый c3a0 cb40) или рекурсивный — по флагу
     * grid-цикла. Вложенность считается и не умножается: 0x45c3a0 не зовёт ни
     * 45cb40, ни себя, nesting всегда 1 → µs каждого входа идут в сумму один раз. */
    if (diag) {
        InterlockedIncrement(&g_e0p_ts);
        if (g_e0p_loop != 0) {
            InterlockedIncrement(&g_e0p_recur);
        }
        tsdepth = InterlockedIncrement(&g_e0p_ts_depth);
        if (tsdepth > g_e0p_depth_max) {
            g_e0p_depth_max = tsdepth;
        }
        tp = ld_now_us();
    }
    (void)p2;                          /* 0x45c3a0 p2 не читает */
    /* p4: store @0x45c43d (FSTPS [EDX]) и numerator гейта @0x45c45a ([ESP+0x20]
     * при ESP=A−16) — p4 читается, слот p4 = адрес store/numerator. */

#ifdef BATTLE_REPLAY
    g_chain[0] = g_chain[1] = g_chain[2] = g_chain[3] = 0;
    g_chain[4] = p3; g_chain[5] = p7;
#endif

    if (p1 == NULL) {                  /* 0x45c3a6/0x45c3aa → 0x45c479: XOR EAX,EAX */
        InterlockedIncrement(&g_ts_null);
        if (diag) {
            InterlockedAdd64(&g_e0p_ts_us, ld_now_us() - tp);
            InterlockedDecrement(&g_e0p_ts_depth);
        }
        return NULL;
    }

    *p5 = 0xcb18967f;                  /* MIN-слот = −1e7 (0x45c3b9, EBP = p5) */
    *p6 = 0x4b18967f;                  /* MAX-слот = +1e7 (0x45c3c0, EAX = p6) */

    cur = p1;                          /* 0x45c3ca: XOR EDI,EDI → best = 0 */
    if (p1->chain_next != NULL) {      /* 0x45c3c6/0x45c3cc JZ 0x45c3d7 */
        cur = s6_chain_last(p1);       /* FUN_00456a70 @0x45c3d0 — спуск по +0x28 до NULL, капа нет */
    }

    for (; cur != NULL; cur = cur->next) {   /* 0x45c420/0x45c423/0x45c425 — капа нет */
        unsigned char *ctx;
        void *world;
        int status;

        ctx = (unsigned char *)self->game_ctx;                         /* 0x45c3e5 */
        world = *(void **)(ctx + 0x04);                                /* 0x45c3e7 */
#ifdef BATTLE_REPLAY
        n_walked++;
#endif
        status = native::tree_lookup(world, (unsigned int)cur->key_rec->key);  /* 0x45c3eb */
#ifdef BATTLE_REPLAY
        if (n_walked < 16) {
            int *e = &walk[(n_walked - 1) * 5];
            e[0] = replay_cell_index(cur);
            e[1] = cur->key_rec->key;
            e[2] = status;
            e[3] = *(const int *)&cur->min_metric;
            e[4] = *(const int *)&cur->max_metric;
        }
#endif

        if (status == 1) {                 /* 0x45c3f8: EDI = cur — кандидат принят */
            best = cur;
        } else if (status == 0) {          /* 0x45c3f3 JZ → 0x45c3fc: min/max */
            if (best != NULL) {            /* 0x45c3fc TEST EDI,EDI — есть с чем сравнивать */
                float a = best->min_metric;   /* FLD [EDI+0x14] @0x45c400 */
                float b = cur->max_metric;    /* FCOMP [ESI+0x18] @0x45c403 */
                if (a < b) {                 /* JP(маска 0x5) NOT taken ⟺ строго a<b (п. 1.1.7) */
                    memcpy(p5, &cur->min_metric, sizeof(int));   /* 0x45c40d/0x45c414: *p5 = node[0x14] */
                    memcpy(p6, &cur->max_metric, sizeof(int));   /* 0x45c410/0x45c417/0x45c41a: *p6 = node[0x18] */
                } else {
                    best = NULL;           /* a>=b → JP взят → 0x45c41e: сброс кандидата */
                }
            }
            /* best == NULL: 0x45c3fc JZ 0x45c41e — сравнивать не с чем, EDI остаётся 0 */
        }
        /* status > 1: DEC/JNZ @0x45c3f6 — узел пропущен */
    }

    if (best != NULL) {                    /* 0x45c427 TEST EDI,EDI; JZ 0x45c43f */
        unsigned char *ctx = (unsigned char *)self->game_ctx;   /* 0x45c42b: MOV EAX,[EBX] */
        void *world = *(void **)(ctx + 0x04);                   /* 0x45c42d: MOV ECX,[EAX+4] */
        float out = s6_world_scale(world) + best->min_metric;   /* 0x45c430..0x45c43a */
        s6_bits_set((int *)p4, out);      /* FSTPS [EDX], EDX = [ESP+0x20] = p4 @0x45c43d/0x45c436 */
    }

    /* Финальный гейт (0x45c43f..0x45c46e). p3 = 0 в первом вызове (@0x45cb63) —
     * гейт пропускается (TEST EAX,EAX @0x45c443 → JZ 0x45c470). */
    if (p3 != 0 && best != NULL) {         /* guard p3 (@0x45c443), best (@0x45c447) */
        unsigned char *ctx = (unsigned char *)self->game_ctx;   /* 0x45c44b: MOV ECX,[EBX] */
        if (((*(const unsigned int *)ctx >> 1) & 1) != 0) {     /* SHR EDX,1 / TEST DL,1 @0x45c44f/0x45c451 */
            float ref, d, thr;

            memcpy(&ref, (const void *)(INT_PTR)p3, sizeof(float));  /* FSUB [EAX], EAX = p3 @0x45c45c —
                                                                       * значение p3 разыменовывается как float-указатель,
                                                                       * как в оригинале */
            d = s6_bits_get(*(int *)p4) - ref;  /* FLD [EDX]@0x45c45a (p4) − FSUB @0x45c45c */
            if (d < 0.0f) {
                d = -d;                    /* FABS @0x45c45e */
            }
            d = d / (float)p7;             /* FIDIV dword ptr [ESP+0x2c] = p7 @0x45c460 */
            thr = self->dist_threshold;    /* FCOMP float ptr [EBX+0xc] @0x45c464 */
            if (d > thr) {                 /* JNZ(маска 0x41) NOT taken ⟺ строго a>b
                                            * → сброс (XOR EDI,EDI @0x45c46e) */
                best = NULL;
            }
        }
    }
    if (best == NULL) {
        InterlockedIncrement(&g_ts_null);
    }
    if (diag) {
        InterlockedAdd64(&g_e0p_ts_us, ld_now_us() - tp);
        InterlockedDecrement(&g_e0p_ts_depth);
    }
#ifdef BATTLE_REPLAY
    g_chain[3] = n_walked;
    g_chain[0] = (best != NULL) ? *(const int *)&best->min_metric : 0;
    g_chain[1] = (best != NULL) ? *(const int *)&best->max_metric : 0;
    g_chain[2] = (best != NULL) ? best->key_rec->key : 0;
    replay_chain_trace("c3a0", p1, n_walked, walk,
                       replay_cell_index(p1),
                       (n_walked > 0 && n_walked <= 16)
                           ? walk[(n_walked - 1) * 5] : -1,
                       (best != NULL) ? replay_cell_index(best) : -1,
                       p5before, p6before,
                       (p5 != NULL) ? *p5 : 0, (p6 != NULL) ? *p6 : 0,
                       p3, p7);
#endif
    return best;
}

/* FUN_0045cb40 (589 байт, 0x45cb40..0x45cd8c) — древовидный поиск по сетке от
 * центра pos = spec[0]; каллер dd70 (@0x45df17) и @0x45f12f, spec = PathState-
 * форма dd70-окна L[0x3c..0x6c]. ABI: int __thiscall(BattleAI *this, PathState
 * *spec), RET 0x8; ECX = this (0x45cb47); 2 стек-арга (pos, &spec), arg2 не
 * читается. SEH-фрейма нет: граница harness'а — __try у s6_ai_update.
 * Рамка: ESP на call dd70 = U−8 (pushes @0x45df04/0x45df10), entry = U−12
 * (retaddr), `sub esp,0x3c` @0x45cb40 + 4 push → cb40 base Y = U − 88; слот
 * cb40 k (относительно Y) ↔ dd70 slot (k − 88) (re/esp_scan.py; retaddr входит
 * в delta, слоты dd70 alias'а не имеют). Все чтения
 * идут после записей — stale-read'ов в cb40 нет; в частности @0x45cbad
 * L[0x20] читает слот, записанный @0x45cb71 (посев pos->y).
 * Таблица слотов (LS(k) = l[k>>2], k относительно Y):
 *   L[0x04] — arg1 = &spec (@0x45cb49); он же L[0x50] при ESP−84 @0x45ccb1;
 *   L[0x3c] — pos->x (@0x45cb6d); перечитывается ebx @0x45ccce;
 *   L[0x20] — pos->y (@0x45cb71); центр ecx @0x45cbad, внешний хвост @0x45cd28;
 *   L[0x28] — &spec+0x10 (@0x45cb66); p3 рекурсии @0x45cca8, финальный
 *            гейт ecx @0x45cd49;
 *   L[0x10] — MIN-пара 1: посев spec[0x2c] (+1e7) @0x45cba0; хвост @0x45ccfc,
 *            гейт @0x45cd57/0x45cd6a;
 *   L[0x2c] — MIN-пара 2: посев spec[0x28] (−1e7) @0x45cb9a; p3-источник
 *            @0x45cca8; хвост @0x45cce5;
 *   L[0x14] — −1e7 посев @0x45cc18 (ESP Y−8→−84); пара с L[0x2c] (@0x45ccd2),
 *            p5 рекурсии = &LS(0x14);
 *   L[0x18] — +1e7 посев @0x45cc20; пара с L[0x10], p6 рекурсии = &LS(0x18);
 *   L[0x1c] — loop_base/inner idx: посев edi−ebx = −y_radius @0x45cbe9 (до
 *            чтения @0x45cc86), +1 @0x45cd09, abs-операнд @0x45cc86;
 *   L[0x24] — min_slot/outer idx: посев ebp−ecx = −x_radius @0x45cbca (до
 *            чтения @0x45cd1f), +1 @0x45cd30, abs-операнд @0x45cc90
 *            (`MOV 0x28(ESP)` при Y−80 → L[0x24]);
 *   L[0x30] — текущий inner edi (@0x45cbdb), FILD-операнд size_x (@0x45cc4d);
 *   L[0x34] — текущий outer ebp (@0x45cbbc), FILD-операнд size_y (@0x45cc61);
 *   L[0x38] — ключ клетки (@0x45cc07), &key lower_bound (@0x45cc0b);
 *   L[0x40] — результат первого c3a0 (@0x45cb8b), возврат edx (@0x45cd3a);
 *   L[0x44] — out-слот lower_bound (&L[0x44] @0x45cc10, callee пишет
 *            0x434c10/0x434c27); читается @0x45cc2d;
 *   L[0x48] — p4 рекурсии = &LS(0x48) (@0x45ccb5).
 * Тело: посев pos->x/pos->y (@0x45cb6d/0x45cb71), сентинелы spec[0x28]=−1e7,
 * spec[0x2c]=+1e7 (@0x45cb75/0x45cb7b); CALL c3a0 (p7=1, p6=&spec+0x2c,
 * p5=&spec+0x24, p4=&spec+0x10, p3=0, p2=p1=pos) @0x45cb82 — результат в
 * L[0x40], NULL → возврат 0 (@0x45cb8f). Флаг game_ctx&1 (@0x45cba4): 0 →
 * сразу финальный гейт. Центр ecx = pos->y (@0x45cbad); внешний цикл от
 * центра−x_radius до центра+x_radius (@0x45cbb1..0x45cbc0, старт > конец →
 * пропуск); внутренний от pos->x−y_radius до pos->x+y_radius (@0x45cbd0..
 * 0x45cbdf); центральная клетка (ecx==ebp && ebx==edi @0x45cbf0/0x45cbf4)
 * пропускается; ключ = (word(ebp)<<16)|word(edi) (@0x45cbfc..0x45cc07);
 * lower_bound(map2, &LS(0x44), &LS(0x38)) @0x45cc28; узел == occ_head → NOT
 * занято (@0x45cc2d/0x45cc32; занято → сразу хвост); тесты знаков edi<0/
 * ebp<0 (@0x45cc3f/0x45cc45) → 0; границы !(size_x > L[0x30]), !(size_y >
 * L[0x34]) (FILD-порядок: ST0 = size (@0x45cc51/0x45cc65), ST1 = счётчик
 * (@0x45cc4d/0x45cc61); маска 0x41 → выход при size <= счётчик, grid =
 * game_ctx[8]) → 0; узел CALL
 * 0x4570d0(grid, edi, ebp) — x=inner, y=outer (@0x45cc77, RET 8), NULL → 0
 * (@0x45cc80); p7 = imax(|L[0x1c]|,|L[0x24]|) (@0x45cc86..0x45cca2);
 * рекурсия CALL c3a0 @0x45ccc1: p1=узел, p2=spec[0], p3=&spec+0x10 (int,
 * гейт c3a0 разыменовывает как float* — валидный &out_metric, AV нет),
 * p4=&LS(0x48), p5=&LS(0x14), p6=&LS(0x18), p7; NULL → 0 (@0x45ccce JE).
 * Хвост (0x45ccd2, обе ветви head-проверки): bits(L[0x2c])<bits(L[0x14]) →
 * L[0x2c]=L[0x14]; bits(L[0x10])>bits(L[0x18]) → L[0x10]=L[0x18].
 * Финальный гейт (0x45cd3e..0x45cd73): флаги сравнения world[0x120] с
 * [0x4ec37c]=0.0f вычисляются, второй FNSTSW (@0x45cd5e) их перекрывает;
 * успех ⟺ JP(маска 0x5, FCOMPS(out,L[0x10]) @0x45cd57) ИЛИ JNE(маска 0x41,
 * FCOMPS(final_threshold+out, L[0x10]) @0x45cd6a); min = bits(L[0x10]),
 * out = spec->out_metric. Возврат: L[0x40] при успехе, иначе 0. */
static void *s6_path_search_body(BattleAI *self, PathState *spec)
{
    LD_CRASH_SCOPE(4);           /* LDHOOK_CRASHLOG: stage 4 (no-op off) */
    LDPos *pos = spec->pos;           /* spec[0] — grid-клетка; координаты +0xc/+0x10
                                       * читаются LDPos-раскладкой (@0x45cb4f/0x45cb52) */
    int l[22];                        /* слоты Y+0x00..Y+0x54 */
    /* E0PATH (план 9.2a): diag==0 → ни одной записи ниже, поведение прежнее. */
    const int diag = e0_diag_on();
    LONGLONG tp = -1;

    /* LDHOOK_OCC_SHARED: клетка входа (pos) пишется в реестр общей занятости. */
    if (occ_shared_on()) {
        occ_shared_note(self, spec->pos->x, spec->pos->y);
    }

#define LS(k) (l[(k) >> 2])

    LS(0x28) = (int)(INT_PTR)&spec->out_metric;  /* @0x45cb66 */
    LS(0x3c) = pos->x;                            /* @0x45cb6d */
    LS(0x20) = pos->y;                            /* @0x45cb71 */
    spec->min_sentinel = 0xcb18967f;              /* @0x45cb75 */
    spec->max_sentinel = 0x4b18967f;              /* @0x45cb7b */
    LS(0x40) = (int)(INT_PTR)s6_path_tree_search(self, (GridCell *)pos, (int)(INT_PTR)pos, 0,
                                                 (float *)&spec->out_metric,
                                                 &spec->min_sentinel,
                                                 &spec->max_sentinel, 1);  /* @0x45cb82 */
    InterlockedIncrement(&g_pfs_calls);
    if (LS(0x40) == 0) {
        s6_pfs_diag(PFS_R_FIRST);
        GATE_EXIT(PFS_R_FIRST, 0);
        return NULL;                              /* @0x45cb8f JE 0x45cd75 */
    }
    LS(0x2c) = *(const int *)&spec->min_sentinel; /* @0x45cb9a */
    LS(0x10) = *(const int *)&spec->max_sentinel; /* @0x45cba0 */

    if (((int)*(const unsigned int *)self->game_ctx & 1) != 0) {  /* @0x45cba4 TESTB $1,(%eax) */
        int ecx, ebx, edi, ebp;

        ecx = LS(0x20);                           /* @0x45cbad — слот записан @0x45cb71, stale нет */
        ebp = ecx - self->x_radius;               /* @0x45cbb1..0x45cbb6 */
        LS(0x34) = ebp;                           /* @0x45cbbc */
        if (ebp <= ecx + self->x_radius) {        /* @0x45cbc0 JG → пропуск внешнего */
            LS(0x24) = -self->x_radius;           /* @0x45cbca (min_slot, запись до чтения @0x45cd1f) */
            if (diag) {
                g_e0p_loop = 1;                   /* E0PATH: входы 0x45c3a0 отсюда — рекурсия */
            }
            ebx = LS(0x3c);                       /* pos->x (ebx @0x45cb4f, перечитывание @0x45ccce) */
            for (;;) {                            /* внешний цикл (ebp — outer-счётчик); вход @0x45cbd0
                                                   * (капа @0x45cd34 JLE → 0x45cbd0: каждый виток заново
                                                   * сеет edi (@0x45cbd0) и L[0x1c] (@0x45cbe9)) */
                edi = ebx - self->y_radius;       /* @0x45cbd0..0x45cbd5 */
                LS(0x30) = edi;                   /* @0x45cbdb */
                if (edi <= ebx + self->y_radius) {    /* @0x45cbdf JG → пропуск внутреннего */
                LS(0x1c) = -self->y_radius;       /* @0x45cbe9 (loop_base, запись до чтения @0x45cc86) */
                    for (;;) {                    /* внутренний цикл (edi — inner-счётчик) */
                        if (diag) {
                            InterlockedIncrement(&g_e0p_cells);  /* ячейка радиального окна
                                                                  * (y ± x_radius, x ± y_radius);
                                                                  * центральная — тоже виток */
                        }
                        if (!(ecx == ebp && ebx == edi)) {   /* @0x45cbf0/0x45cbf4 центр (текущий ecx) */
                        ecx = (int)((unsigned short)ebp << 16)
                            | (int)(unsigned short)edi;       /* @0x45cbfc..0x45cc05: ключ строится В ecx */
                        LS(0x38) = ecx;                       /* @0x45cc07 */
                        LS(0x14) = 0xcb18967f;                /* @0x45cc18: −1e7 посев L[0x14] (`MOV 0x1C(ESP)` при Y−8) */
                        LS(0x18) = 0x4b18967f;                /* @0x45cc20: +1e7 посев L[0x18] (`MOV 0x20(ESP)` при Y−8) */
                        native::lower_bound_slots(&self->map2, &LS(0x44), &LS(0x38));  /* @0x45cc10/&L[0x44], @0x45cc0b/&L[0x38], CALL 0x434bc0 */
                        ecx = (int)(INT_PTR)&self->map2;      /* @0x434bc0 PUSH ECX / POP ECX @0x434c13/0x434c2a:
                                                               * ecx возвращает посев @0x45cc15 (lea 0x74(%esi)) */
                        if ((void *)(INT_PTR)LS(0x44) == self->occ_head) {  /* @0x45cc2d: head → NOT занято */
                            /* LDHOOK_OCC_SHARED (эксперимент): реестр клеток юнитов
                             * (battle/occ.cpp) на клетку другого юнита. Режим 1 — клетка
                             * не исследуется (путь огибает толпу); режим 2 — исследуется
                             * со штрафом occ_shared_pen() к метрике клетки. Дефолт (0):
                             * ни вызова, ни пропуска, ни штрафа, ветвь дословно прежняя. */
                            const int occ_mode = occ_shared_on();
                            const int occ_foreign =
                                (occ_mode != 0 && occ_shared_other(self, edi, ebp));
                            if (occ_mode == 1 && occ_foreign) {
                                ebx = LS(0x3c);   /* @0x45ccce перечитывание pos->x, без обхода */
                            } else {
                            GridLevel *grid;
                            GridCell *node2;
                            int p7;

                            grid = (GridLevel *)(INT_PTR)
                                *(const int *)((const unsigned char *)self->game_ctx + 8);  /* @0x45cc3c: ecx = grid */
                            ecx = (int)(INT_PTR)grid;
                            if (edi < 0) { s6_pfs_diag(PFS_R_SIGN); GATE_EXIT(PFS_R_SIGN, 0); return NULL; }  /* @0x45cc38/0x45cc3f JL */
                            if (ebp < 0) { s6_pfs_diag(PFS_R_SIGN); GATE_EXIT(PFS_R_SIGN, 0); return NULL; }  /* @0x45cc45/0x45cc47 JL */
                            /* FCOMPP ST0=size_x (FILD @0x45cc51) vs ST1=edi (FILD @0x45cc4d):
                             * маска 0x41 (C3|C0) → выход, когда size_x <= edi. */
                            if (!(grid->size_x > LS(0x30))) { s6_pfs_diag(PFS_R_BOUND); GATE_EXIT(PFS_R_BOUND, 0); return NULL; }  /* @0x45cc4d..0x45cc5b */
                            if (!(grid->size_y > LS(0x34))) { s6_pfs_diag(PFS_R_BOUND); GATE_EXIT(PFS_R_BOUND, 0); return NULL; }  /* @0x45cc61..0x45cc6f */
                            if (diag) {
                                tp = ld_now_us();
                            }
                            node2 = s6_grid_lookup(grid, edi, ebp);  /* @0x45cc77: x=inner, y=outer; ecx НЕ пишется
                                                                       * в 0x4570d0..0x457101 (ecx только читается
                                                                       * @0x4570d4/0x4570de/0x4570eb) → ecx = grid */
                            if (diag) {
                                InterlockedIncrement(&g_e0p_grid);
                                InterlockedAdd64(&g_e0p_grid_us, ld_now_us() - tp);
                                tp = -1;
                            }
                            if (node2 == NULL) { s6_pfs_diag(PFS_R_GRID); GATE_EXIT(PFS_R_GRID, 0); return NULL; }  /* @0x45cc80 */
                            /* ESP=Y после add $8 @0x45cc9f; аргументы @0x45cc86..0x45ccc1:
                             * arg7 = MAX(|LS(0x1c)|,|LS(0x28)|) = адрес &spec[0x10] — FIDIV-делитель @0x45c460;
                             * arg3=arg6=&LS(0x18), arg4=&LS(0x50), arg5=[cb40 arg2] → min-слот = &pos[0];
                             * харнесс держит min=LS(0x14), p3=&out_metric: запись в восстановленный
                             * LDPos меняет identity leaf-значения. */
                            p7 = s6_imax(s6_iabs(LS(0x14)), (int)(INT_PTR)&spec->out_metric);
                            ecx = s6_iabs(LS(0x24));  /* @0x45c394: MOV 0x8(%ESP),%ECX = arg2 = |L[0x24]|, RET его сохраняет */
                            if (s6_path_tree_search(self, node2, (int)(INT_PTR)spec->pos,
                                                    (int)(INT_PTR)&spec->out_metric, (float *)&LS(0x48),
                                                    &LS(0x14), &LS(0x18),
                                                    p7) == NULL) {      /* @0x45ccc1 */
                                s6_pfs_diag(PFS_R_RECUR);
                                GATE_EXIT(PFS_R_RECUR, 0);
                                return NULL;                          /* @0x45ccce JE 0x45cd75 */
                            }
                            /* @0x45c3a0 clobbers ecx; остаточный ecx не наблюдаем (последнее значение ecx внутри
                             * 0x45c3e0..0x45c417 зависит от обхода) — консервативно сохраняем ecx=grid. Все
                             * продолжения проходят перечитывание @0x45cd04, где native сеет ecx слотом L[0x20]. */
                            if (occ_mode == 2 && occ_foreign) {
                                /* LDHOOK_OCC_SHARED=2: метрика клетки другого юнита
                                 * дороже на occ_shared_pen() — хвост 0x45ccd2 сравнивает
                                 * штрафную LS(0x14), путь выбирает свободную клетку. */
                                s6_bits_set(&LS(0x14), s6_bits_get(LS(0x14)) + occ_shared_pen());
                            }
                            ebx = LS(0x3c);                           /* @0x45ccce перечитывание pos->x */
                            }
                        }
                        /* хвост 0x45ccd2 (общий обеим ветвям head-проверки): */
                        if (s6_bits_get(LS(0x2c)) < s6_bits_get(LS(0x14))) {  /* JP(маска 0x5) NOT taken ⟺ a<b */
                            LS(0x2c) = LS(0x14);                      /* @0x45cce5 */
                        }
                        if (s6_bits_get(LS(0x10)) > s6_bits_get(LS(0x18))) {  /* JNE(маска 0x41) NOT taken ⟺ a>b строго */
                            LS(0x10) = LS(0x18);                      /* @0x45ccfc */
                        }
                    }
                    /* внутренний хвост 0x45cd00: */
                    ecx = LS(0x20);                                 /* @0x45cd04: native перечитывает посев центра
                                                                     * КАЖДЫЙ виток — все продолжения (0x45cbf0-капа
                                                                     * @0x45cd19 и внешний хвост @0x45cd1f) видят L[0x20] */
                    LS(0x1c) = LS(0x1c) + 1;                          /* @0x45cd09 */
                    edi = edi + 1;                                    /* @0x45cd08 */
                    LS(0x30) = edi;                                   /* @0x45cd15 */
                    if (edi > ebx + self->y_radius) {                 /* @0x45cd11..0x45cd19 JLE → continue */
                        break;                                        /* → внешний хвост */
                    }
                }
                /* внешний хвост 0x45cd1f: */
                ebp = ebp + 1;                                        /* @0x45cd26 */
                LS(0x24) = LS(0x24) + 1;                              /* @0x45cd27/0x45cd30 */
                LS(0x34) = ebp;                                       /* @0x45cd2c */
                if (ebp > ecx + self->x_radius) {                     /* @0x45cd2a/0x45cd34 JLE → continue */
                    break;
                }
                }                         /* внешний for(;;) */
            }
        }
    }

    /* Финальный гейт 0x45cd3e..0x45cd73: первый FCOMPS (world[0x120] vs 0.0f
     * @0x45cd4d) мёртв — его флаги перекрывает второй FNSTSW (@0x45cd5e).
     * Успех ⟺ JP(маска 0x5, FCOMPS(out, L[0x10]) @0x45cd57) — «не (out < min)»,
     * ИЛИ JNE(маска 0x41, FCOMPS(final_threshold+out, L[0x10]) @0x45cd6a) —
     * «thr+out <= min». min = bits(L[0x10]), out = spec->out_metric. Возврат
     * L[0x40] (@0x45cd3a). */
    {
        float outv = spec->out_metric;
        float minv = s6_bits_get(LS(0x10));

        if (!(outv < minv) || (self->final_threshold + outv <= minv)) {
            s6_pfs_diag(PFS_R_OK);
            GATE_EXIT(PFS_R_OK, 1);
            return (void *)(INT_PTR)LS(0x40);
        }
    }
    s6_pfs_diag(PFS_R_GATE);
    GATE_EXIT(PFS_R_GATE, 0);
    return NULL;                          /* @0x45cd75: XOR EAX,EAX */

#undef LS
}

void *s6_path_search(BattleAI *self, PathState *spec)
{
    LD_CRASH_SCOPE(4);           /* LDHOOK_CRASHLOG: stage 4 (no-op off) */
#ifdef BATTLE_REPLAY
    if (g_replay_override) {
        return replay_path_next();        /* A/B харнесса: результат из захвата */
    }
    {
        int spec_in[22], spec_out[22];   /* PathState = 0x58 = 22 dword (structs.h) */
        void *r;

        memset(g_gate, 0, sizeof(g_gate));        /* слоты на каждый вызов */
        memcpy(spec_in, spec, sizeof(spec_in));   /* состояние cb40 на входе */
        replay_apply_chain_state(replay_gate_index());  /* v8: состояние цепочки */
        r = s6_path_search_body(self, spec);
        memcpy(spec_out, spec, sizeof(spec_out)); /* состояние на выходе */
        replay_note_gate_state(spec_in, spec_out, g_gate);
        r = replay_note_path_res(r);      /* v5: cb40-результат + identity-носитель */
        return r;
    }
#else
    if (!e0_diag_on()) {
        return s6_path_search_body(self, spec);   /* env не задан — путь побайтно прежний */
    }
    {
        void *r;

        InterlockedIncrement(&g_e0p_calls);
        InterlockedIncrement(&g_e0p_depth);
        e0path_repeat_note(spec->pos);    /* ключ (cell, target) таблицы тика;
                                           * spec->pos только как ключ, не разыменовывается */
        r = s6_path_search_body(self, spec);
        if (r == NULL) {
            InterlockedIncrement(&g_e0p_null);
        }
        g_e0p_loop = 0;                   /* grid-цикл закрыт: все выходы тела */
        InterlockedDecrement(&g_e0p_depth);
        return r;
    }
#endif
}
