/* tools/ldhook/battle/native_sites.cpp -- table-driven call-site hooks.
 *
 * The generated site table (native_sites_table.inc) lists every RECORD call
 * site of the body; NOOP_OK targets are absent.  Each direct site gets a stub
 * emitted at runtime (see the header for the mechanism).  Indirect calls are
 * hooked by redirecting the four unit vtable slots once per vtable.
 *
 * Two stub shapes:
 *   direct (5-byte `call rel32`, return address statically site+5):
 *     entry reports the call into a per-stub static entry, sets the
 *     continuation, tail-jumps the original.  The continuation records the
 *     result and jumps DIRECTLY to site+5 -- no return LIFO, so a concurrent
 *     thread cannot pop a foreign return address.
 *   slot (3-byte `call *disp(%reg)`, return address = site+3):
 *     entry reports into a per-THREAD LIFO only on the capture thread; foreign
 *     threads take the transparent path (no push, no continuation).  The
 *     continuation pops its own thread's return address and jumps there.
 *
 * The original runs with the caller's exact frame, registers and x87 stack
 * (the helpers are integer-only; the only x87 traffic is the F32 continuation,
 * which restores ST(0) with fld).  `ret n` is the original's own.  All game
 * reads are under __try.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "native_sites.h"

typedef struct {
    int site, target, slot, kind, ecx, nargs;
    int args[8];
    int token;
    int trap;
} ns_ent;

/* Per-thread slot LIFO.  Direct sites do not use it. */
#define NS_TLS_MAX 8
typedef struct {
    DWORD tid;
    int n;
    void *ret_addr[NS_RET_MAX];
    ns_ent ent[NS_RET_MAX];
    void *pending_ret;
    int used;
} ns_tls;

static ns_tls g_tls[NS_TLS_MAX];
static DWORD g_cap_tid = 0;
static int g_cap_tid_set = 0;
static const native_sink *g_ns_sink = NULL;
static unsigned char *g_last_stub = NULL;
static int g_last_stub_size = 0;
static unsigned char *g_ns_base = NULL;

#include "native_sites_table.inc"

int native_site_count(void)
{
    return (int)(sizeof(k_ns_sites) / sizeof(k_ns_sites[0]));
}

const struct native_site_def *native_site_at(int i)
{
    if (i < 0 || i >= native_site_count()) return NULL;
    return &k_ns_sites[i];
}

void native_sites_set_sink(const struct native_sink *sink) { g_ns_sink = sink; }

void native_sites_set_base(void *base) { g_ns_base = (unsigned char *)base; }

void native_sites_set_capture_thread(void)
{
    g_cap_tid = GetCurrentThreadId();
    g_cap_tid_set = 1;
}

static int g_ns_allow_outside = 0;
void native_sites_set_test_allow_outside(int on) { g_ns_allow_outside = on; }

static int ns_to_va(int addr)
{
    if (g_ns_base != NULL && addr != 0) {
        return (int)((unsigned char *)(size_t)(unsigned)addr - g_ns_base) +
               0x400000;
    }
    return addr;
}

static ns_tls *tls_get(void)
{
    DWORD tid = GetCurrentThreadId();
    int i, free_i = -1;

    for (i = 0; i < NS_TLS_MAX; i++) {
        if (g_tls[i].used && g_tls[i].tid == tid) return &g_tls[i];
        if (!g_tls[i].used && free_i < 0) free_i = i;
    }
    if (free_i < 0) return NULL;
    g_tls[free_i].used = 1;
    g_tls[free_i].tid = tid;
    g_tls[free_i].n = 0;
    g_tls[free_i].pending_ret = NULL;
    return &g_tls[free_i];
}

void native_sites_reset(void)
{
    int i;
    for (i = 0; i < NS_TLS_MAX; i++) {
        if (g_tls[i].used) {
            g_tls[i].n = 0;
            g_tls[i].pending_ret = NULL;
        }
    }
}

/* ----------------------------- stub emitter ------------------------------ */

typedef struct {
    unsigned char *p;
    int n;
} Emit;

