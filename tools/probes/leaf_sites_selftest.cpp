/* tools/probes/leaf_sites_selftest.cpp — offline-проверка механизма хуков на
 * call-сайты листьев (tools/ldhook/battle/leaf_sites.{h,cpp}) БЕЗ игры.
 *
 * Для каждого вида возврата проверяется полный цикл:
 *   1. `call rel32` на заглушку (та же сигнатура/ABI, что у настоящего листа:
 *      __stdcall = `ret 4`) патчится leaf_site_patch: байты сайта обязаны быть
 *      E8 + rel32 именно на заглушку (это и есть проверка 5-байтовой длины:
 *      rel32 декодируется из байтов 1..4, E8 — байт 0);
 *   2. вызов сайта из naked-вызывающего (ECX = «приёмник», один стек-аргумент);
 *   3. хук снимает va/ECX/аргументы, ИСПОЛНЯЕТ ОРИГИНАЛ и снимает результат;
 *   4. значение, которое увидел вызывающий, НЕ изменилось (сравнение с прямым
 *      вызовом заглушки), а стек/регистры остались в порядке (иначе упало бы).
 *
 * Ран: leaf_sites_selftest.exe (сборка — leaf_sites_selftest_build.py).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "../ldhook/battle/leaf_sites.h"

/* ------------------------------- заглушки -------------------------------- *
 * __stdcall → эпилог `ret 4` (один стек-аргумент), как у 45c6f0/403e80. */
static int g_peak_arg = 0;

static int __stdcall stub_int(int a)
{
    g_peak_arg = a;
    return a * 3 + 1;
}

static float __stdcall stub_f32(int a)
{
    return (float)a * 0.5f;                /* x87: результат в ST(0) */
}

static int __stdcall stub_al(int a)
{
    g_peak_arg = a;
    return 0x7F00 | (a & 0xFF);            /* тело читает только AL */
}

/* Тройка, записанная «через указатель»: как 45e1d0 (w по возврату). */
static int g_triple[3] = { 11, 22, 33 };

static int __stdcall stub_w3(int a)
{
    g_peak_arg = a;
    return (int)(size_t)&g_triple[0];
}

/* Не дать компоновщику выбросить заглушки: на них ссылается только asm. */
static volatile void *g_keep[4] = {
    (void *)&stub_int, (void *)&stub_f32, (void *)&stub_al, (void *)&stub_w3
};

/* --------------------------- вызывающие сайты ---------------------------- *
 * `call` внутри — ровно 5-байтовый E8 rel32; его и патчит установщик. */
static int __declspec(naked) call_int_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 1234ABCDh
    __asm push eax
    __asm call stub_int
    __asm ret
}

static float __declspec(naked) call_f32_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 22220000h
    __asm push eax
    __asm call stub_f32
    __asm ret
}

static int __declspec(naked) call_al_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 33330000h
    __asm push eax
    __asm call stub_al
    __asm ret
}

static int __declspec(naked) call_w3_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 44440000h
    __asm push eax
    __asm call stub_w3
    __asm ret
}

/* ------------------------- обработчики (как в игре) ---------------------- */
LS_SITE(901, 0xDEAD0001, 1, 4, INT, LS_K_INT)
LS_SITE(902, 0xDEAD0002, 1, 4, F32, LS_K_F32)
LS_SITE(903, 0xDEAD0003, 1, 4, AL,  LS_K_AL)
LS_SITE(904, 0xDEAD0004, 1, 4, INT, LS_K_INT_W3_RET)

/* ------------------------------- sink ------------------------------------ */
typedef struct {
    int va, ecx, nargs;
    int args[6];
    int ret_bits;
    int nw, w[3];
    int trap;
    int used;
} rec;

static rec  g_rec[8];
static int  g_nrec = 0;
static int  g_missing = 0;

static int sink_enter(int va, int ecx, const int *args, int nargs)
{
    rec *r = &g_rec[g_nrec];
    memset(r, 0, sizeof(*r));
    r->va = va;
    r->ecx = ecx;
    r->nargs = nargs;
    if (args != NULL) memcpy(r->args, args, (size_t)(nargs > 6 ? 6 : nargs) * 4);
    r->used = 1;
    return g_nrec++;
}

static void sink_exit(int token, int va, int ecx, const int *args, int nargs,
                      int ret_bits, const int *w, int nw, int trap)
{
    rec *r;

    (void)va; (void)ecx; (void)args; (void)nargs;
    if (token < 0 || token >= g_nrec) return;
    r = &g_rec[token];
    r->ret_bits = ret_bits;
    r->nw = nw;
    if (w != NULL && nw > 0) memcpy(r->w, w, (size_t)(nw > 3 ? 3 : nw) * 4);
    r->trap = trap;
}

static void sink_missing(int site, int va, const char *why)
{
    (void)site; (void)va; (void)why;
    g_missing++;
}

static const leaf_sink g_sink = { sink_enter, sink_exit, sink_missing };

/* Поиск единственного `call rel32` в теле функции (короткая naked-функция). */
static unsigned char *find_call(void *fn, int len)
{
    unsigned char *p = (unsigned char *)fn;
    int i;

    for (i = 0; i + 5 <= len; i++) {
        if (p[i] == 0xE8) return p + i;
    }
    return NULL;
}

