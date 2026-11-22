/* tools/probes/bdd0/path_diff.cpp -- --path-diff diagnostic (no verdicts).
 *
 * Side-by-side table of the recorded call order and the port's actual call
 * order for one tick.  Both sides are the union of bdd0_native + bdd0_leaf,
 * ordered by the shared per-tick order (recorded) / global request sequence
 * (consumed).  Reporting mode only: it is not a check and does not affect any
 * pass/skip/fail.
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"

struct PathRow {
    int order;
    int r_cls;                  /* 0 none, 1 leaf, 2 native */
    int r_site, r_va, r_slot, r_ret, r_kind;
    int r_ecx, r_nargs, r_args[8];
    int c_cls;
    int c_site, c_va, c_slot, c_ecx, c_nargs, c_args[8];
    int c_matched;
    int match;
};

static const char *kind_name(int kind)
{
    switch (kind) {
    case 1: return "al";
    case 2: return "f32";
    case 3: return "void";
    case 4: return "ptr";
    case 5: return "ptr";
    default: return "int";
    }
}

static void push_leaf(PathRow &r, const LeafRec *l)
{
    r.r_cls = 1;
    r.r_site = l->site;
    r.r_va = l->va;
    r.r_slot = -1;
    r.r_ret = l->ret;
    r.r_kind = -1;
    r.r_ecx = l->ecx;
    r.r_nargs = l->nargs;
    for (int i = 0; i < 8; i++) r.r_args[i] = (i < l->nargs) ? l->args[i] : 0;
}

static void push_native(PathRow &r, const NativeRec *n)
{
    r.r_cls = 2;
    r.r_site = n->site;
    r.r_va = n->va;
    r.r_slot = n->slot;
    r.r_ret = n->ret;
    r.r_kind = n->kind;
    r.r_ecx = n->ecx;
    r.r_nargs = n->nargs;
    for (int i = 0; i < 8; i++) r.r_args[i] = (i < n->nargs) ? n->args[i] : 0;
}

static int rows_match(const PathRow &r)
{
    if (r.r_cls != r.c_cls || r.r_cls == 0) return 0;
    if (r.r_cls == 1) return (r.r_va == r.c_va);
    return (r.r_slot == r.c_slot) &&
           (r.r_slot != 0 || r.r_va == r.c_va);
}

static int fields_differ(const PathRow &r)
{
    int i;
    if (r.r_cls == 0 || r.c_cls == 0) return 0;
    if (r.r_ecx != r.c_ecx || r.r_nargs != r.c_nargs) return 1;
    for (i = 0; i < r.r_nargs && i < r.c_nargs && i < 8; i++) {
        if (r.r_args[i] != r.c_args[i]) return 1;
    }
    return 0;
}

static void fmt_recorded(const PathRow &r, char *buf, int cap)
{
    if (r.r_cls == 0) {
        _snprintf_s(buf, (size_t)cap, _TRUNCATE, "-");
    } else if (r.r_cls == 1) {
        _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%06x %06x leaf ret=0x%x",
                    (unsigned)r.r_site, (unsigned)r.r_va, (unsigned)r.r_ret);
    } else if (r.r_slot >= 0) {
        _snprintf_s(buf, (size_t)cap, _TRUNCATE,
                    "%06x slot%d %06x %s ret=0x%x", (unsigned)r.r_site,
                    r.r_slot, (unsigned)r.r_va, kind_name(r.r_kind),
                    (unsigned)r.r_ret);
    } else {
        _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%06x %06x %s ret=0x%x",
                    (unsigned)r.r_site, (unsigned)r.r_va,
                    kind_name(r.r_kind), (unsigned)r.r_ret);
    }
}

static void fmt_consumed(const PathRow &r, const PathRow &ref,
                         const int *slot_va, const int *slot_kind,
                         char *buf, int cap)
{
    int i, n;
    if (r.c_cls == 0) {
        _snprintf_s(buf, (size_t)cap, _TRUNCATE, "-");
        return;
    }
    int cva = r.c_va;
    const char *ck = kind_name(0);
    if (r.c_slot > 0) {
        if (cva == 0 && slot_va != NULL) cva = slot_va[r.c_slot];
        if (slot_kind != NULL && slot_kind[r.c_slot] != 0) {
            ck = kind_name(slot_kind[r.c_slot]);
        }
    }
    if (r.c_cls == 1) {
        n = _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%06x %06x leaf",
                        (unsigned)(r.c_site ? r.c_site : ref.r_site),
                        (unsigned)r.c_va);
    } else if (r.c_slot >= 0) {
        n = _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%06x slot%d %06x %s",
                        (unsigned)(r.c_site ? r.c_site : ref.r_site),
                        r.c_slot, (unsigned)cva, ck);
    } else {
        n = _snprintf_s(buf, (size_t)cap, _TRUNCATE, "%06x %06x %s",
                        (unsigned)(r.c_site ? r.c_site : ref.r_site),
                        (unsigned)r.c_va, ck);
    }
    if (n < 0 || n >= cap) return;
    if (fields_differ(r)) {
        int m = _snprintf_s(buf + n, (size_t)(cap - n), _TRUNCATE,
                            " ecx=0x%08x args=[", (unsigned)r.c_ecx);
        if (m < 0) return;
        n += m;
        for (i = 0; i < r.c_nargs && i < 8 && n < cap - 12; i++) {
            m = _snprintf_s(buf + n, (size_t)(cap - n), _TRUNCATE, "%s0x%x",
                            i ? "," : "", (unsigned)r.c_args[i]);
            if (m < 0) return;
            n += m;
        }
        _snprintf_s(buf + n, (size_t)(cap - n), _TRUNCATE, "]");
    }
}

