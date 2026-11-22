/* tools/ldhook/battle/leaf_sites.cpp — таблица CALL-САЙТОВ 10 логических
 * листьев тела FUN_0044bdd0 (0x44bdd0..0x44d08e) и установка хуков на них.
 *
 * Сайты получены из листинга EXE (game_dis.txt, диапазон 0x44bdd0..0x44d08e,
 * все `call` с целью в списке 10 листьев). Все 32 сайта — 5-байтовый
 * `call rel32` (E8 + rel32); установщик это проверяет байтами и сверяет
 * rel32 с ожидаемым адресом листа — несовпадение = пропуск + запись
 * `leaf_missing`, а не молчаливая потеря листьев.
 *
 * Таблица: site VA, лист VA, nargs (стек-аргументы), ret n (эпилог листа),
 * вид результата. Источники:
 *   45c6f0 `ret 4`  1 арг, EAX=out + тройка по arg1 (native.cpp ctx_pos_get);
 *   45c370 `ret`    0 арг, EAX=голова ленты (band_head);
 *   45af50 `ret`    0 арг, EAX=длина ленты/0x18 (band_count);
 *   45c730 `ret`    0 арг, EAX=курсор ленты (band_cursor);
 *   45b150 `ret 0xc` 3 арг, результат ST(0) (band_interp_gate);
 *   45b3e0 `ret 0xc` 3 арг, результат AL (band_target_moved);
 *   403e80 `ret 4`  1 арг, EAX=0/1 (pos3_equal);
 *   458050 `ret 0x18` 6 арг, EAX=узел якоря (grid_anchor);
 *   45e1d0 `ret 8`  2 арг, EAX=указатель тройки (path_find);
 *   44bbf0 `ret 0x10` 4 арг, EAX=результат (grid_query).
 * У всех 10 листьев ret_n == 4*nargs, поэтому continuation всегда входит с
 * ESP = ESP вызывающего на сайте (стек сбалансирован самим листом).
 *
 * Соглашения (все 10 листьев — __thiscall-подобные: ECX = приёмник, стек-
 * аргументы в порядке arg1..argN с [esp+4], возврат EAX, кроме 45b150 (ST(0))
 * и 45b3e0 (AL); все завершаются `ret n`, вызывающий стек не чистит):
 *
 *   #  site VA    лист     n args/ret n  приёмник ECX  результат
 *   1  44bea1  45c6f0     1 / 4        ctx           EAX=out (+тройка по arg1)
 *   2  44bee7  45c370     0 / 0        ai/BattleAI   EAX=голова ленты
 *   3  44bef2  45c370     0 / 0        ai            EAX
 *   4  44bef9  45af50     0 / 0        band          EAX=длина/0x18
 *   5  44c247  44bbf0     4 / 16       world         EAX
 *   6  44c276  44bbf0     4 / 16       world         EAX
 *   7  44c36f  45c730     0 / 0        ai            EAX=курсор ленты
 *   8  44c37e  45c730     0 / 0        ai            EAX
 *   9  44c3b7  45c370     0 / 0        ai            EAX
 *  10  44c3c6  45c370     0 / 0        ai            EAX
 *  11  44c3cd  45af50     0 / 0        band          EAX
 *  12  44c3fa  45c730     0 / 0        ai            EAX
 *  13  44c41c  45c6f0     1 / 4        ctx           EAX=out (+тройка по arg1)
 *  14  44c423  403e80     1 / 4        a             EAX=0/1
 *  15  44c4c4  45c370     0 / 0        ai            EAX
 *  16  44c4cb  45b150     3 / 12       band          ST(0)
 *  17  44c54d  45c370     0 / 0        ai            EAX
 *  18  44c554  45b3e0     3 / 12       band          AL=0/1
 *  19  44c5d9  45c370     0 / 0        ai            EAX
 *  20  44c5e0  45b150     3 / 12       band          ST(0)
 *  21  44c65f  45c370     0 / 0        ai            EAX
 *  22  44c666  45b3e0     3 / 12       band          AL=0/1
 *  23  44c6fc  45c6f0     1 / 4        ctx           EAX=out (+тройка по arg1)
 *  24  44c730  44bbf0     4 / 16       world         EAX
 *  25  44c7da  458050     6 / 24       grid          EAX=узел якоря
 *  26  44c8c8  45e1d0     2 / 8        PathFinder    EAX (+тройка по EAX)
 *  27  44cd83  45c370     0 / 0        ai            EAX
 *  28  44cd8e  45c370     0 / 0        ai            EAX
 *  29  44cd95  45af50     0 / 0        band          EAX
 *  30  44cfec  45c370     0 / 0        ai            EAX
 *  31  44cff7  45c370     0 / 0        ai            EAX
 *  32  44cffe  45af50     0 / 0        band          EAX
 *
 * 5-байтовая длина проверена дважды: (1) по листингу EXE все 32 сайта — E8 +
 * rel32 (байты совпадают с `game_dis.txt`); (2) при установке leaf_site_patch
 * требует p[0]==0xE8 и site+5+rel32 == адрес ожидаемого листа, а адрес
 * handler'а проверяется на влезание в rel32. Несовпадение → `leaf_missing`.
 *
 * Модуль не зависит от игровых структур: он не знает ни про `native::`, ни
 * про файл захвата — адреса листьев даёт `resolve`, записи забирает `leaf_sink`.
 * Поэтому тот же код (и тот же макрос LS_SITE) проверяется offline
 * (tools/probes/leaf_sites_selftest.cpp).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "leaf_sites.h"

volatile int g_ls_ret_n = 0;
void *g_ls_ret_stk[LS_RET_MAX];

/* ----------------------------- LIFO входов ------------------------------- */
typedef struct {
    int va;
    int kind;
    int ecx;
    int nargs;
    int args[6];
    int token;          /* >=0 — sink ждёт exit */
    int trap;           /* запись неполная (сбой чтения) */
} ls_ent;

