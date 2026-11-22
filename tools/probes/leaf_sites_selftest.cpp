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
#include "../ldhook/battle/native_sites.h"

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

/* =================== v12: native_sites stub emitter ====================== *
 * The emitter is tested with the same ABI shapes used by the body:
 *   int (EAX), f32 (ST0), AL (EAX&0xff), void; 1 stack arg and `ret 4`.
 * Nesting: the outer target calls the inner site, so the LIFO must pair
 * enter(outer) -> enter(inner) -> exit(inner) -> exit(outer). */
static int   g_ns_arg = 0;
static int   g_ns_void = 0;

static int __stdcall ns_int(int a)  { g_ns_arg = a; return a * 5 + 2; }
static float __stdcall ns_f32(int a) { return (float)a * 0.25f; }
static int __stdcall ns_al(int a)   { g_ns_arg = a; return 0xAB00 | (a & 0xff); }
static void __stdcall ns_void(int a) { g_ns_arg = a; g_ns_void++; }

static int __declspec(naked) ns_int_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A1A1A1A1h
    __asm push eax
    __asm call ns_int
    __asm ret
}
static float __declspec(naked) ns_f32_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A2A2A2A2h
    __asm push eax
    __asm call ns_f32
    __asm ret
}
static int __declspec(naked) ns_al_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A3A3A3A3h
    __asm push eax
    __asm call ns_al
    __asm ret
}
static int __declspec(naked) ns_void_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A4A4A4A4h
    __asm push eax
    __asm call ns_void
    __asm ret
}
static int __stdcall ns_int2(int a) { g_ns_arg = a; return a + 100; }
static int __declspec(naked) ns_int2_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A2C2C2C2h
    __asm push eax
    __asm call ns_int2
    __asm ret
}
/* Outer int target that calls the (patched) inner site: nesting. */
static int __stdcall ns_nested(int a)
{
    g_ns_arg = a;
    return ns_int2_site(a) + 1;
}
static int __declspec(naked) ns_nested_site(int arg)
{
    __asm mov eax, dword ptr [esp+4]
    __asm mov ecx, 0A5A5A5A5h
    __asm push eax
    __asm call ns_nested
    __asm ret
}

typedef struct {
    int site, target, slot, kind, ecx, nargs, args[8], ret, nw, w[3], trap;
} ns_rec;
#define NS_REC_MAX 16
static ns_rec g_ns_rec[NS_REC_MAX];
static int g_ns_nrec = 0;
static int g_ns_events[NS_REC_MAX];
static int g_ns_nev = 0;
static int g_ns_missing = 0;

static int ns_sink_enter(int site, int target, int slot, int kind, int ecx,
                         const int *args, int nargs)
{
    ns_rec *r = &g_ns_rec[g_ns_nrec];
    memset(r, 0, sizeof(*r));
    r->site = site; r->target = target; r->slot = slot; r->kind = kind;
    r->ecx = ecx;
    r->nargs = (nargs > 8) ? 8 : nargs;
    for (int i = 0; i < r->nargs; i++) r->args[i] = args[i];
    g_ns_events[g_ns_nev++] = 1;             /* enter */
    return g_ns_nrec++;
}
static void ns_sink_exit(int token, int site, int target, int slot, int kind,
                         int ecx, const int *args, int nargs, int ret_bits,
                         const int *w, int nw, int trap)
{
    (void)site; (void)target; (void)slot; (void)kind; (void)ecx;
    (void)args; (void)nargs;
    if (token < 0 || token >= g_ns_nrec) return;
    g_ns_rec[token].ret = ret_bits;
    g_ns_rec[token].trap = trap;
    g_ns_rec[token].nw = nw;
    if (w != NULL) for (int i = 0; i < nw && i < 3; i++) g_ns_rec[token].w[i] = w[i];
    g_ns_events[g_ns_nev++] = 2;             /* exit */
}
static void ns_sink_missing(int site, int target, const char *why)
{
    (void)site; (void)target; (void)why;
    g_ns_missing++;
}
static const native_sink g_ns_sink = { ns_sink_enter, ns_sink_exit,
                                       ns_sink_missing };