static void json_row(FILE *f, const PathRow &r, int seq)
{
    fprintf(f, "{\"seq\":%d,\"order\":%d,", seq, r.order);
    if (r.r_cls == 0) {
        fputs("\"recorded\":null,", f);
    } else {
        fprintf(f, "\"recorded\":{\"cls\":\"%s\",\"site\":\"%x\",\"va\":\"%x\","
                   "\"slot\":%d,\"ret\":%d,\"ecx\":%d,\"args\":[",
                r.r_cls == 1 ? "leaf" : "native", (unsigned)r.r_site,
                (unsigned)r.r_va, r.r_slot, r.r_ret, r.r_ecx);
        for (int i = 0; i < r.r_nargs && i < 8; i++) {
            fprintf(f, "%s%d", i ? "," : "", r.r_args[i]);
        }
        fputs("]},", f);
    }
    if (r.c_cls == 0) {
        fputs("\"consumed\":null,", f);
    } else {
        fprintf(f, "\"consumed\":{\"cls\":\"%s\",\"site\":\"%x\",\"va\":\"%x\","
                   "\"slot\":%d,\"ecx\":%d,\"args\":[",
                r.c_cls == 1 ? "leaf" : "native", (unsigned)r.c_site,
                (unsigned)r.c_va, r.c_slot, r.c_ecx);
        for (int i = 0; i < r.c_nargs && i < 8; i++) {
            fprintf(f, "%s%d", i ? "," : "", r.c_args[i]);
        }
        fputs("]},", f);
    }
    fprintf(f, "\"match\":%d}\n", r.match);
}

