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

/* Лента `this+0x3c` (сайты FUN_0045eee0 B4 @0x45efb6/@0x45efbd) и очистка карт
 * (B5 @0x45efd7). Инвентарь 1.4 §5: вердикт «нативно». Все — __thiscall,
 * ECX = приёмник, 0 стек-аргументов (`ret`), тела не портированы. */
void band_free(void *band);          /* FUN_0045b6a0 — _free([band+0x20]) + нули +0x20..+0x28 */
void native_45af10(void *band);      /* FUN_0045af10 — `movb $1,0x4(ecx)`; назначение не подтверждено */
void clear_maps(void *ai);           /* FUN_0045ca70 — erase+_free по map1/map2 (обход от this+0x6c) */
void search_params_update(void *ai); /* FUN_0045c8c0 — пересчёт радиуса/RECT/порога поиска */
void band_rebuild(void *ai, void *node, int bits);  /* FUN_0045e9c0 — перестройка ленты (ret 8) */
void *target_search(void *ai, int filter, void *cmp_pos, int arg3, int *out_code);  /* FUN_0045e0a0 */

/* FUN_00458050 (0x458050..0x4581f4) — grid-якорь: ECX = ctx[8] (GridLevel),
 * 6 стек-аргументов (`ret $0x18`): arg1..arg4 — 16-байтовый temp (dword 0/2 —
 * float-позиция, dword 3 — указатель), arg5 — int-слот, куда callee пишет
 * байт-флаг (@0x4580fd `= 1` / @0x458161 `= 0`), arg6 — world (источник
 * 0x449160 @0x4580e8). Возврат EAX — узел/якорь или NULL. Тело не портировано
 * (инвентарь §5: порт — 4.1d только вызов); сайт @0x45f10c. */
void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot, void *world);

/* FUN_0045f430 (0x45f430..0x45f4e2) — ctor/reset объекта PathFinder:
 * __thiscall, ECX = this, 0 стек-аргументов (`ret` @0x45f4e2); тело (SEH)
 * зовёт 45c260 по +0x3c, 47ab80 ×2 (+0x68/+0x74) и 45c330 по +0x0. Сайт
 * R12 B82 — `&frame+0xcc` (0x44c74d); тот же объект — приёмник 45e1d0
 * (0x44c8b6, R14) и dtor 45e5e0 (0x44cad2, R16), размер ≤0x88 Б.
 * Тело не портировано. */
void pathfinder_init(void *pf);

/* FUN_0045e5e0 (0x45e5e0..0x45e6ab) — dtor объекта PathFinder (`frame+0xcc`):
 * `45ca70` (очистка карт), `_free` блоков +0x00/+0x0c, map-dtor +0x74/+0x68,
 * `45b670` по +0x3c. __thiscall, ECX = объект, 0 стек-аргументов (`ret`
 * @0x45e6ab). sites R16: 0x44cae4 (B100), 0x44cb00 (B101). */
void pathfinder_dtor(void *pf);

/* IAT 0x4ec268 — SetRectEmpty(rect) (`call *0x4ec268` @0x45c34c в FUN_0045c330;
 * вызов через IAT-слот — `mov eax,[addr]` перед call). 1 аргумент, возврат BOOL
 * в EAX (в порту не читается). */
int set_rect_empty(void *rect);

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

/* --- Фаза 5.1b: листья R1/R2 (FUN_0044bdd0). Все — вызовы по VA/ABI
 * листинга; номер строки — site-адрес в game_dis.txt. Обёртки снимают/-
 * восстанавливают esp (pushad + сохранённый esp), поэтому `ret` / `ret n` /
 * очистка вызывающим неразличимы для вызывающего порта. --- */

/* FUN_00442570 — аксессор состояния игры. __thiscall, ECX = unit, 1 стек-
 * аргумент (`ret $0x4` @0x4426f2), возврат EAX = result-объект
 * (`+0x04..+0x0c` float-тройка, `+0x10` player, `+0x14` pos-cursor).
 * sites: 0x44be0e, 0x44beac, 0x44bf0b, 0x44bf32. */
void *game_state_get(void *unit, int flags);

