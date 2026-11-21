/* battle/native.cpp — нативные обёртки игровых функций (PLAN_BATTLE_AI_CPP.md,
 * инвариант 3: «голых» VA в логике нет).
 *
 *   native::fn(VA) = base + (VA - 0x400000), base = GetModuleHandleA(NULL).
 *   EXE без ASLR (imagebase 0x400000, docs/STATE.md), base+RVA валиден как
 *   абсолютный адрес.
 *
 * Обёртки листьев (не портируются — «Границы порта»): std::map-вставки и
 * аллокатор узлов. Порт FUN_00434bc0 (lower_bound) и FUN_0047f800 (next) уже
 * есть в path.cpp (он не аллоцирует) — обёртки ниже его не заменяют, это
 * инфраструктура для Фаз 2–5. ABI (число stack-аргументов и ret n) уточняется
 * п. 1.1; стек восстанавливается сохранением esp. Обёртки, ещё не подключённые,
 * задокументированы и не вызываются.
 */
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"

namespace native {

static unsigned char *g_base = NULL;

/* Счётник перехваченных исключений (AV) во всех obёртках ниже. `__except`
 * с фильтром-счётником меняет только подсчёт: handler тот же
 * (EXCEPTION_EXECUTE_HANDLER), поток исполнения — идентичен.
 * Читатели (battle/target.cpp) сравнивают дельты до/после вызова. */
static volatile LONG g_av = 0;

int av_filter(void)
{
    InterlockedIncrement(&g_av);
    return EXCEPTION_EXECUTE_HANDLER;
}

long av_count(void)
{
    return (long)g_av;
}

void init(void)
{
    g_base = (unsigned char *)GetModuleHandleA(NULL);
}

unsigned char *base(void)
{
    if (g_base == NULL) {
        init();
    }
    return g_base;
}

/* VA → адрес в памяти процесса. NULL, если base не получен. */
void *fn(unsigned int va)
{
    unsigned char *b = base();

    if (b == NULL) {
        return NULL;
    }
    return (void *)(b + (va - 0x400000));
}

/* --- именованные обёртки листьев ------------------------------------ */

/* FUN_00434bc0 — std::map<uint32,V>::lower_bound(key) (thiscall: ecx = map,
 * args = key + hint). Порт s6_map_lower_bound остаётся; обёртка подключается
 * Фазой 2.4. Вызов — inline asm (MSVC не даёт thiscall-указателя).
 * ABI (число stack-аргументов и ret n) уточняется п. 1.1; стек восстанавливается
 * сохранением esp: pushad фиксирует esp «до аргументов», после call esp =
 * сохранённому значению, поэтому ret / ret n / очистка вызывающим безопасны. */
TreeNode *lower_bound(void *map, unsigned int key, void *hint)
{
    void *addr = fn(0x434bc0);
    TreeNode *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, map
            push hint
            push key
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_00434bc0 — форма вызова FUN_0045cb40 (см. native.h): ecx = map,
 * push &key, push &out, RET 8; результат пишется native в *out_slot.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
TreeNode *lower_bound_slots(void *map, int *out_slot, int *key_slot)
{
    void *addr = fn(0x434bc0);
    TreeNode *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, map
            push key_slot
            push out_slot
            mov eax, addr
            call eax
            mov res, eax                   /* eax = out-указатель, читается до esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_0047f800 — in-order successor по std::_Tree (thiscall, ecx = узел).
 * Порт s6_tree_next остаётся (он не аллоцирует); обёртка — инфраструктура
 * Фаз 2–5.
 * ABI (число stack-аргументов и ret n) уточняется п. 1.1; стек восстанавливается
 * сохранением esp (см. lower_bound). */
TreeNode *tree_next(TreeNode *node)
{
    void *addr = fn(0x47f800);
    TreeNode *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad                         /* esp «до вызова», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, node
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_00449160 (31 байт) — тип (статус) узла в дереве мира.
 * ABI: int __fastcall(int key), ECX = world, RET 4 (листинг 0x44916b ADD ECX,0x104
 * — дерево лежит по world+0x104; калл — 0x4359b0). Возврат: [node+0x10] (int),
 * 0 — узла нет. Ключ — (Y<<16)|X. Потребитель: FUN_0045c3a0 @0x45c3eb
 * (решение 1.4: функция нативная, не портируется).
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
int tree_lookup(void *world, unsigned int key)
{
    void *addr = fn(0x449160);
    int res = 0;

    if (addr == NULL) {
        return 0;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, world                 /* ECX = world (receiver по fastcall) */
            push key
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* FUN_0045dd00 — регистрация занятого слота (this = BattleAI, arg = объект).
 * Вставка в родные std::map не портируется (правило инвариантности кучи);
 * вызов подключается Фазой 2.6.
 * ABI (число stack-аргументов и ret n) уточняется п. 1.1; стек восстанавливается
 * сохранением esp (см. lower_bound). */
void register_occupied_slot(void *ai, LDObject *obj)
{
    void *addr = fn(0x45dd00);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
            push obj
            mov eax, addr
            call eax
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
    }
}

/* Виртуальный вызов `MOV EDX,[receiver]; CALL *[EDX+off]` (ECX = receiver), без
 * стек-аргументов (форма @0x45c5ef/0x45c63b, байты FF 52 40 / FF 52 48 —
 * КОСВЕННЫЙ вызов через память vtable). Оффсет vtable задаётся вызывающим
 * (см. native.h). ABI (число stack-аргументов и ret n) — стек восстанавливается
 * сохранением esp (см. lower_bound). */
int vcall0(void *receiver, unsigned int voffset)
{
    int res = 0;

    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, receiver
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov edi, voffset
            mov eax, [edx+edi]             /* CALL *[vtable+off] (не vtable+off!) */
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* Виртуальный вызов той же формы с одним int-аргументом (форма @0x45c603:
 * `MOV [ESP+0xC],result1` + `CALL *0x40(EDX)`). ФОРМА — МОДЕЛЬНОЕ ДОПУЩЕНИЕ:
 * листинг не фиксирует число аргументов; возможная альтернатива — 0-аргументный
 * thiscall с a1 в spilled-слоте (`MOV EDI,[ESP+0x2C]` @0x45c617 читает arg1-слот).
 * Вызов сохраняется в этой форме до пиннинга по live-захвату. */
int vcall1(void *receiver, unsigned int voffset, int arg)
{
    int res = 0;

    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, receiver
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov edi, voffset
            mov eax, [edx+edi]             /* CALL *[vtable+off] */
            push arg
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* FUN_0044bbf0 — grid_query(world, arg1, arg2, arg3, arg4) (инвентарь 1.4 §1.10,
 * вердикт «нативно»): листинг 0x44bbf0..0x44bcef (SEH-фрейм `PUSH -1; PUSH
 * 0x4e6248` @0x44bbf0..0x44bbfe, 89 инстр., калли 0x42d9c0/0x44b260, vtable
 * [world]+0x40 ×3). ECX = world (0x45c676..0x45c678), ABI: `ADD ESP,0x28;
 * RET 0x10` @0x44bceb/0x44bcee — 4 стек-аргумента. На site 0x45c67b вызывающий
 * пушей не кладёт: лист читает остатки фрейма FUN_0045c5e0. Аргументы — см.
 * native.h; port кладёт их явно (младший первым = последний push).
 * Риск: лист зовёт _free (0x4d0e16 @0x44bcbd/0x44bcd1) на указателях из
 * аргументов — не-кучевое значение портит кучу игры (план, «Риски»).
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
int grid_query(void *world, int arg1, int arg2, int arg3, int arg4)
{
    void *addr = fn(0x44bbf0);
    int res = 0;

    if (addr == NULL) {
        return 0;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, world
            push arg4                      /* старший аргумент — первый push */
            push arg3
            push arg2
            push arg1                      /* младший: [ESP+4] на входе листа */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* IAT 0x4ec26c — PtInRect(rect, pt): форма site 0x45c6c4..0x45c6ca (PUSH y,
 * PUSH x, PUSH rect; ECX = rect). struct POINT (8 байт) кладётся как (y, x) —
 * x младше по адресу, как в оригинале. Возврат — BOOL в EAX.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
int pt_in_rect(const int *rect, int pt_x, int pt_y)
{
    void *addr = fn(0x4ec26c);
    int res = 0;

    if (addr == NULL) {
        return 0;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, rect                  /* LEA ECX,[EBP+0x18] @0x45c6c6 */
            push pt_y                      /* POINT.y = game-X (+0x0c) */
            push pt_x                      /* POINT.x = game-Y (+0x10, младше по адресу) */
            push rect                      /* RECT* (last push = [ESP+4]) */
            mov eax, addr
            mov eax, [eax]                 /* CALL *0x4ec26c — через IAT-слот */
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* FUN_004cfa11 — игровой operator new (0x4cfa11..0x4cfa34, 36 байт).
 * Листинг: 0x4cfa11 PUSH ESI; 0x4cfa16 JMP 0x4cfa28; 0x4cfa28 PUSH ESI;
 * CALL 0x4d138a (ядро new с new-hook); 0x4cfa2e TEST EAX,EAX; при 0 —
 * повтор через handler 0x4d0863; 0x4cfa33 POP ESI; RET.
 * ABI: cdecl, один аргумент (размер), очистка вызывающим (FUN_0045c2e0 делает
 * ADD ESP,4 @0x45c2eb), результат — указатель в EAX.
 * Это единственный разрешённый аллокатор для игровых структур: malloc из .pyd
 * порол бы кучу игры (план, «Риски»; defect C аудита). */
typedef void *(*PFN_operator_new)(unsigned int size);

void *operator_new(unsigned int size)
{
    void *addr = fn(0x4cfa11);
    void *p = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        p = ((PFN_operator_new)addr)(size);
    } __except (native::av_filter()) {
        p = NULL;
    }
    return p;
}

/* FUN_004d0e16 — игровой _free (thunk: JMP 0x4d0da5). Именно его зовёт
 * deallocator FUN_0047ac00 (CALL 0x4d0e16 @0x47ae9c) при освобождении узлов map.
 * ABI: cdecl, один аргумент (указатель), очистка вызывающим. Парная функция к
 * operator_new выше: память, взятую 0x4cfa11, возвращать только через 0x4d0e16. */
typedef void (*PFN_free)(void *ptr);

void deallocate(void *ptr)
{
    void *addr = fn(0x4d0e16);

    if (addr == NULL) {
        return;
    }
    __try {
        ((PFN_free)addr)(ptr);
    } __except (native::av_filter()) {
    }
}

} /* namespace native */