/* ---------------- v12: two-thread slot LIFO isolation ------------------- */
static void *g_fake_vt[32];
static HANDLE g_ev_enter = NULL, g_ev_release = NULL, g_ev_done = NULL;
static volatile LONG g_worker_ret = 0;

static int __stdcall ns_slot_target(int a)
{
    g_ns_arg = a;
    if (a == 1) {
        SetEvent(g_ev_enter);
        WaitForSingleObject(g_ev_release, 5000);
    }
    return a * 10 + 1;
}

static int __declspec(naked) ns_slot_site(int arg)
{
    __asm mov edx, offset g_fake_vt
    __asm mov eax, dword ptr [esp+4]
    __asm push eax
    __asm call dword ptr [edx+40h]
    __asm ret
}

static DWORD WINAPI slot_worker(LPVOID p)
{
    (void)p;
    WaitForSingleObject(g_ev_enter, 5000);
    InterlockedExchange(&g_worker_ret, (LONG)ns_slot_site(2));
    SetEvent(g_ev_release);
    SetEvent(g_ev_done);
    return 0;
}

static int __stdcall ns_slot28_target(int a1, int a2, int a3, int a4, int a5,
                                      int a6, int a7, int a8)
{
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
}

static int __declspec(naked) ns_slot28_site(void)
{
    __asm mov edx, offset g_fake_vt
    __asm push 1
    __asm push 2
    __asm push 3
    __asm push 4
    __asm push 5
    __asm push 6
    __asm push 7
    __asm push 8
    __asm call dword ptr [edx+28h]
    __asm ret
}

static void ns_nargs8_test(char *why)
{
    void *stub;
    int r;

    g_fake_vt[0x28 / 4] = NULL;
    stub = native_stub_emit(0, 0, NS_SLOT_U28, NS_K_INT, 8, 3,
                            (void *)ns_slot28_target, NULL, why, 256);
    check("ns nargs8: 8-arg slot stub emitted", stub != NULL, why);
    if (stub == NULL) return;
    g_fake_vt[0x28 / 4] = stub;
    native_sites_set_capture_thread();
    native_sites_set_test_allow_outside(1);
    g_ns_nrec = 0;
    g_ns_nev = 0;
    r = ns_slot28_site();
    printf("    nargs8: ret=%d nargs=%d trap=%d args=[%d,%d,...,%d,%d]\n",
           r, (g_ns_nrec > 0) ? g_ns_rec[0].nargs : -1,
           (g_ns_nrec > 0) ? g_ns_rec[0].trap : -1,
           (g_ns_nrec > 0) ? g_ns_rec[0].args[0] : 0,
           (g_ns_nrec > 0) ? g_ns_rec[0].args[1] : 0,
           (g_ns_nrec > 0) ? g_ns_rec[0].args[6] : 0,
           (g_ns_nrec > 0) ? g_ns_rec[0].args[7] : 0);
    check("ns nargs8: value unchanged (sum 1..8=36)", r == 36, "");
    check("ns nargs8: 8 args recorded, trap=0",
          g_ns_nrec == 1 && g_ns_rec[0].nargs == 8 &&
          g_ns_rec[0].trap == 0, "");
    check("ns nargs8: first/last captured (8 and 1)",
          g_ns_nrec == 1 && g_ns_rec[0].args[0] == 8 &&
          g_ns_rec[0].args[7] == 1, "");
    native_sites_set_test_allow_outside(0);
}

