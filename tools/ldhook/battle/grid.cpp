/* battle/grid.cpp — доступ к сетке pathfinding'а: faithful-порты FUN_00456ed0,
 * FUN_004570d0, FUN_00456a70 (PLAN_BATTLE_AI_CPP.md, п. 2.1).
 * Дизасм — артефакт разведки re/battle_ai_disasm3.md п. 1.1.13 (Ghidra на
 * baseline LETHALDREAMS.EXE.pre4, imagebase 0x400000).
 *
 * Решения по артефакту:
 * - FUN_004570d0 — только ПОЛУЧАЕТ узел сетки: она зовёт исключительно
 *   FUN_00456ed0 и не вызывает allocation-функций. Создание узла — в
 *   FUN_0045dd70 (CALL 0x45c2e0 @0x45df2c), а не здесь.
 * - Receiver FUN_00456ed0 = тот же grid root, что у FUN_004570d0 (ECX не
 *   меняется: 0x4570eb задаёт ESI, CALL @0x4570f5; в FUN_0045dd70 ECX =
 *   [ctx+0x08] с 0x45dee4).
 * - Тела трёх функций (35/52/14 байт) без SEH-фреймов: единственный ret-путь,
 *   разыменования оригинала не guarded. Порты без __try/__except — guard
 *   добавил бы поведение, которого в оригинале нет.
 */
#include <windows.h>

#include "../ldhook_internal.h"
#include "battle.h"

#ifdef BATTLE_REPLAY
extern "C" int g_replay_override;
extern "C" void *replay_grid_cell(int x, int y);
#endif

/* FUN_00456ed0 (0x456ed0..0x456ef2, 35 байт) — ячейка сетки по индексу.
 * ABI: int __thiscall(GridLevel *level, int idx), RET 4; ECX = level.
 *   0x456ed4/0x456ed6: idx < 0 (знаковой JL) → 0 (0x456ee3: XOR EAX,EAX);
 *   0x456ed8..0x456ee1: спуск по [level+0x1c], пока следующий уровень не NULL;
 *   0x456ee8..0x456eee: leaf->[0x34] + idx*0x34. Проверки cells == NULL в
 *   оригинале нет: при пустом листе возвращается cells + idx*0x34.
 * Возврат: GridCell*, при idx < 0 — NULL. */
GridCell *s6_grid_cell(GridLevel *level, int index)
{
    GridLevel *leaf = level;

    if (index < 0) {
        return NULL;   /* 0x456ed6 JL → 0x456ee3 */
    }
    while (leaf->next_level != NULL) {   /* 0x456ed8..0x456ee1 */
        leaf = leaf->next_level;
    }
    return (GridCell *)((unsigned char *)leaf->cells +
                        index * S6_GRID_CELL_SIZE);   /* 0x456eeb IMUL, 0x456eee ADD */
}

/* FUN_004570d0 (0x4570d0..0x457103, 52 байта) — getter узла сетки по (x, y).
 * ABI: int __thiscall(GridLevel *grid, int x, int y), RET 8; ECX = grid.
 * Порядок проверок (знаковые JGE/JL, размеры [grid+0x2c]/[grid+0x30] — int):
 *   0x4570d4/0x4570d8: x >= size_x → 0;
 *   0x4570de/0x4570e1: y >= size_y → 0;
 *   0x4570e3/0x4570e5: x < 0 → 0; 0x4570e7/0x4570e9: y < 0 → 0;
 *   0x4570eb..0x4570f1: index = [grid+0x38][x][y] (массив рядов, row[x][y]);
 *   0x4570f4/0x4570f5: CALL 0x456ed0 с ECX = grid.
 * Узел не создаётся (см. решение в шапке). */