/* vtable `unit[+0x28]` — world-notify. ECX = unit, аргумент — 32-байтовая
 * структура ПО ЗНАЧЕНИЮ: вызывающий делает `sub $0x20` и копирует снимок в
 * [esp] (0x44be15..0x44be49), callee снимает 0x20 байт (иначе ESP на 0x44be58
 * не восстанавливается). Возврат не читается. site: 0x44be55. */
void world_notify_snapshot(void *unit, const void *snapshot32);

/* FUN_0045c6f0 (0x45c6f0..0x45c71e) — позиция из ctx: 3 dword из
 * `[ctx]+0xc`, либо нули при `[ctx]==0`. __thiscall, ECX = ctx (= unit+0x138),
 * 1 стек-аргумент = out (`ret $0x4`), возврат EAX = out. site: 0x44bea1. */
void *ctx_pos_get(void *ctx, float *out_f);

/* FUN_0045c370 (0x45c370..0x45c382) — голова ленты `this+0x3c`: count != 0 ?
 * &this[0x3c] : 0 (sign-mask `neg/sbb/and`). __thiscall, ECX = BattleAI,
 * 0 стек-аргументов (`ret`). site: 0x44bee7/0x44bef2. */
void *band_head(void *ai);

/* FUN_0045af50 (0x45af50..0x45af70) — длина ленты = ([+0x24]−[+0x20])/0x18
 * (magic-делитель, знаковое). __thiscall, ECX = band (= ai+0x3c), 0 стек-
 * аргументов (`ret`). site: 0x44bef9. */
int band_count(void *band);

/* FUN_0045c730 (0x45c730..0x45c74c) — курсор ленты. Листинг: `MOV EAX,[ECX];
 * TEST; JZ` (@0x45c730..0x45c734), затем дважды `MOV EAX,[EAX+0xc]; TEST; JZ`
 * (@0x45c736..0x45c742), затем `MOV EAX,[EAX+0x234]; RET` (@0x45c744..0x45c74a);
 * ветка NULL — `XOR EAX,EAX; RET` (@0x45c74b). Итог: `[[[ctx]+0xc]+0xc]+0x234`,
 * 0 при NULL на любом шаге. __thiscall, ECX = BattleAI (= unit+0x138), 0 стек-
 * аргументов (`ret`); side effects нет. sites: 0x44c36f/0x44c37e (R7). */
void *band_cursor(void *ai);

/* vtable `unit[+0x2c]` — float фазы. ECX = unit, 0 стек-аргументов; результат
 * в ST(0), поэтому пишется в *out (fstp). site: 0x44bf1e. */
void phase_time(void *unit, float *out_f);

/* vtable `unit[+0x40]` — player-getter. ECX = unit, 1 стек-аргумент (idx).
 * В нативе слот не всегда пушат (0x44bfb1: callee читает [esp+4] = слот
 * сохранённого ESI, т.е. значение ESI на входе в 44bdd0) и callee может не
 * снимать аргумент — обёртка восстанавливает esp, поэтому обе формы ABI
 * дают одинаковый результат. sites: 0x44bf40 (arg = player), 0x44bfb1
 * (arg = *(frame+4)). */
void *player_get(void *unit, int arg);

/* vtable `unit[+0x38]` — вызов с одним стек-аргументом-указателем
 * (`MOV EDX,[unit]; PUSH <ptr>; CALL *0x38(EDX)`, ECX = unit; 1 аргумент,
 * `ret $4`). site 0x44c71b (R11 B78: ptr = локальная тройка, её же dword'ы
 * уходят аргументами в 44bbf0); второй сайт оффсета — R14 0x44c883.
 * Возврат в R11 не читается. */
int vcall38_ptr(void *unit, void *ptr);

/* FUN_00439ae0 — сравнение string-объектов. __cdecl, 2 аргумента, очистка
 * вызывающим (`add $0x8,%esp` @0x44bf49). Возврат — uint в EAX; вызывающий
 * тестирует только AL (`test %al,%al`). site: 0x44bf44. */
int string_obj_cmp(void *a, void *b);

