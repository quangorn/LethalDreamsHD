/* tools/probes/bdd0/native_replay.cpp — replay-слой логических листьев тика.
 *
 * Десять листьев тела FUN_0044bdd0 (45c6f0, 45c370, 45af50, 45c730, 45b150,
 * 45b3e0, 403e80, 458050, 45e1d0, 44bbf0) не исполняются офлайн: каждый
 * запрос «снимает» следующую запись потока bdd0_leaf ЭТОГО seq и отдаёт её
 * ret. Поток строгий: другой va или исчерпание — нарушение, которое ловит
 * проверка `leaf_seq` (тик при этом доигрывается на модельных значениях, чтобы
 * отчёт дошёл до конца, а не оборвался).
 *
 * Указатели: ret листа — адрес в пространстве игры. Он переводится в
 * shadow-адрес арены (arena_resolve_ptr: привязки снимка, иначе opaque-объект).
 * Так `band_head → band_count`, `grid_anchor → record` и т.п. не разыменовывают
 * игровую память.
 *
 * Побочные записи листа (dword'ы, которые натив писал ЧЕРЕЗ указатель) берутся
 * из необязательного ключа `w` записи; если его нет — из документированной
 * семантики листа (45c6f0 = три dword `[ctx+0xc]`), см. native.h.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "harness.h"

#define LEAF_LOG_MAX 512

static int fbits(float f)
{
    int b;
    memcpy(&b, &f, 4);
    return b;
}

static const LeafRec *g_v = NULL;
static int g_n = 0;
static int g_i = 0;             /* потреблено записей */
static int g_seq = 0;
static int g_viol = 0;
static char g_why[192];
static CallLogEnt g_log[LEAF_LOG_MAX];
static int g_log_n = 0;

void bdd0_leaf_begin(int seq, const LeafRec *v, int n)
{
    g_v = v; g_n = n; g_i = 0; g_seq = seq;
    g_viol = 0; g_why[0] = '\0';
    g_log_n = 0;
}

int bdd0_leaf_calls(void) { return g_log_n; }
int bdd0_leaf_violations(void) { return g_viol; }
const char *bdd0_leaf_violation_why(void) { return g_why; }

const CallLogEnt *bdd0_calllog(int *n)
{
    if (n != NULL) *n = g_log_n;
    return g_log;
}

void bdd0_calllog_reset(void)
{
    g_log_n = 0;
    g_viol = 0;
    g_why[0] = '\0';
}

static void viol(const char *fmt, ...)
{
    if (g_viol) return;                    /* первое нарушение информативно */
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(g_why, sizeof(g_why), fmt, ap);
    va_end(ap);
    g_why[sizeof(g_why) - 1] = '\0';
    g_viol = 1;
}

/* Запрос листа: журнал + строгое сопоставление с потоком. NULL — нарушения
 * нет и запись отдана; иначе NULL-запись (вызывающий берёт модель). */
static const LeafRec *request(int va, const void *ecx, int nargs, const int *args)
{
    if (g_log_n < LEAF_LOG_MAX) {
        CallLogEnt &e = g_log[g_log_n];
        e.order = g_log_n;
        e.va = va;
        e.ecx = (int)(size_t)ecx;
        e.nargs = nargs;
        for (int k = 0; k < 6; k++) e.args[k] = (nargs > k) ? args[k] : 0;
        e.ret = 0;
        g_log_n++;
    } else {
        viol("leaf call log overflow at call %d (va=0x%x)", g_log_n, va);
    }
    if (g_viol) return NULL;
    if (g_i >= g_n) {
        viol("leaf stream exhausted at call %d: requested va=0x%x, "
             "%d records for seq %d", g_i, va, g_n, g_seq);
        return NULL;
    }
    const LeafRec *r = &g_v[g_i];
    if (r->va != va) {
        viol("leaf #%d: requested va=0x%x, recorded va=0x%x (seq %d)",
             g_i, va, r->va, g_seq);
        return NULL;
    }
    if (r->order != g_i) {
        viol("leaf #%d: recorded order %d (seq %d)", g_i, r->order, g_seq);
        return NULL;
    }
    g_i++;
    if (g_log_n > 0) g_log[g_log_n - 1].ret = r->ret;
    return r;
}