static void e8(Emit *b, unsigned v) { b->p[b->n++] = (unsigned char)v; }
static void e32(Emit *b, unsigned v)
{
    b->p[b->n++] = (unsigned char)(v & 0xff);
    b->p[b->n++] = (unsigned char)((v >> 8) & 0xff);
    b->p[b->n++] = (unsigned char)((v >> 16) & 0xff);
    b->p[b->n++] = (unsigned char)((v >> 24) & 0xff);
}
static void p32(Emit *b, const void *p) { e32(b, (unsigned)(size_t)p); }

/* FF 25 disp32 -- jmp dword ptr [disp32] (no register clobber). */
static void jmp_mem(Emit *b, const void *slot)
{
    e8(b, 0xFF); e8(b, 0x25); p32(b, slot);
}

static int rel32_ok(void *from_next, void *to)
{
    long d = (long)((const unsigned char *)to -
                    (const unsigned char *)from_next);
    return d >= -2147483647L && d <= 2147483647L;
}

void *native_stub_emit(unsigned site_va, unsigned target_va, int slot, int kind,
                       int nargs, int ret_minus, void *orig, void *direct_ret,
                       char *why, int cap)
{
    unsigned char *base;
    unsigned char *cont, *orig_slot, *ret_slot, *f32_slot, *state;
    Emit e;
    int is_direct = (ret_minus == 0);
    int safe_pos, safe_rel;
    unsigned char *call_ip;

    if (why != NULL && cap > 0) why[0] = '\0';
    if (is_direct && direct_ret == NULL) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "direct stub without return address");
        return NULL;
    }
    base = (unsigned char *)VirtualAlloc(NULL, 0x400, MEM_COMMIT | MEM_RESERVE,
                                         PAGE_EXECUTE_READWRITE);
    if (base == NULL) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "VirtualAlloc failed (%u)",
                                        (unsigned)GetLastError());
        return NULL;
    }
    cont = base + 0x100;
    orig_slot = base + 0x200;
    ret_slot = base + 0x204;
    f32_slot = base + 0x208;
    state = base + 0x210;
    *(void **)orig_slot = orig;
    *(void **)ret_slot = direct_ret;
    memset(f32_slot, 0, 4);
    memset(state, 0, sizeof(ns_ent));

    if (nargs < 0) nargs = 0;
    if (nargs > 8) nargs = 8;

    /* ------------------------------ entry ------------------------------- */
    e.p = base; e.n = 0;
    e8(&e, 0x9C);                       /* pushfd */
    e8(&e, 0x60);                       /* pushad */
    e8(&e, 0x89); e8(&e, 0xE6);         /* mov esi, esp */

    if (is_direct) { e8(&e, 0x68); p32(&e, state); }  /* push state (arg8) */
    else { e8(&e, 0x68); e32(&e, 0); }               /* push NULL state */
    e8(&e, 0x68); e32(&e, (unsigned)nargs);       /* push nargs (arg7) */
    e8(&e, 0x8D); e8(&e, 0x46); e8(&e, 0x28);     /* lea eax, [esi+0x28] */
    e8(&e, 0x50);                                 /* push eax (args, arg6) */
    e8(&e, 0x8B); e8(&e, 0x46); e8(&e, 0x18);     /* mov eax, [esi+0x18] */
    e8(&e, 0x50);                                 /* push eax (ecx, arg5) */
    e8(&e, 0x68); e32(&e, (unsigned)kind);        /* push kind (arg4) */
    e8(&e, 0x68); e32(&e, (unsigned)slot);        /* push slot (arg3) */
    e8(&e, 0x68);
    e32(&e, is_direct ? target_va : (unsigned)(size_t)orig); /* push target */
    if (is_direct) {
        e8(&e, 0x68); e32(&e, site_va);           /* push site (arg1) */
    } else {
        e8(&e, 0x8B); e8(&e, 0x46); e8(&e, 0x24); /* mov eax,[esi+0x24] */
        e8(&e, 0x83); e8(&e, 0xE8); e8(&e, (unsigned char)ret_minus);
        e8(&e, 0x50);                             /* push site (ret-3, arg1) */
    }
    e8(&e, 0xE8); call_ip = e.p + e.n; e32(&e, 0);/* call helper */
    if (!rel32_ok(e.p + e.n, (void *)cap_native_args_record)) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "helper out of rel32 range");
        VirtualFree(base, 0, MEM_RELEASE);
        return NULL;
    } else {
        long d = (long)((const unsigned char *)cap_native_args_record -
                        (const unsigned char *)(e.p + e.n));
        memcpy(call_ip, &d, 4);
    }
    e8(&e, 0x83); e8(&e, 0xC4); e8(&e, 0x20);     /* add esp, 0x20 */

    if (is_direct) {
        e8(&e, 0x61);                             /* popad */
        e8(&e, 0x9D);                             /* popfd */
        e8(&e, 0xC7); e8(&e, 0x04); e8(&e, 0x24); p32(&e, cont);
                                                  /* mov [esp], cont */
        jmp_mem(&e, orig_slot);
    } else {
        e8(&e, 0x85); e8(&e, 0xC0);               /* test eax, eax */
        e8(&e, 0x74); safe_pos = e.n; e8(&e, 0x00); /* jz foreign */
        e8(&e, 0x61);                             /* popad */
        e8(&e, 0x9D);                             /* popfd */
        e8(&e, 0xC7); e8(&e, 0x04); e8(&e, 0x24); p32(&e, cont);
                                                  /* mov [esp], cont */
        jmp_mem(&e, orig_slot);
        /* foreign: transparent, original returns to the foreign caller */
        safe_rel = (e.n - (safe_pos + 1)) & 0xff;
        if (safe_rel > 0x7f) {
            if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                            "stub entry overflow");
            VirtualFree(base, 0, MEM_RELEASE);
            return NULL;
        }
        base[safe_pos] = (unsigned char)safe_rel;
        e8(&e, 0x61);                             /* popad */
        e8(&e, 0x9D);                             /* popfd */
        jmp_mem(&e, orig_slot);
    }
    if (e.n >= 0x100) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "stub entry too large (%d)", e.n);
        VirtualFree(base, 0, MEM_RELEASE);
        return NULL;
    }

    /* --------------------------- continuation ---------------------------- */
    e.p = cont; e.n = 0;
    if (kind == NS_K_F32) {
        e8(&e, 0x50);                                   /* push eax */
        e8(&e, 0xD9); e8(&e, 0x1D); p32(&e, f32_slot);  /* fstp [tmp] */
        if (is_direct) { e8(&e, 0x68); p32(&e, state); }
        else { e8(&e, 0x6A); e8(&e, 0x00); }            /* push state */
        e8(&e, 0xFF); e8(&e, 0x35); p32(&e, f32_slot);  /* push [tmp] */
    } else if (kind == NS_K_VOID) {
        e8(&e, 0x50);                                   /* push eax */
        if (is_direct) { e8(&e, 0x68); p32(&e, state); }
        else { e8(&e, 0x6A); e8(&e, 0x00); }            /* push state */
        e8(&e, 0x6A); e8(&e, 0x00);                     /* push 0 */
    } else if (kind == NS_K_AL) {
        e8(&e, 0x50);                                   /* push eax */
        e8(&e, 0x0F); e8(&e, 0xB6); e8(&e, 0xC0);       /* movzx eax, al */
        if (is_direct) { e8(&e, 0x68); p32(&e, state); }
        else { e8(&e, 0x6A); e8(&e, 0x00); }            /* push state */
        e8(&e, 0x50);                                   /* push eax */
    } else {
        e8(&e, 0x50);                                   /* push eax (save) */
        if (is_direct) { e8(&e, 0x68); p32(&e, state); }
        else { e8(&e, 0x6A); e8(&e, 0x00); }            /* push state */
        e8(&e, 0x50);                                   /* push eax (ret_bits) */
    }
    e8(&e, 0xE8); call_ip = e.p + e.n; e32(&e, 0);      /* call finish */
    if (!rel32_ok(e.p + e.n, (void *)cap_native_finish)) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "finish helper out of rel32 range");
        VirtualFree(base, 0, MEM_RELEASE);
        return NULL;
    }
    {
        long d = (long)((const unsigned char *)cap_native_finish -
                        (const unsigned char *)(e.p + e.n));
        memcpy(call_ip, &d, 4);
    }
    e8(&e, 0x83); e8(&e, 0xC4); e8(&e, 0x08);           /* add esp, 8 */
    if (kind == NS_K_F32) {
        e8(&e, 0xD9); e8(&e, 0x05); p32(&e, f32_slot);  /* fld [tmp] */
        e8(&e, 0x58);                                   /* pop eax */
    } else if (kind == NS_K_VOID || kind == NS_K_AL || kind == NS_K_INT ||
               kind == NS_K_PTR || kind == NS_K_PTR_W3) {
        e8(&e, 0x58);                                   /* pop eax */
    }
    if (is_direct) {
        jmp_mem(&e, ret_slot);                          /* jmp site+5 */
    } else {
        /* pop this thread's saved caller return address (finish stored it) */
        e8(&e, 0x50);                                   /* push eax (save) */
        e8(&e, 0xE8); call_ip = e.p + e.n; e32(&e, 0);
        {
            long d = (long)((const unsigned char *)cap_native_take_ret -
                            (const unsigned char *)(e.p + e.n));
            memcpy(call_ip, &d, 4);
        }
        e8(&e, 0x89); e8(&e, 0xC2);                     /* mov edx, eax */
        e8(&e, 0x58);                                   /* pop eax (restore) */
        e8(&e, 0xFF); e8(&e, 0xE2);                     /* jmp edx */
    }
    if (e.n > 0x100) {
        if (why && cap > 0) _snprintf_s(why, (size_t)cap, _TRUNCATE,
                                        "continuation too large (%d)", e.n);
        VirtualFree(base, 0, MEM_RELEASE);
        return NULL;
    }

    FlushInstructionCache(GetCurrentProcess(), base, 0x400);
    g_last_stub = base;
    g_last_stub_size = e.n;
    return base;
}