/* FUN_00446a80 — обновление таймера сообщения (`unit+0x54` = текущее время, сек).
 * __thiscall, ECX = unit, аргумент — 0x3c-байтовая структура ПО ЗНАЧЕНИЮ
 * (`ret $0x3c` @0x446aeb; вызывающий резервирует её `sub $0x3c` @0x44bf5d и
 * callee снимает ровно столько же). site: 0x44bfa7. */
void ai_time_update(void *unit, const void *msg60);

/* FUN_004040a0 — ctor msg-объекта (0x34 Б): __thiscall, ECX = this, 0 стек-
 * аргументов (`ret` @0x4040e6). site: 0x44bf58. */
void msg_ctor(void *this_);

/* FUN_004040f0 — копия msg-объекта (0x3c Б: +0x00..+0x38). __thiscall,
 * ECX = dst, 1 стек-аргумент = src (`ret $0x4` @0x40415a). site: 0x44bfa0. */
void msg_copy(void *dst, const void *src);

/* FUN_00462780 (0x462780..0x4627a3) — dtor ссылочной строки (поле msg-объекта
 * `+0x0c`; `+0x04` — указатель на блок {count, ..., vtable}, декремент и вызов
 * `*0x18` при нуле). __thiscall, ECX = объект, 0 стек-аргументов (`ret`).
 * site R16 B99 — 0x44cacd, ECX = `&frame+0x58`. */
void msg_string_dtor(void *obj);

/* --- Фаза 5.1b-2: листья R3 (FUN_0044bdd0, `0x44bfb8..0x44c09c`) --- */

/* FUN_0042da60 — ленивый геттер player-флагов: возврат EAX = `[this+0x22c]`
 * (при 0 и char-арг != 0 — `operator new(0x40)` + 0x4380f0). __thiscall,
 * ECX = player, 1 стек-аргумент char (`ret $0x4` @0x42dacb; читается как
 * `[esp+0x14]` @0x42da82 после 4 push пролога). sites: 0x44bfc4/0x44bfe0/
 * 0x44bffd. */
void *player_flag_get(void *player, int flag);

/* FUN_00435d50 — флаги игрока: 0 стек-аргументов (`ret` @0x435d6d),
 * ECX = объект из player_flag_get, возврат EAX. sites: 0x44bfcb/0x44bfe7/
 * 0x44c004 (`TEST EAX` / `TESTB $2,(%eax)` / `TESTB $4,(%eax)`). */
void *player_flags(void *flags_obj);

/* FUN_00448c30 — cursor/mode update (SEH-пролог 0x448c30..0x448c44).
 * __thiscall, ECX = unit, 1 стек-аргумент (`ret $0x4` @0x448e34; сайт — 0x40
 * @0x44c00e). Возврат не читается. site: 0x44c012. */
void cursor_mode_update(void *unit, int arg);

/* FUN_0049b0f0 — ctor строкового объекта (3 dword) из char*: __thiscall,
 * ECX = объект, 1 стек-аргумент = char* (`ret $0x4` @0x49b10a), возврат
 * EAX = объект. site: 0x44c02b (литерал 0x4eee1c @0x44c022). */
void *string_ctor(void *obj, const char *s);

/* FUN_00413060 — сборка строки: 3 стек-аргумента, очистка вызывающим
 * (`ret` @0x4130d6; вызывающий `add $0xc,%esp` @0x44c04b). arg1 — dst-объект
 * (первый параметр = последний push сайта). Возврат EAX = ESI, снятый
 * `mov 0x24(%esp),%esi` @0x4130a3 = слот `[S+4]` (dst); @0x4130c9 `mov
 * %esi,%eax`. Порт передаёт значение как есть.
 * arg2/arg3 — источники (сайт: arg2 = unit+0x18, arg3 = объект-литерал).
 * ECX на входе мёртв (перезаписан @0x41307e); обёртка кладёт arg3, как в сайте.
 * site: 0x44c046. */
void *string_format(void *dst, const void *a2, const void *a3);

/* FUN_0049ad70 — c_str(): __thiscall, ECX = объект, 0 аргументов (`ret`
 * @0x49ad7b), EAX = `[obj]` или 0x4ed260 при NULL. site: 0x44c058. */
