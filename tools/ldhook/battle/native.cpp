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
#include "capture.h"
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

    ld_crash_enter(9);                     /* LDHOOK_CRASHLOG: native wrapper */
    if (addr == NULL) {
        LD_CRASH_RET(NULL);
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
    LD_CRASH_RET(res);
}

/* FUN_00434bc0 — форма вызова FUN_0045cb40 (см. native.h): ecx = map,
 * push &key, push &out, RET 8; результат пишется native в *out_slot.
 * ABI (число stack-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound).
 * E0COST (9.d2d): при LDHOOK_E0_DIAG=1 время внутри обёртки суммируется в
 * g_e0c_lb_us (map2 lower_bound — и в dd70, и в cb40). env не задан →
 * один предсказуемый branch, ни записей, ни строк. */
TreeNode *lower_bound_slots(void *map, int *out_slot, int *key_slot)
{
    void *addr = fn(0x434bc0);
    TreeNode *res = NULL;
    LONGLONG t = e0_diag_on() ? ld_now_us() : -1;

    ld_crash_enter(9);
    if (addr == NULL) {
        LD_CRASH_RET(NULL);
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
    if (t >= 0) {
        LONGLONG d = ld_now_us() - t;
        InterlockedAdd64(&g_e0c_lb_us, d);
        if (e0_path_inside()) {           /* E0PATH (9.2a): lower_bound внутри 45cb40 */
            InterlockedIncrement(&g_e0p_lb);
            InterlockedAdd64(&g_e0p_lb_us, d);
        }
    }
    LD_CRASH_RET(res);
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x449160);
    int res = 0;

    if (addr == NULL) {
        LD_CRASH_RET(0);
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
    LD_CRASH_RET(res);
}

/* FUN_0045dd00 — регистрация занятого слота (this = BattleAI, arg = объект).
 * Вставка в родные std::map не портируется (правило инвариантности кучи);
 * вызов подключается Фазой 2.6.
 * ABI (число stack-аргументов и ret n) уточняется п. 1.1; стек восстанавливается
 * сохранением esp (см. lower_bound). */
void register_occupied_slot(void *ai, LDObject *obj)
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45dd00);

    if (addr == NULL) {
        LD_CRASH_RET_VOID();
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
    ld_crash_leave();
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45b6a0);

    if (addr == NULL) {
        LD_CRASH_RET_VOID();
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
    ld_crash_leave();
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

/* FUN_0045e0a0 — нативный поиск цели (только режим site6 OFF: порт eee0 вызывает
 * его вместо s6_target_search, диспетчер — battle/move.cpp 4.2). __thiscall,
 * ECX = BattleAI, 4 стек-аргумента (`ret $0x10` @0x45e16d/0x45e170):
 * arg1 filter, arg2 cmp_pos, arg3 float-биты таймаута, arg4 out_code
 * (порядок push @0x45f25d..0x45f260: out_code первым, filter последним).
 * Возврат — узел/NULL в EAX. Тело не портировано здесь (порт s6_target_search).
 * ABI (число стек-аргументов и ret n) — стек восстанавливается сохранением
 * esp (см. lower_bound). */
void *target_search(void *ai, int filter, void *cmp_pos, int arg3, int *out_code)
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45e0a0);
    void *res = NULL;

    if (addr == NULL) {
        LD_CRASH_RET(NULL);
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
            push out_code                  /* arg4 — старший, первый push */
            push arg3                      /* arg3 = float-биты таймаута */
            push cmp_pos                   /* arg2 */
            push filter                    /* arg1 — младший, [ESP+4] на входе */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* стек = сохранённый esp при любом ret n */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    LD_CRASH_RET(res);
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45e9c0);

    if (addr == NULL) {
        LD_CRASH_RET_VOID();
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
    ld_crash_leave();
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x458050);
    void *res = NULL;

    if (addr == NULL) {
        LD_CRASH_RET(NULL);
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
    /* v12: bdd0_leaf — ECX = tree (grid), args = [a1..a4, flag*, world] (все 6
     * стек-аргументов листа; float-позиция — битами, как их видит callee). */
    {
        int av[6];
        av[0] = a1;
        av[1] = a2;
        av[2] = a3;
        av[3] = a4;
        av[4] = (int)(size_t)flag_slot;
        av[5] = (int)(size_t)world;
        battle_capture_bdd0_leaf(0x458050, grid, av, 6, (int)(size_t)res, NULL, 0);
    }
    LD_CRASH_RET(res);
}