GridCell *s6_grid_lookup(GridLevel *grid, int x, int y)
{
    int *row;
    int index;

#ifdef BATTLE_REPLAY
    if (g_replay_override) {
        return (GridCell *)replay_grid_cell(x, y);
    }
#endif
    if (x >= grid->size_x) {   /* 0x4570d8 JGE */
        return NULL;
    }
    if (y >= grid->size_y) {   /* 0x4570e1 JGE */
        return NULL;
    }
    if (x < 0) {               /* 0x4570e5 JL */
        return NULL;
    }
    if (y < 0) {               /* 0x4570e9 JL */
        return NULL;
    }

    row = grid->rows[x];       /* 0x4570ee: [grid+0x38] + x*4 */
    index = row[y];            /* 0x4570f1: [row + y*4] */
    return s6_grid_cell(grid, index);
}

/* FUN_00456a70 (0x456a70..0x456a7d, 14 байт) — последний узел цепочки +0x28.
 * ABI: int __fastcall(node), ECX = node; только RET (ret n нет).
 *   0x456a72..0x456a7b: edx = [eax+0x28]; если NULL → возврат eax,
 *   иначе eax = edx и повтор.
 * +0x28 — link цепочки обхода (потребитель FUN_0045c3a0 @0x45c3d0), не левый сын
 * RB-дерева; поле GridCell.chain_next именовано пунктом 2.3. */
GridCell *s6_chain_last(GridCell *node)
{
    GridCell *cur = node;

    for (;;) {
        if (cur->chain_next == NULL) {
            return cur;   /* 0x456a77 JZ → 0x456a7d */
        }
        cur = cur->chain_next;   /* 0x456a79/0x456a7b */
    }
}

/* FUN_00456b20 (0x456b20..0x456b50, 49 байт) — масштабная запись позиции уровня
 * сетки (п. 2.5, вердикт «порт» — инвентарь 1.4). Оригинал: `int __thiscall
 * (node, float *out)` RET 4. Receiver = LDPos* (FUN_0045c5e0 @0x45c61b): уровень
 * берётся из [pos+0x1c] как лист, БЕЗ спуска по цепочке +0x1c.
 *   0x456b21: EAX=[ECX+0x1c] — лист; 0x456b24: FLDS [EAX+0x24] — ST2-операнд
 *   второй записи; 0x456b2a: FLDS [EAX+0x28] — ST1-операнд (scale_mul);
 *   0x456b27: EDX=[ECX+0x10] → slot ESP+0 (FILD-операнд @0x456b40, int-значение).
 *   0x456b31..0x456b3e: FILD [ECX+0xc]; FMUL [EAX+0x28]; FADD [EAX+0x20];
 *   FSTP [EAX] → out[0] = float (x*mul + add).
 *   0x456b40..0x456b48: FILD slot(y); FMUL ST1; FADD ST0,ST2 (ST2 = [EAX+0x24]);
 *   FSTP [EAX+4] → out[1] = float (y*mul + scale_load).
 * EAX на выходе = out и читается вызывающим (@0x45c62d/0x45c668) — порт
 * возвращает out; порядок операций соблюдён (plain double, PC=53 — п. 1.6).
 * SEH у оригинала нет: разыменования идут под __try вызывающего. */
float *s6_grid_scale_compute(LDPos *pos, float *out)
{
    GridLevel *level = pos->level;
    double mul, add, load, v;

    load = level->scale_load;   /* FLDS [EAX+0x24] @0x456b24 — ST2 @0x456b46 */
    mul = level->scale_mul;     /* FLDS [EAX+0x28] @0x456b2a; FMUL @0x456b34; ST1 @0x456b44 */
    add = level->scale_add;     /* FADD-операнд @0x456b37 (ST2-копия не участвует) */

    v = (double)pos->x * mul;   /* FILD [ECX+0xc] → FMUL @0x456b31..0x456b34 */
    v = v + add;                /* FADD [EAX+0x20] @0x456b37 */
    out[0] = (float)v;          /* FSTP [EAX] @0x456b3e — float, не int */

    v = (double)pos->y * mul;   /* FILD slot @0x456b40 (значение [ECX+0x10] @0x456b27) */
    v = v + load;               /* FADD ST0,ST2 @0x456b46 — scale_load, НЕ scale_add */
    out[1] = (float)v;          /* FSTP [EAX+4] @0x456b48 */
    return out;
}