const unsigned char *native_stub_bytes(void) { return g_last_stub; }
int native_stub_size(void) { return g_last_stub_size; }

/* ------------------------------ site patch ------------------------------- */

static int ns_fail(char *why, int cap, const char *fmt, unsigned a, unsigned b)
{
    if (why != NULL && cap > 0) {
        _snprintf_s(why, (size_t)cap, _TRUNCATE, fmt, a, b);
    }
    return 0;
}

int native_site_patch(void *site, void *target_expected, void *stub,
                      char *why, int cap)
{
    unsigned char *p = (unsigned char *)site;
    long rel;
    DWORD old = 0;

    if (site == NULL || target_expected == NULL || stub == NULL) {
        return ns_fail(why, cap, "null argument (site=0x%x target=0x%x)",
                       (unsigned)(size_t)site, (unsigned)(size_t)target_expected);
    }
    __try {
        if (p[0] != 0xE8) {
            return ns_fail(why, cap,
                           "site 0x%x is not `call rel32` (opcode 0x%x)",
                           (unsigned)(size_t)site, (unsigned)p[0]);
        }
        if ((void *)(p + 5 + *(const int *)(p + 1)) != target_expected) {
            if (why != NULL && cap > 0) {
                _snprintf_s(why, (size_t)cap, _TRUNCATE,
                            "site 0x%x calls 0x%x, expected 0x%x",
                            (unsigned)(size_t)site,
                            (unsigned)(size_t)(p + 5 + *(const int *)(p + 1)),
                            (unsigned)(size_t)target_expected);
            }
            return 0;
        }
        rel = (long)((const unsigned char *)stub - (p + 5));
        if (rel < -2147483647L || rel > 2147483647L) {
            return ns_fail(why, cap, "stub out of rel32 range (0x%x)",
                           (unsigned)(size_t)stub, 0);
        }
        if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
            return ns_fail(why, cap, "VirtualProtect failed at 0x%x (0x%x)",
                           (unsigned)(size_t)p, (unsigned)GetLastError());
        }
        p[0] = 0xE8;
        *(int *)(p + 1) = (int)rel;
        FlushInstructionCache(GetCurrentProcess(), p, 5);
        VirtualProtect(p, 5, old, &old);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return ns_fail(why, cap, "fault while patching 0x%x (0x%x)",
                       (unsigned)(size_t)site, 0);
    }
    return 1;
}