static void ns_thread_test(char *why)
{
    void *stub;
    HANDLE th;
    int main_ret;

    g_fake_vt[0x40 / 4] = NULL;
    stub = native_stub_emit(0, 0, NS_SLOT_U40, NS_K_INT, 1, 3,
                            (void *)ns_slot_target, NULL, why, 256);
    check("ns thread: slot stub emitted", stub != NULL, why);
    if (stub == NULL) return;
    g_fake_vt[0x40 / 4] = stub;

    native_sites_set_capture_thread();          /* main is the capture thread */
    native_sites_set_test_allow_outside(1);
    g_ev_enter = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_ev_release = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_ev_done = CreateEvent(NULL, FALSE, FALSE, NULL);
    g_ns_nrec = 0;
    g_ns_nev = 0;
    g_worker_ret = 0;
    th = CreateThread(NULL, 0, slot_worker, NULL, 0, NULL);

    main_ret = ns_slot_site(1);                 /* blocks inside the original */
    WaitForSingleObject(g_ev_done, 5000);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(g_ev_enter); CloseHandle(g_ev_release); CloseHandle(g_ev_done);
    g_ev_enter = g_ev_release = g_ev_done = NULL;

    printf("    thread: main_ret=%d worker_ret=%ld records=%d\n",
           main_ret, g_worker_ret, g_ns_nrec);
    check("ns thread: capture thread returned to its own continuation (11)",
          main_ret == 11, "");
    check("ns thread: foreign thread passed through (21)",
          g_worker_ret == 21, "");
    check("ns thread: only the capture thread recorded (1 record)",
          g_ns_nrec == 1, "");
    check("ns thread: record belongs to the capture call (arg=1, ret=11)",
          g_ns_nrec == 1 && g_ns_rec[0].args[0] == 1 &&
          g_ns_rec[0].ret == 11 && g_ns_rec[0].trap == 0, "");
    native_sites_set_test_allow_outside(0);
}


static unsigned char g_bad_bytes[8] = { 0x90, 0x90, 0x90, 0x90,
                                         0x90, 0x90, 0x90, 0x90 };
static void *bad_resolver(unsigned va) { (void)va; return g_bad_bytes; }

/* Body-range fake VAs so cap_native_args_record accepts the events. */
static int ns_install(void *fn, int len, unsigned site_va, unsigned target_va,
                      int kind, void *target, char *why){
    unsigned char *site = find_call(fn, len);
    void *stub;
    if (site == NULL) return 0;
    stub = native_stub_emit(site_va, target_va, 0, kind, 1, 0, target,
                            site + 5, why, 256);
    if (stub == NULL) return 0;
    return native_site_patch(site, target, stub, why, 256);
}

