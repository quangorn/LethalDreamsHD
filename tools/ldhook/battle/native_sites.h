/* tools/ldhook/battle/native_sites.h -- table-driven CALL-SITE hooks for the
 * non-leaf natives of FUN_0044bdd0 (v12 `bdd0_native` on the native run).
 *
 * Why: bdd0_leaf covers all 12 logic leaves.  Every other native the body
 * calls was a no-op stub in the offline harness, so the port took early exits
 * (player_get -> zeroed shadow, phase_time -> 0.0f, ...).  This module records
 * the RESULT of every call site that the body's flow or state depends on.
 *
 * Mechanism (same guarantee as leaf_sites): a 5-byte `call rel32` site is
 * replaced by an `E8 rel32` to a per-site stub.  The stub saves registers and
 * flags, pushes the caller return address on a shared LIFO, reports the call
 * (site, target, slot, kind, ECX, args), sets [esp] to a continuation, and
 * tail-jumps to the ORIGINAL target.  The original runs with the caller's exact
 * frame/registers/x87 and its own `ret n` returns to the continuation, which
 * records EAX/AL/ST(0)/void and jumps back to the real caller.  Stack balance
 * is the original's, not ours.  Unlike leaf_sites the stubs are NOT hand-written
 * per site: tools/probes/gen_native_sites.py emits the site table, and a small
 * runtime emitter builds the stub bytes into executable memory.
 *
 * Indirect calls (`call *disp(%reg)`, 3 bytes) cannot be patched as a 5-byte
 * `E8`.  They are hooked at the VTABLE SLOT instead: the first time a tick sees
 * the unit, its vtable slots 0x28/0x2c/0x38/0x40 are redirected to generated
 * slot stubs that save the original target and record it per (site, slot).  The
 * site is recovered from the return address (site = retaddr - 3); the slot stub
 * tail-calls the saved original, so behaviour is identical for every caller.
 *
 * Result kinds: EAX int, EAX&0xff (AL), ST(0) float bits, void, and pointer
 * (EAX plus up to 3 dwords read through EAX -- the object fields the body
 * tests).  trap=1 marks a record whose capture faulted; replay never trusts it.
 */
#ifndef NATIVE_SITES_H
#define NATIVE_SITES_H

#include <windows.h>

#define NS_K_INT       0   /* EAX bits */
#define NS_K_AL        1   /* EAX & 0xff */
#define NS_K_F32       2   /* ST(0) bits */
#define NS_K_VOID      3   /* no result (side effect recorded as call event) */
#define NS_K_PTR       4   /* EAX pointer; w = 3 dwords at EAX (optional) */
#define NS_K_PTR_W3    5   /* EAX pointer; w required (tested object fields) */

#define NS_SLOT_NONE   0
#define NS_SLOT_U28    1   /* call *0x28(%reg) -- world_notify */
#define NS_SLOT_U2C    2   /* call *0x2c(%reg) -- phase_time (ST0) */
#define NS_SLOT_U38    3   /* call *0x38(%reg) -- pointer call */
#define NS_SLOT_U40    4   /* call *0x40(%reg) -- player_get */

#define NS_RET_MAX     64

struct native_site_def {
    unsigned site_va;      /* VA of the `call` instruction */
    unsigned target_va;    /* direct target; 0 for the vtable slots */
    int slot;              /* NS_SLOT_* (0 = direct) */
    int nargs;             /* stack dwords captured (clamped to 6) */
    int ret_n;             /* callee `ret n` (informational; balance is the callee's) */
    int kind;              /* NS_K_* */
};

int native_site_count(void);

/* Consumer.  enter runs on call entry (before the original), exit on the
 * continuation (after the original).  enter returns a token >= 0 or -1.
 * w/nw are dwords read through the returned pointer (kind PTR); trap=1 marks a
 * partial record. */
struct native_sink {
    int  (*enter)(int site_va, int target_va, int slot, int kind, int ecx,
                  const int *args, int nargs);
    void (*exit)(int token, int site_va, int target_va, int slot, int kind,
                 int ecx, const int *args, int nargs, int ret_bits,
                 const int *w, int nw, int trap);
    void (*missing)(int site_va, int target_va, const char *why);
};

void native_sites_set_sink(const struct native_sink *sink);
/* Drop the LIFO for a new 44bdd0 invocation (unbalanced SEH unwind). */
void native_sites_reset(void);

/* Install all direct sites.  resolve(VA) -> memory address.  A site that fails
 * the 5-byte `E8 rel32` check is skipped and reported via sink->missing (and
 * counted in `why`).  Returns the number patched. */
int native_site_install_all(void *(*resolve)(unsigned va), char *why, int cap);

/* Arm the four vtable slots of `unit` (once per vtable).  Returns 1 if the
 * vtable is (now) armed.  Called from the 44bdd0 entry capture hook. */
int native_sites_vtable_arm(void *unit, char *why, int cap);

/* Image base for VA conversion: the record's site/va are VAs (0x44xxxx), while
 * indirect slots are resolved to absolute pointers.  Set once at install. */
void native_sites_set_base(void *base);

/* Mark the current thread as the capturing thread (called at H1).  Slot-call
 * LIFOs are per-thread and only the capturing thread records / pops. */
void native_sites_set_capture_thread(void);

/* Self-test only: allow the sink to accept sites outside the body range (the
 * offline launcher lives outside 0x44bdd0..0x44d08e). */
void native_sites_set_test_allow_outside(int on);

/* Self-test hooks (no game).  Emit one stub for explicit parameters; site_va is
 * used when ret_minus==0 (direct, direct_ret = site+5), and site is recovered
 * from retaddr-ret_minus for indirect.  Returns the stub address or NULL. */
void *native_stub_emit(unsigned site_va, unsigned target_va, int slot, int kind,
                       int nargs, int ret_minus, void *orig, void *direct_ret,
                       char *why, int cap);
int native_site_patch(void *site, void *target_expected, void *stub,
                      char *why, int cap);

/* C helpers used by generated stubs (cdecl; do not call from the game).
 * direct stubs pass `state` (per-stub static entry); slot stubs pass NULL and
 * use the per-thread LIFO.  finish pops the slot LIFO; take_ret returns the
 * popped caller return address for the current thread. */
int __cdecl cap_native_args_record(int site, int target, int slot, int kind,
                                   int ecx, const int *args, int nargs,
                                   void *state);
void __cdecl cap_native_finish(int ret_bits, void *state);
void * __cdecl cap_native_take_ret(void);

#endif /* NATIVE_SITES_H */