int native_site_install_all(void *(*resolve)(unsigned va), char *why, int cap)
{
    int i, n = 0, skipped = 0;

    if (why != NULL && cap > 0) why[0] = '\0';
    if (resolve == NULL) return ns_fail(why, cap, "no resolver (0x%x)", 0, 0);
    for (i = 0; i < native_site_count(); i++) {
        const native_site_def *d = &k_ns_sites[i];
        char sub[192];
        void *site, *target, *stub;

        sub[0] = '\0';
        if (d->slot != NS_SLOT_NONE) continue;   /* armed via the vtable */
        site = resolve(d->site_va);
        target = resolve(d->target_va);
        stub = native_stub_emit(d->site_va, d->target_va, 0, d->kind, d->nargs,
                                0, target, (unsigned char *)site + 5, sub,
                                (int)sizeof(sub));
        if (stub == NULL ||
            !native_site_patch(site, target, stub, sub, (int)sizeof(sub))) {
            skipped++;
            if (g_ns_sink != NULL && g_ns_sink->missing != NULL) {
                g_ns_sink->missing((int)d->site_va, (int)d->target_va, sub);
            }
            continue;
        }
        n++;
    }
    if (why != NULL && cap > 0) {
        _snprintf_s(why, (size_t)cap, _TRUNCATE, "patched=%d skipped=%d", n,
                    skipped);
    }
    return n;
}