void bdd0_path_diff(const Scenario *sc, const TickRecord *t,
                    const TickContext &tc, FILE *json)
{
    const LeafRec *lv = (t->leaf_n > 0) ? &sc->leaves[t->leaf_first] : NULL;
    const NativeRec *nv = (t->native_n > 0) ? &sc->natives[t->native_first]
                                            : NULL;
    int ln = t->leaf_n, nn = t->native_n;
    int li = 0, ni = 0;
    int lc_n = 0, nc_n = 0;
    const CallLogEnt *llog = bdd0_calllog(&lc_n);
    const NativeCallLogEnt *nlog = bdd0_native_calllog(&nc_n);
    int lc = 0, nc = 0;
    std::vector<PathRow> rec;
    std::vector<PathRow> con;
    int i, nr, ncons, common, maxn, first_div = -1, last_ok = -1;
    int slot_va[5] = {0, 0, 0, 0, 0};
    int slot_kind[5] = {0, 0, 0, 0, 0};
    char rb[160], cb[220], evidence[96];
    float g114;
    unsigned gbits = t->gate114;

    while (li < ln || ni < nn) {
        PathRow r;
        memset(&r, 0, sizeof(r));
        r.r_slot = -1;
        if (ni >= nn || (li < ln && lv[li].order < nv[ni].order)) {
            push_leaf(r, &lv[li]);
            r.order = lv[li].order;
            li++;
        } else {
            push_native(r, &nv[ni]);
            r.order = nv[ni].order;
            ni++;
        }
        rec.push_back(r);
    }
    while (lc < lc_n || nc < nc_n) {
        PathRow r;
        memset(&r, 0, sizeof(r));
        r.c_slot = -1;
        if (nc >= nc_n || (lc < lc_n && llog[lc].call_seq < nlog[nc].call_seq)) {
            const CallLogEnt &e = llog[lc];
            const LeafRec *m = bdd0_leaf_record_at(lc);
            r.c_cls = 1;
            r.c_site = (m != NULL) ? m->site : 0;
            r.c_va = e.va;
            r.c_ecx = e.ecx;
            r.c_nargs = e.nargs;
            for (i = 0; i < 8; i++) r.c_args[i] = (i < e.nargs) ? e.args[i] : 0;
            r.c_matched = (m != NULL);
            r.order = e.call_seq;
            lc++;
        } else {
            const NativeCallLogEnt &e = nlog[nc];
            const NativeRec *m = bdd0_native_record_at(nc);
            r.c_cls = 2;
            r.c_site = (m != NULL) ? m->site : e.site;
            r.c_va = e.va;
            r.c_slot = e.slot;
            r.c_ecx = e.ecx;
            r.c_nargs = e.nargs;
            for (i = 0; i < 8; i++) r.c_args[i] = (i < e.nargs) ? e.args[i] : 0;
            r.c_matched = (m != NULL);
            r.order = e.call_seq;
            nc++;
        }
        con.push_back(r);
    }

    nr = (int)rec.size();
    ncons = (int)con.size();
    common = (nr < ncons) ? nr : ncons;
    maxn = (nr > ncons) ? nr : ncons;

    for (i = 0; i < (int)sc->natives.size(); i++) {
        const NativeRec &n = sc->natives[(size_t)i];
        if (n.slot > 0 && n.slot < 5) {
            if (slot_va[n.slot] == 0) slot_va[n.slot] = n.va;
            slot_kind[n.slot] = n.kind;
        }
    }
    for (i = 0; i < nr; i++) {
        if (rec[i].r_cls == 2 && rec[i].r_slot > 0 && rec[i].r_slot < 5) {
            if (slot_va[rec[i].r_slot] == 0) slot_va[rec[i].r_slot] = rec[i].r_va;
            slot_kind[rec[i].r_slot] = rec[i].r_kind;
        }
    }
    memset(&g114, 0, sizeof(g114));
    memcpy(&g114, &gbits, 4);
    printf("seq=%d  path-diff (recorded %d, consumed %d)\n",
           t->seq, nr, ncons);
    printf("  #  recorded(site,va,kind,ret)        | consumed(site,va,kind)"
           "                         | match\n");
    for (i = 0; i < common; i++) {
        PathRow r;
        PathRow c;
        memset(&r, 0, sizeof(r));
        memset(&c, 0, sizeof(c));
        r.r_slot = c.c_slot = -1;
        r = rec[i];
        c = con[i];
        c.r_cls = r.r_cls;
        c.r_site = r.r_site;
        c.r_va = r.r_va;
        c.r_slot = r.r_slot;
        c.r_ret = r.r_ret;
        c.r_kind = r.r_kind;
        c.r_nargs = r.r_nargs;
        c.r_ecx = r.r_ecx;
        for (int q = 0; q < 8; q++) c.r_args[q] = r.r_args[q];
        c.order = r.order;
        c.match = rows_match(c);
        if (!c.match && first_div < 0) first_div = i;
        if (c.match) last_ok = i;
        fmt_recorded(r, rb, sizeof(rb));
        fmt_consumed(c, r, slot_va, slot_kind, cb, sizeof(cb));
        printf("  %-2d %-36s | %-45s | %s\n", r.order, rb, cb,
               c.match ? "ok" : "DIVERGE");
        if (json != NULL) json_row(json, c, t->seq);
    }
    if (json != NULL) {
        for (i = common; i < maxn; i++) {
            PathRow c;
            memset(&c, 0, sizeof(c));
            c.c_slot = -1;
            if (i < nr) {
                PathRow r = rec[i];
                c.r_cls = r.r_cls;
                c.r_site = r.r_site;
                c.r_va = r.r_va;
                c.r_slot = r.r_slot;
                c.r_ret = r.r_ret;
                c.r_kind = r.r_kind;
                c.r_nargs = r.r_nargs;
                c.r_ecx = r.r_ecx;
                for (int q = 0; q < 8; q++) c.r_args[q] = r.r_args[q];
                c.order = r.order;
            }
            if (i < ncons) {
                c.c_cls = con[i].c_cls;
                c.c_site = con[i].c_site;
                c.c_va = con[i].c_va;
                c.c_slot = con[i].c_slot;
                c.c_ecx = con[i].c_ecx;
                c.c_nargs = con[i].c_nargs;
                for (int q = 0; q < 8; q++) c.c_args[q] = con[i].c_args[q];
                if (i >= nr) c.order = con[i].order;
            }
            c.match = 0;
            json_row(json, c, t->seq);
        }
    }
    if (first_div >= 0) {
        printf("  first DIVERGE at order %d\n",
               (first_div < nr) ? rec[first_div].order : con[first_div].order);
    } else if (nr != ncons) {
        printf("  first DIVERGE at end (one side has no call)\n");
    }
    if (nr != ncons) {
        printf("  remaining: recorded %d, consumed %d\n",
               nr > common ? nr - common : 0,
               ncons > common ? ncons - common : 0);
    }
    if (last_ok >= 0 && last_ok < nr) {
        const PathRow &r = rec[last_ok];
        if (r.r_cls == 1) {
            _snprintf_s(evidence, sizeof(evidence), _TRUNCATE,
                        "order%d leaf va=0x%x ret=0x%x", r.order,
                        (unsigned)r.r_va, (unsigned)r.r_ret);
        } else {
            _snprintf_s(evidence, sizeof(evidence), _TRUNCATE,
                        "order%d %s va=0x%x ret=0x%x", r.order,
                        kind_name(r.r_kind), (unsigned)r.r_va,
                        (unsigned)r.r_ret);
        }
    } else {
        _snprintf_s(evidence, sizeof(evidence), _TRUNCATE, "none");
    }
    printf("  gate: u170 in=%d after=%d gate114=0x%08x (%.3f) last_ok=%s\n",
           t->u170_in, tc.u170, gbits, g114, evidence);
}