static unsigned read_u32(const void *p)
{
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

/* Возврат листа → shadow-адрес (игровые указатели не утекают в тело). */
static void *ret_ptr(const LeafRec *r)
{
    if (r == NULL || r->ret == 0) return NULL;
    return arena_resolve_ptr((unsigned)r->ret);
}

/* ------------------------------- листья --------------------------------- */
namespace native {

/* 45c6f0 — 3 dword из [ctx+0xc] в out; EAX = out (тело возврат не читает). */
void *ctx_pos_get(void *ctx, float *out_f)
{
    int a[1];
    a[0] = (int)(size_t)out_f;
    const LeafRec *r = request(0x45c6f0, ctx, 1, a);
    if (out_f == NULL) return NULL;
    if (r != NULL && r->nw == 3) {
        memcpy(out_f, r->w, 12);
        return out_f;
    }
    if (ctx != NULL && arena_addr_ok(ctx, 0x18)) {
        memcpy(out_f, (const unsigned char *)ctx + 0xc, 12);
    } else {
        out_f[0] = out_f[1] = out_f[2] = 0.0f;
    }
    return out_f;
}

/* 45c370 — голова ленты: [ai+0x3c] != 0 ? ai+0x3c : NULL. */
void *band_head(void *ai)
{
    const LeafRec *r = request(0x45c370, ai, 0, NULL);
    if (r != NULL) return ret_ptr(r);
    if (ai != NULL && arena_addr_ok((const unsigned char *)ai + 0x3c, 4) &&
        read_u32((const unsigned char *)ai + 0x3c) != 0) {
        return (unsigned char *)ai + 0x3c;
    }
    return NULL;
}

/* 45af50 — длина ленты: ([+0x24] − [+0x20]) / 0x18 (знаковое). */
int band_count(void *band)
{
    const LeafRec *r = request(0x45af50, band, 0, NULL);
    if (r != NULL) return r->ret;
    if (band != NULL && arena_addr_ok((const unsigned char *)band + 0x20, 8)) {
        return (int)(read_u32((const unsigned char *)band + 0x24) -
                     read_u32((const unsigned char *)band + 0x20)) /
               BDD0_BAND_ELEM;
    }
    return 0;
}

/* 45c730 — курсор ленты: [[[ctx]+0xc]+0xc]+0x234 с NULL-guard'ами. */
void *band_cursor(void *ai)
{
    const LeafRec *r = request(0x45c730, ai, 0, NULL);
    if (r != NULL) return ret_ptr(r);
    if (ai == NULL || !arena_addr_ok(ai, 4)) return NULL;
    unsigned p = read_u32(ai);
    if (p == 0) return NULL;
    void *s1 = arena_resolve_ptr(p);
    if (s1 == NULL || !arena_addr_ok((const unsigned char *)s1 + 0xc, 4)) return NULL;
    unsigned p2 = read_u32((const unsigned char *)s1 + 0xc);
    if (p2 == 0) return NULL;
    void *s2 = arena_resolve_ptr(p2);
    if (s2 == NULL || !arena_addr_ok((const unsigned char *)s2 + 0xc, 4)) return NULL;
    unsigned p3 = read_u32((const unsigned char *)s2 + 0xc);
    if (p3 == 0) return NULL;
    void *s3 = arena_resolve_ptr(p3);
    if (s3 == NULL || !arena_addr_ok((const unsigned char *)s3 + 0x234, 4)) return NULL;
    unsigned p4 = read_u32((const unsigned char *)s3 + 0x234);
    return (p4 == 0) ? NULL : arena_resolve_ptr(p4);
}

/* 45b150 — float-гейт ленты; результат ST(0) → *out_f. */
void band_interp_gate(void *band, float p2, int p3, float p4, float *out_f)
{
    int a[3];
    a[0] = fbits(p2);
    a[1] = p3;
    a[2] = fbits(p4);
    const LeafRec *r = request(0x45b150, band, 3, a);
    if (out_f == NULL) return;
    if (r != NULL) {
        memcpy(out_f, &r->ret, 4);           /* ret = биты ST(0) */
    } else {
        *out_f = 0.0f;
    }
}

/* 45b3e0 — предикат ленты «цель сдвинулась/удалена», AL. */
int band_target_moved(void *band, float p1, int p2, float p3)
{
    int a[3];
    a[0] = fbits(p1);
    a[1] = p2;
    a[2] = fbits(p3);
    const LeafRec *r = request(0x45b3e0, band, 3, a);
    if (r != NULL) return r->ret;
    return 0;
}

/* 403e80 — почленное равенство двух float-троек (unord = «не равно»). */
int pos3_equal(const void *a, const void *b)
{
    int av[1];
    av[0] = (int)(size_t)b;
    const LeafRec *r = request(0x403e80, a, 1, av);
    if (r != NULL) return r->ret;
    if (a == NULL || b == NULL || !arena_addr_ok(a, 12) || !arena_addr_ok(b, 12)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        float x, y;
        memcpy(&x, (const unsigned char *)a + i * 4, 4);
        memcpy(&y, (const unsigned char *)b + i * 4, 4);
        if (!(x == y)) return 0;
    }
    return 1;
}

/* 458050 — grid-якорь: arg5 — байт-флаг (1 найдено / 0 нет). */
void *grid_anchor(void *grid, int a1, int a2, int a3, int a4, int *flag_slot,
                  void *world)
{
    int a[6];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    a[4] = (int)(size_t)flag_slot;
    a[5] = (int)(size_t)world;
    const LeafRec *r = request(0x458050, grid, 6, a);
    void *res = ret_ptr(r);
    if (flag_slot != NULL) {
        int keep = *flag_slot & ~0xff;
        *flag_slot = keep | ((res != NULL) ? 1 : 0);
    }
    return res;
}

/* 45e1d0 — grid-recompute: 3 dword возвращённой тройки копируются в out3. */
void grid_recompute(void *rec_pf, void *pos, void *req, float out3[3])
{
    int a[2];
    a[0] = (int)(size_t)pos;
    a[1] = (int)(size_t)req;
    const LeafRec *r = request(0x45e1d0, rec_pf, 2, a);
    if (out3 == NULL) return;
    if (r != NULL && r->nw == 3) {
        memcpy(out3, r->w, 12);
    } else {
        out3[0] = out3[1] = out3[2] = 0.0f;
    }
}

/* 44bbf0 — grid_query: возврат — int аккумулятора. */
int grid_query(void *world, int a1, int a2, int a3, int a4)
{
    int a[4];
    a[0] = a1; a[1] = a2; a[2] = a3; a[3] = a4;
    const LeafRec *r = request(0x44bbf0, world, 4, a);
    if (r != NULL) return r->ret;
    return 0;
}

} /* namespace native */