/* --------------------------- vtable arming ------------------------------- */

#define NS_VT_MAX 8
typedef struct {
    void *vt;
    void *orig[5];      /* index by slot id 1..4 */
    void *stub[5];
} ns_vt;

static ns_vt g_vt[NS_VT_MAX];
static int g_vt_n = 0;

int native_sites_vtable_count(void) { return g_vt_n; }

static unsigned slot_disp(int slot)
{
    switch (slot) {
    case NS_SLOT_U28: return 0x28;
    case NS_SLOT_U2C: return 0x2c;
    case NS_SLOT_U38: return 0x38;
    case NS_SLOT_U40: return 0x40;
    default: return 0;
    }
}

int native_sites_vtable_arm(void *unit, char *why, int cap)
{
    static const int slots[4] = { NS_SLOT_U28, NS_SLOT_U2C,
                                  NS_SLOT_U38, NS_SLOT_U40 };
    void *vt;
    ns_vt *v = NULL;
    int i, k;

    if (why != NULL && cap > 0) why[0] = '\0';
    if (unit == NULL) return 0;
    /* The thread that first arms a vtable is the capturing thread. */
    if (!g_cap_tid_set) native_sites_set_capture_thread();
    __try {
        vt = *(void **)unit;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        vt = NULL;
    }
    if (vt == NULL) return 0;
    for (i = 0; i < g_vt_n; i++) {
        if (g_vt[i].vt == vt) { v = &g_vt[i]; break; }
    }
    if (v == NULL) {
        if (g_vt_n >= NS_VT_MAX) {
            ns_fail(why, cap, "vtable cache full (0x%x)", 0, 0);
            return 0;
        }
        v = &g_vt[g_vt_n++];
        v->vt = vt;
        for (k = 0; k < 5; k++) { v->orig[k] = NULL; v->stub[k] = NULL; }
    }
    for (i = 0; i < 4; i++) {
        int slot = slots[i];
        unsigned disp = slot_disp(slot);
        void **entry = (void **)((unsigned char *)vt + disp);
        void *orig = NULL, *stub = NULL;
        DWORD old = 0;

        __try { orig = *entry; } __except (EXCEPTION_EXECUTE_HANDLER) { orig = NULL; }
        if (orig == NULL || orig == v->stub[slot]) continue;
        stub = native_stub_emit(0, 0, slot, (slot == NS_SLOT_U2C) ? NS_K_F32
                                 : (slot == NS_SLOT_U28) ? NS_K_VOID
                                 : (slot == NS_SLOT_U38) ? NS_K_INT
                                 : NS_K_PTR,
                                (slot == NS_SLOT_U28) ? 8
                                 : (slot == NS_SLOT_U2C) ? 0 : 1,
                                3, orig, NULL, why, cap);
        if (stub == NULL) continue;
        if (!VirtualProtect(entry, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
        v->orig[slot] = orig;
        v->stub[slot] = stub;
        *entry = stub;
        FlushInstructionCache(GetCurrentProcess(), entry, 4);
        VirtualProtect(entry, 4, old, &old);
    }
    return 1;
}

/* --------------------------- capture helpers ----------------------------- */

static int tid_is_capture(void)
{
    return !g_cap_tid_set || GetCurrentThreadId() == g_cap_tid;
}

static void fill_ent(ns_ent *e, int site, int target, int slot, int kind,
                     int ecx, const int *args, int nargs)
{
    int i;

    e->site = (slot != NS_SLOT_NONE) ? ns_to_va(site) : site;
    e->target = (slot != NS_SLOT_NONE) ? ns_to_va(target) : target;
    e->slot = slot;
    e->kind = kind;
    e->ecx = ecx;
    e->nargs = (nargs > 0 && nargs <= 8) ? nargs : 0;
    e->token = -1;
    e->trap = 0;
    for (i = 0; i < 8; i++) e->args[i] = 0;
    if (e->nargs != nargs) e->trap = 1;
    if (e->nargs > 0 && args != NULL) {
        __try {
            for (i = 0; i < e->nargs; i++) e->args[i] = args[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            e->trap = 1;
            for (i = 0; i < 8; i++) e->args[i] = 0;
        }
    }
}

static void enter_sink(ns_ent *e)
{
    if (((e->site >= (int)0x44bdd0 && e->site < (int)0x44d08e) ||
         g_ns_allow_outside) &&
        tid_is_capture() && g_ns_sink != NULL && g_ns_sink->enter != NULL) {
        e->token = g_ns_sink->enter(e->site, e->target, e->slot, e->kind,
                                    e->ecx, e->args, e->nargs);
    }
}

int __cdecl cap_native_args_record(int site, int target, int slot, int kind,
                                   int ecx, const int *args, int nargs,
                                   void *state)
{
    if (state != NULL) {
        /* direct site: per-stub static entry, no LIFO */
        ns_ent *e = (ns_ent *)state;
        fill_ent(e, site, target, slot, kind, ecx, args, nargs);
        enter_sink(e);
        return 1;
    } else {
        /* slot site: per-thread LIFO, capture thread only */
        ns_tls *t = tls_get();
        ns_ent *e;
        int ret_addr = 0;

        if (t == NULL || t->n >= NS_RET_MAX || !tid_is_capture()) return 0;
        if (args != NULL) {
            __try { ret_addr = args[-1]; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }
        e = &t->ent[t->n];
        fill_ent(e, site, target, slot, kind, ecx, args, nargs);
        t->ret_addr[t->n] = (void *)(size_t)(unsigned)ret_addr;
        t->n++;
        enter_sink(e);
        return 1;
    }
}

void __cdecl cap_native_finish(int ret_bits, void *state)
{
    ns_ent *e;
    int w[3];
    int nw = 0;
    int trap;

    if (state != NULL) {
        e = (ns_ent *)state;
    } else {
        ns_tls *t = tls_get();
        if (t == NULL || t->n <= 0) return;
        e = &t->ent[t->n - 1];
        t->pending_ret = t->ret_addr[t->n - 1];
        t->n--;
    }
    trap = e->trap;
    w[0] = w[1] = w[2] = 0;
    if (e->kind == NS_K_PTR || e->kind == NS_K_PTR_W3) {
        if (ret_bits != 0) {
            __try {
                memcpy(w, (const void *)(size_t)(unsigned)ret_bits, 12);
                nw = 3;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                nw = 0;
                trap = 1;
            }
        }
    }
    if (e->token >= 0 && g_ns_sink != NULL && g_ns_sink->exit != NULL) {
        g_ns_sink->exit(e->token, e->site, e->target, e->slot, e->kind,
                        e->ecx, e->args, e->nargs, ret_bits, nw ? w : NULL,
                        nw, trap);
    }
}

void * __cdecl cap_native_take_ret(void)
{
    ns_tls *t = tls_get();
    void *p;
    if (t == NULL) return NULL;
    p = t->pending_ret;
    t->pending_ret = NULL;
    return p;
}
