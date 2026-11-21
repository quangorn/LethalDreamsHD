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

/* --- лента `this+0x3c` и сброс AI (Фаза 4.1b, сайты FUN_0045eee0 B4/B5) --- */

/* FUN_0045b6a0 (0x45b6a0..0x45b6c9, 42 байта) — очистка ленты. Листинг:
 * guard `MOV EAX,[band+0x20]` @0x45b6a6 (`jz` @0x45b6a8), `_free` CALL 0x4d0e16
 * @0x45b6ab на этом указателе, затем нули `+0x20` @0x45b6b3, `+0x24` @0x45b6ba,
 * `+0x28` @0x45b6c1. Имя подтверждено телом. __thiscall, ECX = &band
 * (= this+0x3c, `LEA EDI,[ESI+0x3c]` @0x45efb1), 0 стек-аргументов
 * (`RET` @0x45b6c9). Тело не портировано (инвентарь §5 «нативно»); сайт
 * @0x45efb6.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void band_free(void *band)
{
    void *addr = fn(0x45b6a0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до вызова», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, band
            mov eax, addr
            call eax
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045af10 (4 инструкции, 0x45af10..0x45af14) — единственное действие:
 * `MOVB $1,0x4(ECX)` @0x45af10 (младший байт dword-поля +0x4), `RET` @0x45af14.
 * Назначение НЕ подтверждено телом: «dirty-флаг» — вывод, не факт. По
 * потребителю @0x45af20 `+0x4` — begin-указатель ленты (`MOV EAX,[ECX+4]`,
 * gate `TEST EAX,EAX`, затем `([+0x8]−[+0x4])/24` через 0x2aaaaaab); таким
 * образом запись задевает младший байт этого поля. Имя нейтральное, по VA.
 * __thiscall, ECX = &band, 0 стек-аргументов. Сайт @0x45efbd.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void native_45af10(void *band)
{
    void *addr = fn(0x45af10);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до вызова», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, band
            mov eax, addr
            call eax
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045e9c0 (SEH-пролог 0x45e9c0..0x45e9d5; конец тела в этом суб-шаге не
 * проверялся) — перестройка ленты `this+0x3c`: SEH-фрейм
 * (`push -1; mov fs:0; push 0x4e70c8; mov esp,fs:0; sub 0x3c`), далее
 * add+sort+x87-интерполяция (инвентарь §5, вердикт «нативно»). __thiscall,
 * ECX = BattleAI, 2 стек-аргумента (`ret $8`): arg1 — узел (`push %edi`
 * @0x45f3cd — последний push), arg2 — int-биты float `S(0x1c)` (`push %edx`
 * @0x45f3cc). Возврат не используется (EAX перезаписан @0x45f3d5).
 * ABI (число стек-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void band_rebuild(void *ai, void *node, int bits)
{
    void *addr = fn(0x45e9c0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
            push bits                      /* arg2 — старший, первый push */
            push node                      /* arg1 — младший, [ESP+4] на входе */
            mov eax, addr
            call eax
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_00458050 (0x458050..0x4581f4) — grid-якорь. Пролог `sub $0x8` + push
 * ebx/ebp/esi/edi @0x458050..0x45805b; ECX = ctx[8] (@0x45f103/0x45f105),
 * 6 стек-аргументов (`ret $0x18` @0x45814e/0x45815a). Чтения аргументов:
 * `flds 0x1c(%esp)`@0x45805f = arg1 (float), `flds 0x24(%esp)`@0x45807c = arg3
 * (float), `mov 0x28(%esp),%ebp`@0x4580cb = arg4 (указатель), `mov 0x2c(%esp),
 * %eax`@0x4580f1/@0x45815d = arg5 (int-слот байт-флага: `movb $1,(%eax)`
 * @0x4580fd, `movb $0,(%eax)`@0x458161), `mov 0x30(%esp),%ecx`@0x4580e3/
 * @0x45812e = arg6 (world, receiver 0x449160). Границы [grid+0x2c]/[grid+0x30]
 * (@0x4580a7/0x4580bb), масштаб [grid+0x28]. Имя по телу: возвращает узел
 * якоря (EAX, NULL при промахе). Тело не портировано (инвентарь §5), сайт
 * @0x45f10c.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    void *addr = fn(0x458050);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, grid
            push world                     /* arg6 — старший, первый push */
            push flag_slot                 /* arg5 */
            push a4                        /* arg4 */
            push a3
            push a2
            push a1                        /* arg1 — младший, [ESP+4] на входе */
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

/* IAT 0x4ec268 — SetRectEmpty(rect), форма site @0x45c34c (`call *0x4ec268`,
 * байты FF 15 68 C2 4E 00 — вызов через IAT-слот, поэтому `MOV EAX,[addr]`
 * перед CALL). В FUN_0045c330 аргумент — `&this+0x18` (`LEA EAX,[ESI+0x18]`
 * @0x45c336, PUSH @0x45c339), т.е. canvas_rect. Возврат BOOL в EAX; в порту
 * s6_ai_reset не используется.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
int set_rect_empty(void *rect)
{
    void *addr = fn(0x4ec268);
    int res = 0;

    if (addr == NULL) {
        return 0;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, rect
            push rect                      /* SetRectEmpty(rect) — 1 аргумент */
            mov eax, addr
            mov eax, [eax]                 /* CALL *0x4ec268 — через IAT-слот */
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

/* FUN_0045ca70 (0x45ca70..0x45cb3c) — очистка карт: обход от `[this+0x6c]`
 * (`MOV ECX,[EBX+0x6c]` @0x45ca74; `_free` узлов CALL 0x4d0e16 @0x45ca88; шаг
 * `CALL 0x47f800` @0x45ca94) для map1 и map2. __thiscall, ECX = BattleAI,
 * 0 стек-аргументов (`RET` @0x45cb3c). Тело не портировано (инвентарь §5
 * «нативно»); сайт @0x45efd7.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void clear_maps(void *ai)
{
    void *addr = fn(0x45ca70);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до вызова», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
            mov eax, addr
            call eax
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045c8c0 (0x45c8c0..0x45c995) — пересчёт search-параметров AI: радиусы
 * `+0x14`@0x45c8f7 и `+0x10`@0x45c8fa (ftol от `(размер + 0.5)/grid[0x28]`;
 * `faddl 0x4edf20` @0x45c8e4, `fdivs` @0x45c8ef), порог `+0x08`@0x45c933,
 * `+0x0c` = `fptan(world[0x124])·grid[0x28]` @0x45c939..0x45c94d, RECT `+0x18`
 * (SetRectEmpty @0x45c950, затем `call *0x4ec218` @0x45c989). Имя по телу
 * (порт: радиусы/RECT/порог). __thiscall, ECX = BattleAI, 0 стек-аргументов
 * (`add $0x3c,%esp; ret` @0x45c992/0x45c995). Тело не портировано; сайт @0x45f0c7.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void search_params_update(void *ai)
{
    void *addr = fn(0x45c8c0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad                         /* esp «до вызова», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
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