const char *string_c_str(void *obj);

/* FUN_0049ac70 — dtor строкового объекта (`_free([obj])`). __thiscall,
 * ECX = объект, 0 аргументов (`ret` @0x49ac7d). sites: 0x44c070/0x44c081. */
void string_dtor(void *obj);

/* --- Фаза 5.1b-5: листья R6 (FUN_0044bdd0, `0x44c22a..0x44c306`) --- */

/* FUN_0042d9c0 — player pos_set: __thiscall, ECX = player (результат vtable+0x40),
 * 1 стек-аргумент = &triple (`ret $0x4` @0x42d9f4), пишет 3 float в triple.
 * site: 0x44c240 (аргумент — тот же указатель, что оставлен на стеке
 * `PUSH EAX` @0x44c238 после геттера; геттер его не снимает). */
void player_pos_set(void *player, void *triple);

/* FUN_00446af0 — assert/log: __thiscall, ECX = unit, 2 стек-аргумента
 * (`ret $0x8` @0x446bae). Сайт 0x44c283/0x44c288: `PUSH 0x51cc50` затем
 * `PUSH 0xa`, т.е. arg1 = код 0xa, arg2 = строка. Внутри зовёт ai_time_update. */
void log_assert(void *unit, const char *msg, int code);

/* IAT 0x4ec064 — kernel32!OutputDebugStringA (objdump -p: `000ec064 <none>
 * 0268 OutputDebugStringA`; LoadCursorFromFileA лежит на 0x4ec264 — правка
 * ошибочного «cursor» из disasm4). WinAPI __stdcall, 1 аргумент (char*),
 * callee снимает его; вызов через IAT-слот (`call *0x4ec064` @0x44c05e).
 * Возврат (void) в теле не читается. */
void output_debug_string_a(const char *s);

/* Игровой строковый литерал по VA (CP1251 char*): fn(VA) = base + RVA.
 * Помощник для push-сайтов вида `push $0x4eee1c`: логика передаёт только
 * номер VA, разрешение адреса — здесь (инвариант 3, без «голых» указателей). */
const char *literal(unsigned int va);

/* FUN_0045e1d0 (0x45e1d0..…, 821 B; тело не дизассемблировано) — grid-recompute.
 * ABI по site 0x44c8c8 + инвентарь §1.C: __thiscall, RET 8 (2 стек-аргумента).
 * ECX = &frame+0xcc — тот же PathFinder-объект, что ctor 45f430 @0x44c74d и
 * dtor 45e5e0 @0x44cad2 (один объект кадра).
 *   arg1 = &frame+0xc0 (`lea 0xc0(%esp)`, последний push) — слот, заполненный
 *          перед вызовом через vtable+0x38 (@0x44c883); `float* pos` по ABI.
 *   arg2 = &frame+0x88 (первый push) — grid-record R12: {+0 tag 0xeb, +4 unit,
 *          +8 tree, +0xc &pos-тройки, +0x10 anchor, +0x14 dword unit+0x12c,
 *          +0x18 0}.
 * Оба указателя передаются как есть — записи callee ложатся в те же слоты
 * кадра, что и в нативе. Возврат EAX — указатель на тройку (site @0x44c8cd
 * сразу читает [EAX]/[EAX+4]/[EAX+8], guard'а нет); обёртка копирует 3 dword в
 * out3, пока указатель жив. site: 0x44c8c8. */
void grid_recompute(void *rec, void *pos, void *req, float out3[3]);

/* --- Фаза 5.1b-7: листья R8 (FUN_0044bdd0, `0x44c3ed..0x44c4dc`) --- */

/* FUN_00403e80 (0x403e80..0x403eb5) — равенство двух float-троек: три пары
 * `FLDS [ECX+i]; FCOMPS [EDX+i]; FNSTSW AX; TEST AH,0x44; JP → 0`. `JP(0x44)`
 * взят ⟺ NOT(a==b) (unordered считается «не равно», решение 1.6), поэтому 1 —
 * только при почленном равенстве всех трёх пар (упорядоченно). __thiscall:
 * ECX = a, 1 stack-аргумент `b` (`mov 0x4(%esp),%edx` @0x403e82), `ret $0x4`
 * (@0x403eae/0x403eb3); возврат EAX = 0/1, вызывающий тестирует AL.
 * site: 0x44c423 (ECX = EAX = out-тройка 45c6f0, stack = result+4).
 * Тело известно целиком (53 Б, без каллов) — кандидат на чистый C++-порт. */
