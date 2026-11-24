/* tools/ldhook/battle/leaf_sites.h — хуки на CALL-САЙТЫ логических листьев
 * тела FUN_0044bdd0 (v12 `bdd0_leaf` на НАТИВНОМ прогоне).
 *
 * Зачем: `bdd0_leaf` раньше писали только `native::`-обёртки, а нативное тело
 * 44bdd0 их не зовёт — нативный прогон не записывал ни одного листа. Здесь
 * хуки стоят на самих 5-байтовых `call rel32` внутри 0x44bdd0..0x44d08e:
 *
 *   site:  E8 <rel32>          ; вызов листа (как в EXE)
 *   пач:   E8 <rel32 handler>  ; тот же размер и форма — пролог не переносим
 *
 * Обработчик (naked, на сайт) НЕ подменяет вызов: он снимает аргументы и
 * возвращает [esp] на continuation сайта, после чего прыгает на ОРИГИНАЛ
 * (`jmp [g_ls_origN]`). Оригинал исполняется с ТЕМ ЖЕ кадром вызывающего
 * (его аргументы, его регистры; отличается только адрес возврата), своим
 * `ret n` уходит в continuation, тот пишет `bdd0_leaf` (аргументы + результат)
 * и прыгает по исходному адресу возврата. ESP в конце = ESP вызывающего
 * (стек сбалансирован так, как его оставил бы сам лист).
 *
 * Регистры: обработчик снимает их pushfd/pushad и восстанавливает popad/popfd
 * ДО прыжка в оригинал, поэтому лист видит ровно то, что видел бы без хука.
 * Возврат листа: EAX — как есть; для 45b3e0 в запись идёт EAX&0xff; для
 * 45b150 (float в ST(0)) continuation снимает `fstps`, пишет запись и
 * возвращает значение на стек x87 (`flds`).
 *
 * Вложенность: адреса возврата и записи о вызовах живут в общих LIFO-стеках
 * (`g_ls_ret_stk` + `cap_leaf_args_record/cap_leaf_finish`), поэтому листья,
 * зовущие друг друга (458050/45e1d0), и повторный вход в 44bdd0 не путают
 * пары «вход-выход». Переполнение LIFO — хук пропускает запись и просто
 * прыгает в оригинал (поведение не меняется).
 *
 * Сбой при записи (чтение аргументов/`w`) ловится __try в C-хелперах и
 * помечает запись `trap`, вызов листа при этом не затронут.
 */
#ifndef LEAF_SITES_H
#define LEAF_SITES_H

#include <windows.h>

/* Что считать результатом листа (поле `ret` записи bdd0_leaf). */
#define LS_K_INT         0   /* EAX как биты */
#define LS_K_AL          1   /* EAX & 0xff (тело читает только AL) */
#define LS_K_F32         2   /* ST(0) как float-биты (45b150) */
#define LS_K_INT_W3_ARG1 3   /* EAX + 3 dword'а по arg1 (out-тройка 45c6f0) */
#define LS_K_INT_W3_RET  4   /* EAX + 3 dword'а по EAX (тройка 45e1d0) */

/* Предел глубины LIFO (44bdd0 ре-входим не глубже BDD0_STACK_MAX=32). */
#define LS_RET_MAX 64

struct leaf_site_def {
    unsigned site_va;      /* VA самого `call rel32` */
    unsigned target_va;    /* VA листа, куда он звал */
    int nargs;             /* стек-аргументов у листа */
    int ret_n;             /* `ret n` оригинала (= 4*nargs у всех 12 листьев) */
    int kind;              /* LS_K_* */
    void *handler;         /* naked-обработчик сайта */
    void **orig_slot;      /* ячейка адреса оригинала (заполняет установщик) */
};

/* Потребитель записей. enter возвращает token>=0 (запись нужна) или -1;
 * exit вызывается ровно один раз на удачный enter (LIFO). w/nw — dword'ы,
 * записанные листом через указатель; trap=1 — запись неполная. */
struct leaf_sink {
    int  (*enter)(int site_va, int va, int ecx, const int *args, int nargs);
    void (*exit)(int token, int va, int ecx, const int *args, int nargs,
                 int ret_bits, const int *w, int nw, int trap);
    void (*missing)(int site_va, int target_va, const char *why);
};

int  leaf_site_count(void);
const struct leaf_site_def *leaf_site_at(int i);
const struct leaf_site_def *leaf_site_defs(void);

void leaf_sites_set_sink(const leaf_sink *sink);
/* Сброс LIFO (вход в новый вызов 44bdd0): несбалансированные записи прошлого
 * вызова отбрасываются. */
void leaf_sites_reset(void);

/* Установка: resolve(VA) → адрес в памяти. Проверяет, что сайт — 5-байтовый
 * `call rel32` именно на ожидаемый лист (иначе sink->missing + пропуск).
 * Возврат — число пропатченных сайтов. */