/* FUN_0045f430 (0x45f430..0x45f4e2) — ctor/reset объекта PathFinder.
 * __thiscall, ECX = this, 0 стек-аргументов (`ret` @0x45f4e2; пролог SEH
 * 0x45f430..0x45f448, тело — 45c260 по +0x3c, 47ab80 ×2 по +0x68/+0x74,
 * 45c330 по +0x0). sites: R12 B82 @0x44c74d (`lea 0xcc(%esp),%ecx`);
 * объект — приёмник 45e1d0 @0x44c8b6 (R14) и dtor 45e5e0 @0x44cad2 (R16).
 * Стек восстанавливается сохранением esp. */
void pathfinder_init(void *pf)
{
    void *addr = fn(0x45f430);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, pf
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045e5e0 (0x45e5e0..0x45e6ab) — dtor PathFinder: `45ca70(this)`, `_free`
 * блоков `[this]`/`[[this]+0xc]`, map-dtor `+0x74`/`+0x68` (45d990/45d8d0) и
 * `45b670(this+0x3c)`. __thiscall, ECX = this, 0 стек-аргументов (`ret`).
 * sites R16: 0x44cae4 (B100), 0x44cb00 (B101) — один объект кадра `frame+0xcc`
 * (ctor 45f430 @0x44c74d, приёмник 45e1d0 @0x44c8b6). */
void pathfinder_dtor(void *pf)
{
    void *addr = fn(0x45e5e0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, pf
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45ca70);

    if (addr == NULL) {
        LD_CRASH_RET_VOID();
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
    ld_crash_leave();
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45c8c0);

    if (addr == NULL) {
        LD_CRASH_RET_VOID();
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
    ld_crash_leave();
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x44bbf0);
    int res = 0;

    if (addr == NULL) {
        LD_CRASH_RET(0);
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
    /* v12: bdd0_leaf — ECX = world, args = [arg1..arg4] (4 стек-аргумента). */
    {
        int av[4];
        av[0] = arg1;
        av[1] = arg2;
        av[2] = arg3;
        av[3] = arg4;
        battle_capture_bdd0_leaf(0x44bbf0, world, av, 4, res, NULL, 0);
    }
    LD_CRASH_RET(res);
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
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x4cfa11);
    void *p = NULL;

    if (addr == NULL) {
        LD_CRASH_RET(NULL);
    }
    __try {
        p = ((PFN_operator_new)addr)(size);
    } __except (native::av_filter()) {
        p = NULL;
    }
    LD_CRASH_RET(p);
}

/* --- Фаза 5.1b: листья R1/R2 (FUN_0044bdd0) --- */

/* FUN_00442570 — аксессор состояния игры (thiscall, ecx = unit, 1 стек-арг,
 * `ret $0x4`). Возврат — result-объект. site 0x44be0e/0x44beac/0x44bf0b. */
void *game_state_get(void *unit, int flags)
{
    void *addr = fn(0x442570);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, unit
            push flags                     /* site: `push $0x0` */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* ret / ret n / очистка вызывающим — безразличны */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* vtable unit[+0x28] — world-notify(&snapshot32). Аргумент — 32 Б по значению:
 * `sub $0x20` + копия снимка в [esp] + `call *0x28(%edx)` (0x44be15..0x44be55),
 * callee снимает 0x20. site 0x44be55. */
void world_notify_snapshot(void *unit, const void *snapshot32)
{
    __try {
        __asm {
            pushad                         /* esp «до аргументов» */
            mov ebx, esp                   /* сохранённый esp (esi/edi/ecx — копирование) */
            mov ecx, unit
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov eax, [edx + 0x28]          /* CALL *[vtable+0x28] */
            sub esp, 0x20                  /* 32-байтовая структура по значению */
            mov edi, esp
            mov esi, snapshot32
            mov ecx, 8
            rep movsd                      /* 8 dword = 0x20 байт */
            mov ecx, unit
            call eax
            mov esp, ebx                   /* callee снял 0x20 — esp = сохранённый */
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045c730 — курсор ленты: цепочка `[[[ctx]+0xc]+0xc]+0x234` с null-
 * guard'ом на каждом шаге (любой NULL → 0), `ret`. Тривиальный аксессор без
 * side effects (battle_ai_leaves.md §1.4). sites 0x44c36f/0x44c37e. */
void *band_cursor(void *ai)
{
    void *addr = fn(0x45c730);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, ai
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    battle_capture_bdd0_leaf(0x45c730, ai, NULL, 0, (int)(size_t)res, NULL, 0);
    return res;
}

/* vtable unit[+0x2c] — float фазы в ST(0): callee оставляет ST(0), результат
 * снимается `fstp` в *out. site 0x44bf1e. */
void phase_time(void *unit, float *out_f)
{
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov eax, [edx + 0x2c]          /* CALL *[vtable+0x2c] */
            call eax
            mov edx, out_f
            fstp dword ptr [edx]           /* ST(0) → float */
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        *out_f = 0.0f;
    }
}

/* vtable unit[+0x40] — player-getter: ECX = unit, 1 стек-аргумент (в нативе
 * слот может не сниматься callee'ом — esp восстанавливается из сохранённого).
 * site 0x44bf40 / 0x44bfb1. */
void *player_get(void *unit, int arg)
{
    void *res = NULL;

    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov eax, [edx + 0x40]          /* CALL *[vtable+0x40] */
            push arg
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* vtable unit[+0x38] — вызов с одним stack-аргументом-указателем
 * (`MOV EDX,[unit]; PUSH <ptr>; CALL *0x38(EDX)`, ECX = unit, `ret $4`).
 * site 0x44c71b (B78): ptr = локальная тройка, которую затем читает 44bbf0;
 * второй сайт того же оффсета — R14 0x44c883. Возврат в R11 не читается. */
int vcall38_ptr(void *unit, void *out_triple)
{
    int res = 0;

    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            mov edx, [ecx]                 /* vtable = [receiver] */
            mov eax, [edx + 0x38]          /* CALL *[vtable+0x38] */
            push out_triple
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* FUN_00439ae0 — сравнение string-объектов (__cdecl, 2 аргумента, очистка
 * вызывающим). site 0x44bf44. */
int string_obj_cmp(void *a, void *b)
{
    void *addr = fn(0x439ae0);
    int res = 0;

    if (addr == NULL) {
        return 0;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            push b                         /* arg2 — старший, первый push */
            push a                         /* arg1 — младший, [esp+4] на входе */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* cdecl: снимает вызывающий — не важно */
            popad
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    return res;
}

/* FUN_00446a80 — таймер сообщения: ECX = unit, 0x3c-байтовая структура по
 * значению (`ret $0x3c`), callee снимает ровно зарезервированные вызывающим
 * `sub $0x3c`. site 0x44bfa7. */
void ai_time_update(void *unit, const void *msg60)
{
    void *addr = fn(0x446a80);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov ebx, esp                   /* сохранённый esp (esi/edi/ecx — копирование) */
            mov ecx, unit
            mov edx, msg60
            sub esp, 0x3c                  /* 0x3c байт по значению */
            mov edi, esp
            mov esi, edx
            mov ecx, 15
            rep movsd                      /* 15 dword = 0x3c байт */
            mov ecx, unit
            mov eax, addr
            call eax
            mov esp, ebx                   /* callee снял 0x3c */
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_004040a0 — ctor msg-объекта (thiscall, ecx = this, `ret`). */
void msg_ctor(void *this_)
{
    void *addr = fn(0x4040a0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, this_
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_004040f0 — копия msg-объекта (thiscall, ecx = dst, 1 стек-арг = src,
 * `ret $0x4`). site 0x44bfa0. */
void msg_copy(void *dst, const void *src)
{
    void *addr = fn(0x4040f0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, dst
            push src
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* --- Фаза 5.1b-2: листья R3 (сайты FUN_0044bdd0 0x44bfb8..0x44c09c) --- */

/* FUN_0042da60 — player-флаги: ECX = player, 1 стек-арг char, `ret $0x4`,
 * EAX = [player+0x22c] (объект флагов). site 0x44bfc4/0x44bfe0/0x44bffd. */
void *player_flag_get(void *player, int flag)
{
    void *addr = fn(0x42da60);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, player
            push flag
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_00435d50 — указатель на флаги: ECX = объект, 0 стек-аргументов (`ret`),
 * EAX — результат. sites 0x44bfcb/0x44bfe7/0x44c004. */
void *player_flags(void *flags_obj)
{
    void *addr = fn(0x435d50);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, flags_obj
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_0042d9c0 — player pos_set: ECX = player, 1 стек-арг = &triple,
 * `ret $0x4` (@0x42d9f4); пишет 3 float в тройку. site 0x44c240. */
void player_pos_set(void *player, void *triple)
{
    void *addr = fn(0x42d9c0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, player
            push triple
            mov eax, addr
            call eax
            mov esp, esi                   /* ret 4 — стек = сохранённый esp */
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_00446af0 — assert/log: ECX = unit, 2 стек-арг, `ret $0x8` (@0x446bae).
 * Лист читает arg1 = [S] (последний push), arg2 = [S+4] (@0x446b4f/0x446b57),
 * поэтому сайт `PUSH 0x51cc50; PUSH 0xa` даёт arg1 = 0xa, arg2 = строка.
 * site 0x44c28c. */
void log_assert(void *unit, const char *msg, int code)
{
    void *addr = fn(0x446af0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            push msg                       /* arg2 — старший, первый push */
            push code                      /* arg1 — [S] на входе листа */
            mov eax, addr
            call eax
            mov esp, esi                   /* ret 8 */
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_00448c30 — cursor/mode update: ECX = unit, 1 стек-арг, `ret $0x4`.
 * site 0x44c012. */
void cursor_mode_update(void *unit, int arg)
{
    void *addr = fn(0x448c30);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            push arg
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_00448ee0 — state-флаг + debug-вывод: ECX = unit, 1 стек-арг (char),
 * `ret $0x4` (@0x44914f). sites R22: 0x44d029 (arg 0), 0x44d05c (arg 1). */
void state_flag_log(void *unit, int flag)
{
    void *addr = fn(0x448ee0);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, unit
            push flag
            mov eax, addr
            call eax
            mov esp, esi                   /* ret 4 */
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0049b0f0 — ctor строкового объекта из char*: ECX = объект,
 * 1 стек-арг, `ret $0x4`, EAX = объект. site 0x44c02b. */
void *string_ctor(void *obj, const char *s)
{
    void *addr = fn(0x49b0f0);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, obj
            push s
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_00413060 — сборка строки: 3 стек-арга, очистка вызывающим (`ret`),
 * EAX = ESI (`mov 0x24(%esp),%esi` @0x4130a3 = слот [S+4], dst) @0x4130c9.
 * ECX на входе мёртв (перезаписан @0x41307e) — кладём arg3, как в сайте
 * 0x44c046. */
void *string_format(void *dst, const void *a2, const void *a3)
{
    void *addr = fn(0x413060);
    void *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, a3
            push a3
            push a2
            push dst
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_0049ad70 — c_str(): ECX = объект, 0 стек-аргументов (`ret`),
 * EAX = [obj] или 0x4ed260. site 0x44c058. */
const char *string_c_str(void *obj)
{
    void *addr = fn(0x49ad70);
    const char *res = NULL;

    if (addr == NULL) {
        return NULL;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, obj
            mov eax, addr
            call eax
            mov res, eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    return res;
}

/* FUN_00462780 (0x462780..0x4627a3) — dtor ссылочной строки: `[this+4]` —
 * указатель на блок {count, ..., vtable}; при count→0 вызов `*0x18`.
 * __thiscall, ECX = this, 0 стек-аргументов (`ret`). site R16 B99 0x44cacd
 * (ECX = `frame+0x58`, поле msg-объекта `frame+0x4c`). */
void msg_string_dtor(void *obj)
{
    void *addr = fn(0x462780);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, obj
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0049ac70 — dtor строкового объекта: ECX = объект, 0 аргументов (`ret`).
 * sites 0x44c070/0x44c081. */
void string_dtor(void *obj)
{
    void *addr = fn(0x49ac70);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, obj
            mov eax, addr
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* IAT 0x4ec064 — kernel32!OutputDebugStringA(s): stdcall, 1 аргумент,
 * callee снимает; вызов через IAT-слот (`call *0x4ec064`). site 0x44c05e. */
void output_debug_string_a(const char *s)
{
    void *addr = fn(0x4ec064);

    if (addr == NULL) {
        return;
    }
    __try {
        __asm {
            pushad
            mov esi, esp
            push s
            mov eax, addr
            mov eax, [eax]                 /* CALL *0x4ec064 — через IAT-слот */
            call eax
            mov esp, esi
            popad
        }
    } __except (native::av_filter()) {
    }
}

/* FUN_0045e1d0 — grid-recompute (821 B, тело не дизассемблировано).
 * __thiscall, ECX = rec (PathFinder-объект `frame+0xcc`), 2 стек-аргумента,
 * RET 8 (site 0x44c8c8: ESP=frame−8 перед call; тройка-сохранение R14 и
 * `FLDS 0x1c` R15 сходятся только при снятии 8 байт). arg1 = pos-слот
 * (`&frame+0xc0`, заполнен vtable+0x38), arg2 = grid-record (`&frame+0x88`).
 * Возврат EAX — указатель на тройку; site читает её немедленно и без guard'а,
 * поэтому обёртка копирует 3 dword сразу. NULL (AV) → тройка = 0. */
void grid_recompute(void *rec, void *pos, void *req, float out3[3])
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45e1d0);
    void *res = NULL;

    out3[0] = out3[1] = out3[2] = 0.0f;
    if (addr == NULL) {
        LD_CRASH_RET_VOID();
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, rec
            push req                       /* arg2 — старший, первый push */
            push pos                       /* arg1 — младший, [ESP+4] на входе */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* RET 8 снял аргументы */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = NULL;
    }
    if (res != NULL) {
        memcpy(out3, res, 12);             /* [EAX], [EAX+4], [EAX+8] @0x44c8cd.. */
    }
    /* v12: bdd0_leaf — ECX = PathFinder (rec), args = [pos, req] (оба стек-
     * аргумента в сигнатуре обёртки); ret = EAX листа (указатель тройки). */
    {
        int av[2];
        av[0] = (int)(size_t)pos;
        av[1] = (int)(size_t)req;
        int w[3];
        memcpy(w, out3, 12);              /* тройка, скопированная обёрткой */
        battle_capture_bdd0_leaf(0x45e1d0, rec, av, 2, (int)(size_t)res, w, 3);
    }
    ld_crash_leave();
}

/* --- Фаза 5.1b-7: листья R8 (FUN_0044bdd0, `0x44c3ed..0x44c4dc`) --- */

/* FUN_0045b150 — float-гейт ленты (thiscall: ecx = лента, 3 стек-аргумента
 * по 4 Б, `ret $0xc`, результат ST(0)). На сайте B60 аргументы — triple
 * result+4..+0xc, скопированный на стек dword'ами; поэтому arg2 передаётся
 * как int-биты (телом не читается), arg1/arg3 — float. site 0x44c4cb. */
void band_interp_gate(void *band, float p2, int p3, float p4, float *out_f)
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45b150);
    int i2;
    int i4;

    if (addr == NULL) {
        *out_f = 0.0f;
        LD_CRASH_RET_VOID();
    }
    /* push float в inline asm неоднозначен по размеру операнда — биты
     * переносятся dword'ами (сайт тоже копирует triple как dword'ы). */
    memcpy(&i2, &p2, sizeof(i2));
    memcpy(&i4, &p4, sizeof(i4));
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, band
            push i4                        /* arg3: [ebp+0x10] */
            push p3                        /* arg2: [ebp+0xc] */
            push i2                        /* arg1: [ebp+0x8] */
            mov eax, addr
            call eax
            mov edx, out_f
            fstp dword ptr [edx]           /* ST(0) → float */
            mov esp, esi                   /* RET 0xc снял аргументы */
            popad
        }
    } __except (native::av_filter()) {
        *out_f = 0.0f;
    }
    /* v12: bdd0_leaf — ECX = лента, args = [arg1 bits, arg2, arg3 bits],
     * ret = биты ST(0) (сняты fstp в out_f). */
    {
        int av[3], rbits;
        av[0] = i2;
        av[1] = p3;
        av[2] = i4;
        memcpy(&rbits, out_f, sizeof(rbits));
        battle_capture_bdd0_leaf(0x45b150, band, av, 3, rbits, NULL, 0);
    }
    ld_crash_leave();
}

/* FUN_0045b3e0 — предикат ленты (thiscall: ecx = лента, 3 стек-аргумента по
 * 4 Б, `ret $0xc`; результат AL = 0/1). Тело: гейт длины ленты
 * (`([+0x24]−[+0x20])/0x18` >= 2), 456b20-интерполяция соседних узлов и две
 * x87-границы (`0x4ec37c`, `0x4ef4b8`). site 0x44c554 (B63). */
int band_target_moved(void *band, float p1, int p2, float p3)
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45b3e0);
    int i1;
    int i3;
    int res = 0;

    if (addr == NULL) {
        LD_CRASH_RET(0);
    }
    /* push float в inline asm неоднозначен по размеру операнда — биты
     * переносятся dword'ами (сайт тоже копирует triple как dword'ы). */
    memcpy(&i1, &p1, sizeof(i1));
    memcpy(&i3, &p3, sizeof(i3));
    __try {
        __asm {
            pushad
            mov esi, esp
            mov ecx, band
            push i3                        /* arg3: [ebp+0x10] */
            push p2                        /* arg2: [ebp+0xc] (телом не читается) */
            push i1                        /* arg1: [ebp+0x8] */
            mov eax, addr
            call eax
            mov res, eax                   /* AL = 0/1, читается до сброса esp */
            mov esp, esi                   /* RET 0xc снял аргументы */
            popad
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    /* v12: bdd0_leaf — ECX = лента, args = [arg1 bits, arg2, arg3 bits],
     * ret = AL (тело тестирует только младший байт). */
    {
        int av[3];
        av[0] = i1;
        av[1] = p2;
        av[2] = i3;
        battle_capture_bdd0_leaf(0x45b3e0, band, av, 3,
                                 (int)(unsigned char)res, NULL, 0);
    }
    LD_CRASH_RET(res);
}

/* --- Фаза 5.1b-16: R17 (сайт `0x44cbd0`) --- */

/* FUN_0045eee0 — тик юнит-AI. ECX = BattleAI (= unit+0x138), 1 стек-аргумент
 * = &per-tick record, `ret $4`, EAX = код 0..4. Нативная форма для диспетчера
 * R17 (battle/tick.cpp), т.е. хук site6/трамплин не задействованы. */
int unit_ai_tick(void *ai, void *rec)
{
    ld_crash_enter(9);          /* LDHOOK_CRASHLOG: stage 9 (no-op off) */
    void *addr = fn(0x45eee0);
    int res = 0;

    if (addr == NULL) {
        LD_CRASH_RET(0);
    }
    __try {
        __asm {
            pushad                         /* esp «до аргументов», регистры в стеке */
            mov esi, esp                   /* сохранённый esp */
            mov ecx, ai
            push rec                       /* arg1 — [ESP+4] на входе */
            mov eax, addr
            call eax
            mov res, eax                   /* eax читается до восстановления esp */
            mov esp, esi                   /* RET 4 снял аргумент */
            popad                          /* + регистры вызывающего */
        }
    } __except (native::av_filter()) {
        res = 0;
    }
    LD_CRASH_RET(res);
}

/* Игровой строковый литерал по VA (base + RVA), см. native.h. */
const char *literal(unsigned int va)
{
    return (const char *)fn(va);
}

} /* namespace native */

/* v17: в живом билде replay-слоя нет — вход листа 0x45e1d0 не моделируется,
 * сайт исполняет нативный grid_recompute. */
int bdd0_path_state_modelled(void) { return 0; }
void *b0_path_pf(void) { return NULL; }
