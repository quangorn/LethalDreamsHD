#ifndef BATTLE_NATIVE_H
#define BATTLE_NATIVE_H

#include "structs.h"

/* Нативные обёртки игровых функций (PLAN_BATTLE_AI_CPP.md, инвариант 3):
 * native::fn(VA) = base + (VA - 0x400000), base = GetModuleHandleA(NULL).
 * Определяется в battle/native.cpp. */
namespace native {

void init(void);                     /* кэш base (initldhook) */
unsigned char *base(void);
void *fn(unsigned int va);           /* NULL, если base не получен */

TreeNode *lower_bound(void *map, unsigned int key, void *hint);   /* FUN_00434bc0 */

/* FUN_00434bc0 — форма вызова FUN_0045cb40: ecx = map (this+0x74),
 * arg1 = &out-слота (последний push), arg2 = &key-слота. Ключ читается из
 * arg2 (`MOV ESI,[EDI]` @0x434bd7); результат пишется в *arg1 (0x434c10/0x434c27),
 * EAX = arg1. RET 8. */
TreeNode *lower_bound_slots(void *map, int *out_slot, int *key_slot);
TreeNode *tree_next(TreeNode *node);                              /* FUN_0047f800 */
int tree_lookup(void *world, unsigned int key);                   /* FUN_00449160 (тип узла) */
void register_occupied_slot(void *ai, LDObject *obj);            /* FUN_0045dd00 */

/* Виртуальные вызовы вида `MOV EDX,[receiver]; CALL *[EDX+off]` (ECX = receiver).
 * Формы по листингу FUN_0045c5e0: без стек-аргументов (`CALL *0x40(EDX)` @0x45c5ef,
 * `CALL *0x48(EDX)` @0x45c63b) и с одним int-аргументом (@0x45c603). Оффсет vtable
 * documented at call site: +0x40 = snapshot-геттер world (инвентарь 1.4 §1.5
 * `player_get`, тот же оффсет), +0x48 = геттер scale результата (45c63b; результат
 * читается как float [obj+0x34] @0x45c644). Возврат — EAX (указатель/int),
 * читается до восстановления esp. Форма vcall1 (один int-аргумент) — модельное
 * допущение: листинг @0x45c603 не фиксирует число аргументов. */
int vcall0(void *receiver, unsigned int voffset);
int vcall1(void *receiver, unsigned int voffset, int arg);

/* Счётник исключений во всех native-обёртках (intercepted AV). Читающий сравнивает
 * дельты до/после вызова; increment поведенчески нейтрален. */
long av_count(void);
int av_filter(void);               /* __except-фильтр: +1 к счётнику, EXECUTE_HANDLER */

/* FUN_0044bbf0 — grid_query (состояние+гейт, инвентарь 1.4 §1.10, вердикт
 * «нативно»): листинг 0x44bbf0..0x44bcef. __thiscall, ECX = world
 * (site 0x45c67b: `MOV EAX,[ECX]`; `MOV ECX,0x4(EAX)` @0x45c676..0x45c678).
 * ABI: 4 стек-аргумента (`ADD ESP,0x28; RET 0x10` @0x44bceb..0x44bcee): лист
 * читает их из остатков фрейма вызывающего (`MOV ECX,[ESP+0x44]` ×3
 * @0x44bc2a..0x44bc36, `MOV EBP,[ESP+0x24]` @0x44bc6d, `MOV ESI,[ESP+0x20]`
 * @0x44bc71). На site 0x45c67b ESP = S-0x38 (S = ESP на входе FUN_0045c5e0),
 * аргументы в [S-0x38..S-0x2c] (младший первым): arg1 = int-биты chain2
 * (@0x45c674 FSTP m32 [EBX]), arg2 = int-биты arg1-хранилища (@0x45c66e
 * MOV [EBX+4]), arg3 = int-биты chain1 (@0x45c671 MOV [EBX+8]), arg4 = 0
 * (остаток `PUSH 0x0` @0x45c61d в S-0x2c).
 * Риск: лист вызывает _free (0x4d0e16 @0x44bcbd/0x44bcd1) на указателях из
 * аргументов — не-кучевое значение портит кучу игры (план, «Риски»).
 * Возврат — int (аккумулятор EBX @0x44bcda), pass-through в FUN_0045c5e0. */
int grid_query(void *world, int arg1, int arg2, int arg3, int arg4);

/* IAT 0x4ec26c — PtInRect. Site `0x45c6c4..0x45c6ca`: PUSH pt_y; PUSH pt_x;
 * PUSH rect, ECX = rect (`LEA ECX,[EBP+0x18]`); вызов через IAT. struct POINT
 * кладётся послойно: младший по адресу (pt_x) — последний push, т.е. game-Y
 * (pos[0x10]) → pt_x, game-X (pos[0x0c]) → pt_y. Возврат — BOOL в EAX. */
int pt_in_rect(const int *rect, int pt_x, int pt_y);

/* Игровая куча (п. 2.2): путь аллокации игровых структур — только через эти
 * обёртки; malloc/free из .pyd запрещены (план, «Риски»). Оба — cdecl с одним
 * аргументом, очистка вызывающим (листинг 0x4cfa11: вызывающий ADD ESP,4
 * @0x45c2eb в FUN_0045c2e0). */
void *operator_new(unsigned int size);   /* FUN_004cfa11 (operator new) */
void deallocate(void *ptr);              /* FUN_004d0e16 (_free), пара к нему */

} /* namespace native */

#endif /* BATTLE_NATIVE_H */