int leaf_site_install_all(void *(*resolve)(unsigned va), char *why, int cap);

/* Точечная установка (её же использует self-test вне игры): site — адрес
 * `call rel32`, target_expected — куда он обязан звать, orig_slot — куда
 * положить адрес цели. Возврат 1 — пропатчено. */
int leaf_site_patch(void *site, void *target_expected, void **orig_slot,
                    void *handler, char *why, int cap);

/* LIFO-стек адресов возврата (общий для всех сайтов; порядок = порядок
 * вложенности вызовов). Определён в leaf_sites.cpp. */
extern volatile int g_ls_ret_n;
extern void *g_ls_ret_stk[LS_RET_MAX];

/* C-хелперы обработчиков (cdecl). Не для прямого вызова из игры. */
void __cdecl cap_leaf_args_record(int site, int va, int kind, int ecx,
                                  const int *args, int nargs);
void __cdecl cap_leaf_finish(int va, int ret_bits);

/* ------------------------- генерация обработчиков ------------------------- *
 * Одна пара на сайт: `cap_ls<N>_cont` (продолжение возврата) и
 * `cap_ls<N>_hook` (точка входа из пропатченного call). N — номер сайта
 * (уникальное имя функции и ячейки оригинала), VA — VA листа (в запись и как
 * аргумент хелпера), NARGS — число стек-аргументов, RETN — `ret n` оригинала,
 * CAPK — как снимать результат (INT/AL/F32), RECK — LS_K_* для записи.
 *
 * Инструкции — отдельные __asm-инструкции (naked-функция, C-кода нет): так
 * макрос не зависит от разделителей внутри одного __asm-блока. */
#define LS_RET_POP                                                           \
    __asm mov edx, dword ptr [g_ls_ret_n]                                    \
    __asm dec edx                                                            \
    __asm mov dword ptr [g_ls_ret_n], edx                                    \
    __asm mov edx, dword ptr [g_ls_ret_stk + edx*4]                          \
    __asm jmp edx

#define LS_CAP_INT(N, VA)                                                    \
    __asm push eax                                                           \
    __asm push eax                                                           \
    __asm push VA                                                            \
    __asm call cap_leaf_finish                                               \
    __asm add esp, 8                                                         \
    __asm pop eax

#define LS_CAP_AL(N, VA)                                                     \
    __asm push eax                                                           \
    __asm movzx eax, al                                                      \
    __asm push eax                                                           \
    __asm push VA                                                            \
    __asm call cap_leaf_finish                                               \
    __asm add esp, 8                                                         \
    __asm pop eax

#define LS_CAP_F32(N, VA)                                                    \
    __asm fstp dword ptr [g_ls_f32_##N]                                     \
    __asm push dword ptr [g_ls_f32_##N]                                      \
    __asm push VA                                                            \
    __asm call cap_leaf_finish                                               \
    __asm add esp, 8                                                         \
    __asm fld dword ptr [g_ls_f32_##N]

#define LS_SITE(N, SITE, VA, NARGS, RETN, CAPK, RECK)                        \
    void *g_ls_orig##N = NULL;                                               \
    static float g_ls_f32_##N = 0.0f;                                        \
    void __declspec(naked) cap_ls##N##_cont(void)                            \
    {                                                                        \
        LS_CAP_##CAPK(N, VA)                                                 \
        LS_RET_POP                                                           \
    }                                                                        \
    void __declspec(naked) cap_ls##N##_hook(void)                            \
    {                                                                        \
        __asm pushfd                                                         \
        __asm pushad                                                         \
        __asm mov esi, esp                                                   \
        __asm mov eax, dword ptr [g_ls_ret_n]                                \
        __asm cmp eax, LS_RET_MAX                                            \
        __asm jae lssafe##N                                                  \
        __asm mov ebx, dword ptr [esi+0x24]   /* адрес возврата вызывающего */ \
        __asm mov dword ptr [g_ls_ret_stk + eax*4], ebx                      \
        __asm inc dword ptr [g_ls_ret_n]                                     \
        __asm lea eax, dword ptr [esi+0x28]   /* стек-аргументы вызова */    \
        __asm push NARGS                                                     \
        __asm push eax                                                       \
        __asm push dword ptr [esi+0x18]       /* ECX на входе сайта */       \
        __asm push RECK                                                      \
        __asm push VA                                                        \
        __asm push SITE                                                      \
        __asm call cap_leaf_args_record                                      \
        __asm add esp, 24                                                    \
        __asm popad                                                          \
        __asm popfd                                                          \
        __asm mov dword ptr [esp], offset cap_ls##N##_cont                   \
        __asm jmp dword ptr [g_ls_orig##N]                                   \
        __asm lssafe##N:                                                     \
        __asm popad                                                          \
        __asm popfd                                                          \
        __asm jmp dword ptr [g_ls_orig##N]                                   \
    }

#endif /* LEAF_SITES_H */