static void nat_emitter_tests(void)
{
    char why[256];
    int r, i;
    float rf;
    int bits;
    unsigned char *site;
    unsigned char *stub;
    int stub_n;
    int found_tail = 0;

    native_sites_set_sink(&g_ns_sink);
    native_sites_set_base(NULL);
    g_ns_nrec = 0; g_ns_nev = 0;

    printf("native_sites emitter selftest:\n");

    /* ---- int ---- */
    check("ns: int site patched", ns_install((void *)ns_int_site, 32,
          0x44c100, 0x442570, NS_K_INT, (void *)ns_int, why), why);
    r = ns_int_site(7);
    check("ns int: value unchanged (7*5+2=37)", r == 37, "");
    check("ns int: arg captured", g_ns_rec[g_ns_nrec - 1].args[0] == 7, "");
    check("ns int: ret captured", g_ns_rec[g_ns_nrec - 1].ret == 37, "");
    check("ns int: site/va in record",
          g_ns_rec[g_ns_nrec - 1].site == 0x44c100 &&
          g_ns_rec[g_ns_nrec - 1].target == 0x442570, "");

    /* ---- f32 (ST0) ---- */
    check("ns: f32 site patched", ns_install((void *)ns_f32_site, 32,
          0x44c110, 0x45b150, NS_K_F32, (void *)ns_f32, why), why);
    rf = ns_f32_site(9);
    memcpy(&bits, &rf, 4);
    check("ns f32: result 2.25f preserved", rf == 2.25f, "");
    check("ns f32: record stores ST(0) bits",
          g_ns_rec[g_ns_nrec - 1].ret == bits, "");

    /* ---- AL ---- */
    check("ns: al site patched", ns_install((void *)ns_al_site, 32,
          0x44c120, 0x45b3e0, NS_K_AL, (void *)ns_al, why), why);
    r = ns_al_site(0x101);
    check("ns al: full EAX preserved", r == 0xAB01, "");
    check("ns al: record stores AL only",
          g_ns_rec[g_ns_nrec - 1].ret == 1, "");

    /* ---- void ---- */
    g_ns_void = 0;
    check("ns: void site patched", ns_install((void *)ns_void_site, 32,
          0x44c130, 0x448c30, NS_K_VOID, (void *)ns_void, why), why);
    r = ns_void_site(42);
    check("ns void: original ran", g_ns_void == 1 && g_ns_arg == 42, "");
    check("ns void: record present with ret=0",
          g_ns_rec[g_ns_nrec - 1].kind == NS_K_VOID &&
          g_ns_rec[g_ns_nrec - 1].ret == 0, "");

    /* ---- nesting: outer -> inner ---- */
    check("ns: nested inner (int) patched", ns_install((void *)ns_int2_site, 32,
          0x44c140, 0x442570, NS_K_INT, (void *)ns_int2, why), why);
    check("ns: nested outer (int) patched", ns_install((void *)ns_nested_site, 32,
          0x44c150, 0x45eee0, NS_K_INT, (void *)ns_nested, why), why);
    g_ns_nev = 0;
    r = ns_nested_site(5);
    printf("    nested result=%d (expect 106), events=%d\n", r, g_ns_nev);
    check("ns nested: outer value (5+100+1=106)", r == 106, "");
    check("ns nested: events enter,enter,exit,exit",
          g_ns_nev == 4 && g_ns_events[0] == 1 && g_ns_events[1] == 1 &&
          g_ns_events[2] == 2 && g_ns_events[3] == 2, "");

    /* ---- 5-byte check: non-E8 buffer is rejected (site_skipped path) ---- */
    {
        unsigned char bad[8] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
        check("ns: non-E8 site rejected",
              native_site_patch(bad, (void *)ns_int, (void *)0x11111111,
                                why, 256) == 0, why);
    }
    {
        unsigned char bad[8] = { 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
        check("ns: E8 to wrong target rejected",
              native_site_patch(bad, (void *)ns_int, (void *)0x11111111,
                                why, 256) == 0, why);
    }
    {
        /* install_all over the real table with a resolver that yields garbage:
         * every direct site must be skipped and reported via sink->missing. */
        g_ns_missing = 0;
        int np = native_site_install_all(bad_resolver, why, 256);
        printf("    install_all(bad resolver): patched=%d missing=%d (%s)\n",
               np, g_ns_missing, why);
        check("ns: bad sites skipped and reported as site_skipped",
              np == 0 && g_ns_missing > 0, "");
    }

    /* ---- patch integrity: site is E8 rel32 -> stub; stub tail-jumps to
     *      the original through a slot that holds the original pointer. ---- */
    site = find_call((void *)ns_int_site, 32);
    stub = (unsigned char *)(site + 5 + *(int *)(site + 1));
    printf("    site 0x%08x: %02X %02X %02X %02X %02X  (rel=%d -> 0x%08x)\n",
           (unsigned)(size_t)site, site[0], site[1], site[2], site[3], site[4],
           *(int *)(site + 1), (unsigned)(size_t)stub);
    check("ns integrity: site is E8 rel32 to the emitted stub",
          site[0] == 0xE8 && stub != NULL, "");
    stub_n = 0x100;             /* emitter layout: entry fits in 0x100 */
    for (i = 0; i + 6 <= stub_n; i++) {
        if (stub[i] == 0xFF && stub[i + 1] == 0x25) {
            unsigned slot_addr = *(unsigned *)(stub + i + 2);
            void *slot_val = *(void **)(size_t)slot_addr;
            if (slot_val == (void *)ns_int) found_tail = 1;
            printf("    stub tail at +%d: FF 25 %08x -> *[slot]=0x%08x\n",
                   i, slot_addr, (unsigned)(size_t)slot_val);
        }
    }
    check("ns integrity: stub tail-calls the original target", found_tail, "");

    ns_nargs8_test(why);
    ns_thread_test(why);
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

    nat_emitter_tests();

    printf("leaf_sites selftest: %s (%d failures), records=%d\n",
           fails == 0 ? "OK" : "FAILED", fails, g_nrec);
    return fails == 0 ? 0 : 1;
}