int pos3_equal(const void *a, const void *b);

/* FUN_0045b150 (0x45b150..0x45b3d8) — листовой float-гейт ленты
 * (интерполяция позиции; тело 651 Б не портировано, единственный callee —
 * 456b20). ABI по телу: пролог 0x45b150..0x45b15b; `RET $0xc` @0x45b3d8;
 * результат — ST(0) (`FLDS 0x10(%esp)` @0x45b3ce перед ret). __thiscall:
 * ECX = лента (45c370 → &band; тело читает [+0x20]/[+0x24]), 3 stack-
 * аргумента по 4 Б: arg1 float `[ebp+0x8]`, arg2 undefined4 (телом не
 * читается; layout = triple result+4..+0xc), arg3 float `[ebp+0x10]`.
 * Возврат в ST(0) → *out_f (fstp). sites: 0x44c4cb (B60), 0x44c5e0 (B67). */
void band_interp_gate(void *band, float p2, int p3, float p4, float *out_f);

/* --- Фаза 5.1b-8: листья R9 (FUN_0044bdd0, `0x44c4dd..0x44c5b0`) --- */

/* FUN_0045b3e0 (0x45b3e0..0x45b52d) — предикат ленты «цель сдвинулась/удалена»,
 * возврат AL (1/0), тело на x87. __thiscall: ECX = лента (site 0x44c554:
 * ECX = результат 45c370 → &band; тело читает `[+0x20]`/`[+0x24]` — тот же
 * layout, что 45af50). 3 стек-аргумента по 4 Б, `RET $0xc` (@0x45b524/0x45b52d):
 * arg1 float `[ebp+0x8]` (тело @0x45b4db `fsubs 0x18(%esp)` при esp=e−0x14),
 * arg2 undefined4 (телом НЕ читается: читаются только слоты e+4 и e+0xc),
 * arg3 float `[ebp+0x10]` (@0x45b4e3 `fsubs 0x20(%esp)`). На сайте B63 на стек
 * положена triple result+4..0xc dword'ами (`sub $0xc` @0x44c534, без `add esp`
 * после вызова — аргументы снимает callee). Возврат: AL=1 @0x45b51e, AL=0
 * (`xor al,al`) @0x45b527; вызывающий тестирует `%al` (@0x44c559).
 * sites: 0x44c554 (B63), 0x44c65d (B70, R10). */
int band_target_moved(void *band, float p1, int p2, float p3);

/* --- Фаза 5.1b-16: R17 (FUN_0044bdd0, сайт `0x44cbd0`) --- */

/* FUN_0045eee0 — тик юнит-AI (порт Фазы 4 — `s6_unit_ai_tick`; здесь нативная
 * форма для диспетчера выключенного порта, см. `b0_45eee0_dispatch` в
 * battle/tick.cpp). ABI: `int __thiscall(BattleAI*, ptr)` — ECX = BattleAI
 * (= unit+0x138), 1 стек-аргумент = &per-tick record (7 dword, frame+0xa4),
 * `ret $4`, EAX = код 0..4. Наке́д-хук/трамплин site6 не используется. */
int unit_ai_tick(void *ai, void *rec);

/* Игровая куча (п. 2.2): путь аллокации игровых структур — только через эти
 * обёртки; malloc/free из .pyd запрещены (план, «Риски»). Оба — cdecl с одним
 * аргументом, очистка вызывающим (листинг 0x4cfa11: вызывающий ADD ESP,4
 * @0x45c2eb в FUN_0045c2e0). */
void *operator_new(unsigned int size);   /* FUN_004cfa11 (operator new) */
void deallocate(void *ptr);              /* FUN_004d0e16 (_free), пара к нему */

} /* namespace native */

#endif /* BATTLE_NATIVE_H */