static int fails = 0;

static void check(const char *what, int ok, const char *detail)
{
    printf("  [%s] %s%s\n", ok ? "PASS" : "FAIL", what,
           (detail != NULL) ? detail : "");
    if (!ok) fails++;
}

int main(void)
{
    unsigned char *site;
    char why[256];
    int i, r;
    float rf;
    int bits;
    rec *last;

    leaf_sites_set_sink(&g_sink);
    printf("leaf_sites selftest: %d sites in table, LS_RET_MAX=%d\n",
           leaf_site_count(), LS_RET_MAX);

    /* ---- 1. INT: аргумент + результат + неизменность значения ---- */
    site = find_call((void *)call_int_site, 32);
    check("int site found (E8)", site != NULL, "");
    check("int site is 5 bytes and calls stub_int (patch validates rel32)",
          leaf_site_patch(site, (void *)stub_int, &g_ls_orig901,
                          (void *)cap_ls901_hook, why, (int)sizeof(why)),
          "");
    printf("    patched bytes: %02X %02X %02X %02X %02X\n",
           site[0], site[1], site[2], site[3], site[4]);
    r = call_int_site(7);
    last = &g_rec[g_nrec - 1];
    check("int: launcher returned 7*3+1=22", r == 22, "");
    check("int: original stub really ran (arg seen by callee = 7)",
          g_peak_arg == 7, "");
    check("int: recorded va=0xDEAD0001", last->va == 0xDEAD0001, "");
    check("int: recorded ecx=0x1234ABCD",
          (unsigned)last->ecx == 0x1234ABCDu, "");
    check("int: recorded args[0]=7",
          last->nargs == 1 && last->args[0] == 7, "");
    check("int: recorded ret=22 (EAX bits)", last->ret_bits == 22, "");
    check("int: record not marked trap", last->trap == 0, "");

    /* ---- 2. F32: значение ST(0) и его биты ---- */
    site = find_call((void *)call_f32_site, 32);
    check("f32 site found (E8)", site != NULL, "");
    check("f32 site patched",
          leaf_site_patch(site, (void *)stub_f32, &g_ls_orig902,
                          (void *)cap_ls902_hook, why, (int)sizeof(why)),
          "");
    rf = call_f32_site(9);
    last = &g_rec[g_nrec - 1];
    memcpy(&bits, &rf, 4);
    check("f32: launcher got 4.5f (ST(0) preserved)", rf == 4.5f, "");
    printf("    float=%.3f bits=0x%08X recorded=0x%08X\n",
           rf, (unsigned)bits, (unsigned)last->ret_bits);
    check("f32: recorded ret = bits(ST(0))",
          (unsigned)last->ret_bits == (unsigned)bits, "");
    check("f32: recorded ecx", (unsigned)last->ecx == 0x22220000u, "");

    /* ---- 3. AL: маска, полный EAX вызывающему не испорчен ---- */
    site = find_call((void *)call_al_site, 32);
    check("al site found (E8)", site != NULL, "");
    check("al site patched",
          leaf_site_patch(site, (void *)stub_al, &g_ls_orig903,
                          (void *)cap_ls903_hook, why, (int)sizeof(why)),
          "");
    r = call_al_site(0x101);
    last = &g_rec[g_nrec - 1];
    check("al: launcher got full EAX 0x7F01", r == 0x7F01, "");
    check("al: recorded ret = AL only (1)", last->ret_bits == 1, "");

    /* ---- 4. w: три dword'а, записанные листом по возвращённому указателю ---- */
    site = find_call((void *)call_w3_site, 32);
    check("w3 site patched",
          site != NULL &&
          leaf_site_patch(site, (void *)stub_w3, &g_ls_orig904,
                          (void *)cap_ls904_hook, why, (int)sizeof(why)),
          "");
    r = call_w3_site(5);
    last = &g_rec[g_nrec - 1];
    check("w3: launcher got the triple pointer",
          (unsigned)r == (unsigned)(size_t)&g_triple[0], "");
    check("w3: recorded ret = pointer",
          (unsigned)last->ret_bits == (unsigned)(size_t)&g_triple[0], "");
    check("w3: recorded w = {11,22,33}",
          last->nw == 3 && last->w[0] == 11 && last->w[1] == 22 &&
          last->w[2] == 33, "");

    /* ---- 5. таблица игры: 32 сайта, ABI ret_n == 4*nargs ---- */
    for (i = 0; i < leaf_site_count(); i++) {
        const leaf_site_def *d = leaf_site_at(i);
        if (d == NULL || d->ret_n != 4 * d->nargs || d->handler == NULL) {
            printf("  [FAIL] table entry %d\n", i + 1);
            fails++;
        }
    }
    check("game table: 32 sites, all ret_n == 4*nargs", fails == 0, "");
    check("no missing-site markers in selftest", g_missing == 0, "");

    printf("leaf_sites selftest: %s (%d failures), records=%d\n",
           fails == 0 ? "OK" : "FAILED", fails, g_nrec);
    return fails == 0 ? 0 : 1;
}