#define LS_DEPTH_MAX 64
static ls_ent g_ls_ent[LS_DEPTH_MAX];
static int    g_ls_depth = 0;        /* логическая глубина (может > LS_DEPTH_MAX) */
static const leaf_sink *g_ls_sink = NULL;

void leaf_sites_set_sink(const leaf_sink *sink)
{
    g_ls_sink = sink;
}

void leaf_sites_reset(void)
{
    g_ls_depth = 0;
    g_ls_ret_n = 0;
}

void __cdecl cap_leaf_args_record(int va, int kind, int ecx,
                                  const int *args, int nargs)
{
    ls_ent *e;
    int i;

    if (g_ls_depth >= LS_DEPTH_MAX) {
        g_ls_depth++;               /* переполнение: пара вход/выход без записи */
        return;
    }
    e = &g_ls_ent[g_ls_depth];
    e->va = va;
    e->kind = kind;
    e->ecx = ecx;
    e->nargs = (nargs > 0 && nargs <= 6) ? nargs : 0;
    e->token = -1;
    e->trap = 0;
    for (i = 0; i < 6; i++) e->args[i] = 0;
    if (e->nargs != nargs) e->trap = 1;
    if (e->nargs > 0 && args != NULL) {
        __try {
            for (i = 0; i < e->nargs; i++) e->args[i] = args[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            e->trap = 1;
            for (i = 0; i < 6; i++) e->args[i] = 0;
        }
    }
    if (g_ls_sink != NULL && g_ls_sink->enter != NULL) {
        e->token = g_ls_sink->enter(va, ecx, e->args, e->nargs);
    }
    g_ls_depth++;
}

void __cdecl cap_leaf_finish(int va, int ret_bits)
{
    ls_ent *e;
    int w[3];
    int nw = 0;
    int trap;
    const void *src = NULL;

    if (g_ls_depth <= 0) {
        return;                     /* сброс посреди вызова — запись потеряна */
    }
    g_ls_depth--;
    if (g_ls_depth >= LS_DEPTH_MAX) {
        return;                     /* вход был отброшен по переполнению */
    }
    e = &g_ls_ent[g_ls_depth];
    trap = e->trap;
    if (e->va != va) {
        trap = 1;                   /* рассинхрон пары вход/выход */
    }
    w[0] = w[1] = w[2] = 0;
    if (e->kind == LS_K_INT_W3_ARG1) {
        src = (const void *)(size_t)(unsigned)e->args[0];
    } else if (e->kind == LS_K_INT_W3_RET) {
        src = (const void *)(size_t)(unsigned)ret_bits;
    }
    if (src != NULL) {
        __try {
            memcpy(w, src, 3 * sizeof(int));
            nw = 3;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            nw = 0;
            trap = 1;
        }
    }
    if (e->token >= 0 && g_ls_sink != NULL && g_ls_sink->exit != NULL) {
        g_ls_sink->exit(e->token, e->va, e->ecx, e->args, e->nargs,
                        ret_bits, nw ? w : NULL, nw, trap);
    }
}

/* --------------------------- обработчики сайтов --------------------------- *
 * Порядок = порядок адресов сайтов. Номера фиксированы (на них ссылаются
 * `g_ls_orig<N>` и диагностика). */

LS_SITE(1,  0x45c6f0, 1, 4,  INT, LS_K_INT_W3_ARG1)   /* 0x44bea1 */
LS_SITE(2,  0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44bee7 */
LS_SITE(3,  0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44bef2 */
LS_SITE(4,  0x45af50, 0, 0,  INT, LS_K_INT)           /* 0x44bef9 */
LS_SITE(5,  0x44bbf0, 4, 16, INT, LS_K_INT)           /* 0x44c247 */
LS_SITE(6,  0x44bbf0, 4, 16, INT, LS_K_INT)           /* 0x44c276 */
LS_SITE(7,  0x45c730, 0, 0,  INT, LS_K_INT)           /* 0x44c36f */
LS_SITE(8,  0x45c730, 0, 0,  INT, LS_K_INT)           /* 0x44c37e */
LS_SITE(9,  0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c3b7 */
LS_SITE(10, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c3c6 */
LS_SITE(11, 0x45af50, 0, 0,  INT, LS_K_INT)           /* 0x44c3cd */
LS_SITE(12, 0x45c730, 0, 0,  INT, LS_K_INT)           /* 0x44c3fa */
LS_SITE(13, 0x45c6f0, 1, 4,  INT, LS_K_INT_W3_ARG1)   /* 0x44c41c */
LS_SITE(14, 0x403e80, 1, 4,  INT, LS_K_INT)           /* 0x44c423 */
LS_SITE(15, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c4c4 */
LS_SITE(16, 0x45b150, 3, 12, F32, LS_K_F32)           /* 0x44c4cb */
LS_SITE(17, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c54d */
LS_SITE(18, 0x45b3e0, 3, 12, AL,  LS_K_AL)            /* 0x44c554 */
LS_SITE(19, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c5d9 */
LS_SITE(20, 0x45b150, 3, 12, F32, LS_K_F32)           /* 0x44c5e0 */
LS_SITE(21, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44c65f */
LS_SITE(22, 0x45b3e0, 3, 12, AL,  LS_K_AL)            /* 0x44c666 */
LS_SITE(23, 0x45c6f0, 1, 4,  INT, LS_K_INT_W3_ARG1)   /* 0x44c6fc */
LS_SITE(24, 0x44bbf0, 4, 16, INT, LS_K_INT)           /* 0x44c730 */
LS_SITE(25, 0x458050, 6, 24, INT, LS_K_INT)           /* 0x44c7da */
LS_SITE(26, 0x45e1d0, 2, 8,  INT, LS_K_INT_W3_RET)    /* 0x44c8c8 */
LS_SITE(27, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44cd83 */
LS_SITE(28, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44cd8e */
LS_SITE(29, 0x45af50, 0, 0,  INT, LS_K_INT)           /* 0x44cd95 */
LS_SITE(30, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44cfec */
LS_SITE(31, 0x45c370, 0, 0,  INT, LS_K_INT)           /* 0x44cff7 */
LS_SITE(32, 0x45af50, 0, 0,  INT, LS_K_INT)           /* 0x44cffe */

#define LS_DEF(n, site, va, nargs, retn, kind)                                \
    { site, va, nargs, retn, kind, (void *)cap_ls##n##_hook, &g_ls_orig##n }

static const leaf_site_def k_leaf_sites[] = {
    LS_DEF(1,  0x44bea1, 0x45c6f0, 1, 4,  LS_K_INT_W3_ARG1),
    LS_DEF(2,  0x44bee7, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(3,  0x44bef2, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(4,  0x44bef9, 0x45af50, 0, 0,  LS_K_INT),
    LS_DEF(5,  0x44c247, 0x44bbf0, 4, 16, LS_K_INT),
    LS_DEF(6,  0x44c276, 0x44bbf0, 4, 16, LS_K_INT),
    LS_DEF(7,  0x44c36f, 0x45c730, 0, 0,  LS_K_INT),
    LS_DEF(8,  0x44c37e, 0x45c730, 0, 0,  LS_K_INT),
    LS_DEF(9,  0x44c3b7, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(10, 0x44c3c6, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(11, 0x44c3cd, 0x45af50, 0, 0,  LS_K_INT),
    LS_DEF(12, 0x44c3fa, 0x45c730, 0, 0,  LS_K_INT),
    LS_DEF(13, 0x44c41c, 0x45c6f0, 1, 4,  LS_K_INT_W3_ARG1),
    LS_DEF(14, 0x44c423, 0x403e80, 1, 4,  LS_K_INT),
    LS_DEF(15, 0x44c4c4, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(16, 0x44c4cb, 0x45b150, 3, 12, LS_K_F32),
    LS_DEF(17, 0x44c54d, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(18, 0x44c554, 0x45b3e0, 3, 12, LS_K_AL),
    LS_DEF(19, 0x44c5d9, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(20, 0x44c5e0, 0x45b150, 3, 12, LS_K_F32),
    LS_DEF(21, 0x44c65f, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(22, 0x44c666, 0x45b3e0, 3, 12, LS_K_AL),
    LS_DEF(23, 0x44c6fc, 0x45c6f0, 1, 4,  LS_K_INT_W3_ARG1),
    LS_DEF(24, 0x44c730, 0x44bbf0, 4, 16, LS_K_INT),
    LS_DEF(25, 0x44c7da, 0x458050, 6, 24, LS_K_INT),
    LS_DEF(26, 0x44c8c8, 0x45e1d0, 2, 8,  LS_K_INT_W3_RET),
    LS_DEF(27, 0x44cd83, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(28, 0x44cd8e, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(29, 0x44cd95, 0x45af50, 0, 0,  LS_K_INT),
    LS_DEF(30, 0x44cfec, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(31, 0x44cff7, 0x45c370, 0, 0,  LS_K_INT),
    LS_DEF(32, 0x44cffe, 0x45af50, 0, 0,  LS_K_INT),
};

int leaf_site_count(void)
{
    return (int)(sizeof(k_leaf_sites) / sizeof(k_leaf_sites[0]));
}

const struct leaf_site_def *leaf_site_at(int i)
{
    if (i < 0 || i >= leaf_site_count()) return NULL;
    return &k_leaf_sites[i];
}

const struct leaf_site_def *leaf_site_defs(void)
{
    return k_leaf_sites;
}

/* ------------------------------ установка -------------------------------- */

static int ls_fail(char *why, int cap, const char *fmt, unsigned a, unsigned b)
{
    if (why != NULL && cap > 0) {
        _snprintf_s(why, (size_t)cap, _TRUNCATE, fmt, a, b);
    }
    return 0;
}

int leaf_site_patch(void *site, void *target_expected, void **orig_slot,
                    void *handler, char *why, int cap)
{
    unsigned char *p = (unsigned char *)site;
    long rel;
    DWORD old = 0;
    int ok = 0;

    if (site == NULL || target_expected == NULL || handler == NULL ||
        orig_slot == NULL) {
        return ls_fail(why, cap, "null argument (site=0x%x target=0x%x)",
                       (unsigned)(size_t)site, (unsigned)(size_t)target_expected);
    }
    /* сайт обязан быть 5-байтовым call rel32 именно на ожидаемый лист */
    __try {
        if (p[0] != 0xE8) {
            return ls_fail(why, cap,
                           "site 0x%x is not `call rel32` (opcode 0x%x)",
                           (unsigned)(size_t)site, (unsigned)p[0]);
        }
        if ((void *)(p + 5 + *(const int *)(p + 1)) != target_expected) {
            return ls_fail(why, cap,
                           "site 0x%x calls 0x%x, expected 0x%x",
                           (unsigned)(size_t)site,
                           (unsigned)(size_t)(p + 5 + *(const int *)(p + 1)));
        }
        rel = (long)((const unsigned char *)handler - (p + 5));
        if (rel < -2147483647L || rel > 2147483647L) {
            return ls_fail(why, cap, "handler out of rel32 range (0x%x)",
                           (unsigned)(size_t)handler, 0);
        }
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
            return ls_fail(why, cap, "VirtualProtect failed at 0x%x (0x%x)",
                           (unsigned)(size_t)p, (unsigned)GetLastError());
        }
        p[0] = 0xE8;
        *(int *)(p + 1) = (int)rel;
        *orig_slot = target_expected;
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        VirtualProtect(p, 5, old, &old);
        ok = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ls_fail(why, cap, "fault while patching 0x%x (0x%x)",
                       (unsigned)(size_t)site, 0);
    }
    return ok;
}

int leaf_site_install_all(void *(*resolve)(unsigned va), char *why, int cap)
{
    int i, n = 0, missing = 0;

    if (why != NULL && cap > 0) why[0] = '\0';
    if (resolve == NULL) {
        ls_fail(why, cap, "no resolver (0x%x)", 0, 0);
        return 0;
    }
    for (i = 0; i < leaf_site_count(); i++) {
        const leaf_site_def *d = &k_leaf_sites[i];
        char sub[192];
        void *site = resolve(d->site_va);
        void *target = resolve(d->target_va);

        sub[0] = '\0';
        if (d->ret_n != 4 * d->nargs) {
            ls_fail(sub, sizeof(sub),
                    "bad ABI table entry (nargs*4=0x%x ret_n=0x%x)",
                    (unsigned)(4 * d->nargs), (unsigned)d->ret_n);
        } else if (!leaf_site_patch(site, target, d->orig_slot, d->handler,
                                    sub, (int)sizeof(sub))) {
            /* молча терять листья нельзя: маркер виден в захвате */
        } else {
            n++;
            continue;
        }
        missing++;
        if (g_ls_sink != NULL && g_ls_sink->missing != NULL) {
            g_ls_sink->missing((int)d->site_va, (int)d->target_va, sub);
        }
        if (why != NULL && cap > 0 && why[0] == '\0') {
            _snprintf_s(why, (size_t)cap, _TRUNCATE, "%s", sub);
        }
    }
    if (why != NULL && cap > 0) {
        char tail[64];
        _snprintf_s(tail, sizeof(tail), _TRUNCATE, "patched=%d missing=%d",
                    n, missing);
        if (why[0] == '\0') {
            _snprintf_s(why, (size_t)cap, _TRUNCATE, "%s", tail);
        }
    }
    return n;
}
