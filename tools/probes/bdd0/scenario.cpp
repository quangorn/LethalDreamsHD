/* tools/probes/bdd0/scenario.cpp — JSONL-захват v12 → Scenario.
 *
 * Парсер скопирован из tools/probes/battle_replay.cpp (JParser: тот же стиль,
 * те же отказы «json: <msg> at offset N»). Загружаются три рода записей:
 *   bdd0_in   — снапшот входа (frame/unit/ai/band/gs + p1/p2/p3/u170/gate114
 *               + raw-цепочка указателей e0/e0_44/tree/gctx0);
 *   bdd0_leaf — поток логических листьев; раскладывается по seq;
 *   bdd0_native — поток прочих RECORD-нативов (slot = vtable-вызов);
 *   bdd0_res  — записанный исход (al/out/u170/m0..m2) для --impl native.
 * Неизвестные ключи игнорируются (v10/v11-файлы читаются: bdd0_in нет →
 * ни одной записи, ядро вернёт «no v12 records» с rc 2).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.h"

/* --------------------------- минимальный JSON --------------------------- */
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t;
    long long n;
    bool b;
    std::string s;
    std::vector<JVal> a;
    std::map<std::string, JVal> o;
    JVal() : t(NUL), n(0), b(false) {}
};

class JParser {
    const std::string &s;
    size_t p;

    void fail(const char *msg) const
    {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "json: %s at offset %u",
                    msg, (unsigned)p);
        throw std::runtime_error(buf);
    }
    void ws()
    {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' ||
                                s[p] == '\r' || s[p] == '\n')) p++;
    }
    char peek()
    {
        ws();
        if (p >= s.size()) fail("unexpected end");
        return s[p];
    }
    void expect(char c)
    {
        if (peek() != c) fail("unexpected character");
        p++;
    }
    JVal parseString()
    {
        JVal v; v.t = JVal::STR;
        expect('"');
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') return v;
            if (c == '\\') {
                if (p >= s.size()) fail("bad escape");
                char e = s[p++];
                switch (e) {
                case 'n': v.s += '\n'; break;
                case 'r': v.s += '\r'; break;
                case 't': v.s += '\t'; break;
                case '"': v.s += '"'; break;
                case '\\': v.s += '\\'; break;
                case '/': v.s += '/'; break;
                case 'u':
                    if (p + 4 > s.size()) fail("bad unicode escape");
                    p += 4; v.s += '?'; break;
                default: fail("bad escape");
                }
            } else {
                v.s += c;
            }
        }
        fail("unterminated string");
        return v;
    }
    JVal parseNum()
    {
        JVal v; v.t = JVal::NUM;
        size_t st = p;
        if (s[p] == '-' || s[p] == '+') p++;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == st || (p == st + 1 && (s[st] == '-' || s[st] == '+'))) {
            fail("bad number");
        }
        if (p < s.size() && (s[p] == '.' || s[p] == 'e' || s[p] == 'E')) {
            while (p < s.size() && (s[p] == '.' || s[p] == 'e' || s[p] == 'E' ||
                                    s[p] == '-' || s[p] == '+' ||
                                    (s[p] >= '0' && s[p] <= '9'))) p++;
        }
        std::string t = s.substr(st, p - st);
        v.n = _strtoi64(t.c_str(), NULL, 10);
        return v;
    }
    JVal parseValue()
    {
        char c = peek();
        if (c == '{') {
            JVal v; v.t = JVal::OBJ;
            p++;
            if (peek() == '}') { p++; return v; }
            for (;;) {
                if (peek() != '"') fail("object key must be string");
                JVal k = parseString();
                expect(':');
                JVal val = parseValue();
                v.o[k.s] = val;
                char d = peek();
                if (d == ',') { p++; continue; }
                if (d == '}') { p++; break; }
                fail("expected , or }");
            }
            return v;
        }
        if (c == '[') {
            JVal v; v.t = JVal::ARR;
            p++;
            if (peek() == ']') { p++; return v; }
            for (;;) {
                v.a.push_back(parseValue());
                char d = peek();
                if (d == ',') { p++; continue; }
                if (d == ']') { p++; break; }
                fail("expected , or ]");
            }
            return v;
        }
        if (c == '"') return parseString();
        if (c == 't') {
            if (s.compare(p, 4, "true") != 0) fail("bad literal");
            p += 4; JVal v; v.t = JVal::BOOL; v.b = true; return v;
        }
        if (c == 'f') {
            if (s.compare(p, 5, "false") != 0) fail("bad literal");
            p += 5; JVal v; v.t = JVal::BOOL; v.b = false; return v;
        }
        if (c == 'n') {
            if (s.compare(p, 4, "null") != 0) fail("bad literal");
            p += 4; return JVal();
        }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNum();
        fail("bad value");
        return JVal();
    }
public:
    explicit JParser(const std::string &text) : s(text), p(0) {}
    JVal parse()
    {
        JVal v = parseValue();
        ws();
        if (p != s.size()) fail("trailing data");
        return v;
    }
    /* Потоковое чтение: один объект за вызов, true — есть объект. */
    bool parse_next(JVal &v)
    {
        ws();
        if (p >= s.size()) return false;
        v = parseValue();
        ws();
        if (p < s.size() && s[p] == '\r') p++;
        if (p < s.size() && s[p] == '\n') p++;
        return true;
    }
};

static bool jhas(const JVal &v, const char *key)
{
    return v.t == JVal::OBJ && v.o.find(key) != v.o.end();
}
static const JVal *jget(const JVal &v, const char *key)
{
    if (v.t != JVal::OBJ) return NULL;
    std::map<std::string, JVal>::const_iterator it = v.o.find(key);
    return (it == v.o.end()) ? NULL : &it->second;
}
static long long jint_opt(const JVal &v, const char *key, long long def)
{
    const JVal *x = jget(v, key);
    return (x != NULL && x->t == JVal::NUM) ? x->n : def;
}
static std::string jstr_opt(const JVal &v, const char *key)
{
    const JVal *x = jget(v, key);
    return (x != NULL && x->t == JVal::STR) ? x->s : std::string();
}

/* --------------------------------- base64 -------------------------------- */
static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Декодирует в dst (cap байт). Возврат — число байт или -1 (битый ввод). */
static int b64_decode(const std::string &in, unsigned char *dst, int cap)
{
    int o = 0;
    unsigned int acc = 0;
    int bits = 0;
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int v = b64_val(c);
        if (v < 0) return -1;
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            dst[o++] = (unsigned char)((acc >> bits) & 0xff);
        }
    }
    return o;
}

/* Блок фиксированного размера: отсутствующий/null/битый — has=0, не отказ. */
static void read_block(const JVal &v, const char *key, unsigned char *dst,
                       int len, int *has, int *exact)
{
    std::string s = jstr_opt(v, key);
    *has = 0;
    *exact = 0;
    if (s.empty()) return;
    int n = b64_decode(s, dst, len);
    if (n < 0) return;
    *has = 1;
    *exact = (n == len) ? 1 : 0;
}

/* --------------------------------- загрузка ------------------------------ */
struct ResRec {
    int al, out, u170, has, trap;
    int has_band;
    unsigned band_begin, band_end;
    unsigned m[3];
    ResRec() : al(0), out(0), u170(0), has(0), trap(0), has_band(0),
               band_begin(0), band_end(0) { m[0] = m[1] = m[2] = 0; }
};

static void apply_in(TickRecord &t, const JVal &v, int *bad)
{
    t.seq = (int)jint_opt(v, "seq", 0);
    t.tick = (int)jint_opt(v, "tick", 0);
    t.unit_index = (int)jint_opt(v, "unit", -1);
    t.p1 = (int)jint_opt(v, "p1", 0);
    t.p2 = (int)jint_opt(v, "p2", 0);
    t.p3 = (int)jint_opt(v, "p3", 0);
    t.u170_in = (int)jint_opt(v, "u170", 0);
    t.gate114 = (unsigned)jint_opt(v, "gate114", 0);
    t.trap = (int)jint_opt(v, "trap", 0);   /* снимок снят при сбое */
    t.self_raw = (unsigned)jint_opt(v, "self", 0);
    t.band_raw = (unsigned)jint_opt(v, "band_begin", 0);
    /* v13 (аддитивно): "band":[begin,end] — входное состояние ленты (BattleAI
     * +0x3c+0x20/+0x24). Старые файлы ключа не имеют → has_band_state=0. */
    {
        const JVal *band = jget(v, "band");
        if (band != NULL && band->t == JVal::ARR && band->a.size() >= 2 &&
            band->a[0].t == JVal::NUM && band->a[1].t == JVal::NUM) {
            t.has_band_state = 1;
            t.band_in_begin = (unsigned)band->a[0].n;
            t.band_in_end = (unsigned)band->a[1].n;
        }
    }
    int exact = 0;
    read_block(v, "frame_b64", t.frame, BDD0_FRAME_LEN, &t.has_frame, &exact);
    if (t.has_frame && !exact && bad != NULL) *bad |= 1;
    read_block(v, "unit_b64", t.unit, BDD0_UNIT_LEN, &t.has_unit, &exact);
    if (t.has_unit && !exact && bad != NULL) *bad |= 2;
    read_block(v, "ai_b64", t.ai, BDD0_AI_LEN, &t.has_ai, &exact);
    if (t.has_ai && !exact && bad != NULL) *bad |= 4;

    /* лента: band_b64 (band_count*0x18) — необязательный ключ */
    {
        std::string s = jstr_opt(v, "band_b64");
        int n = s.empty() ? -1 : b64_decode(s, t.band, (int)sizeof(t.band));
        if (n >= 0) {
            t.has_band = (n > 0) ? 1 : 0;
            t.band_n = (n / BDD0_BAND_ELEM > BDD0_BAND_MAX)
                       ? BDD0_BAND_MAX : n / BDD0_BAND_ELEM;
        } else {
            t.band_n = (int)jint_opt(v, "band_count", 0);
            if (t.band_n > BDD0_BAND_MAX) t.band_n = BDD0_BAND_MAX;
            if (t.band_n < 0) t.band_n = 0;
        }
    }
    /* game_state (объект FUN_00442570): 0x20 байт */
    read_block(v, "gs_b64", t.gs, BDD0_GS_LEN, &t.has_gs, &exact);

    /* raw-цепочка указателей снимка (аддитивные поля v12) */
    if (jhas(v, "e0") && jhas(v, "e0_44") && jhas(v, "tree")) {
        t.e0 = (unsigned)jint_opt(v, "e0", 0);
        t.e0_44 = (unsigned)jint_opt(v, "e0_44", 0);
        t.tree = (unsigned)jint_opt(v, "tree", 0);
        t.has_chain = 1;
    }
    if (jhas(v, "gctx0")) {
        t.gctx0 = (unsigned)jint_opt(v, "gctx0", 0);
        t.has_gctx0 = 1;
    }
}

static void apply_res(ResRec &r, const JVal &v)
{
    r.has = 1;
    r.trap = (int)jint_opt(v, "trap", 0);
    r.al = (int)jint_opt(v, "al", 0);
    r.out = (int)jint_opt(v, "out", -1);
    r.u170 = (int)jint_opt(v, "u170", 0);
    r.m[0] = (unsigned)jint_opt(v, "m0", 0);
    r.m[1] = (unsigned)jint_opt(v, "m1", 0);
    r.m[2] = (unsigned)jint_opt(v, "m2", 0);
    /* v13 (аддитивно): после-состояние ленты (begin,end) из bdd0_res. */
    {
        const JVal *band = jget(v, "band");
        if (band != NULL && band->t == JVal::ARR && band->a.size() >= 2 &&
            band->a[0].t == JVal::NUM && band->a[1].t == JVal::NUM) {
            r.has_band = 1;
            r.band_begin = (unsigned)band->a[0].n;
            r.band_end = (unsigned)band->a[1].n;
        }
    }
}

static void apply_leaf(LeafRec &l, const JVal &v)
{
    memset(&l, 0, sizeof(l));
    l.order = (int)jint_opt(v, "order", 0);
    const JVal *va = jget(v, "va");
    if (va != NULL && va->t == JVal::STR) {
        l.va = (int)strtoul(va->s.c_str(), NULL, 16);
    } else if (va != NULL && va->t == JVal::NUM) {
        l.va = (int)va->n;
    }
    {
        const JVal *site = jget(v, "site");
        if (site != NULL && site->t == JVal::STR) {
            l.site = (int)strtoul(site->s.c_str(), NULL, 16);
        } else if (site != NULL && site->t == JVal::NUM) {
            l.site = (int)site->n;
        }
    }
    l.ecx = (int)jint_opt(v, "ecx", 0);
    l.ret = (int)jint_opt(v, "ret", 0);
    l.trap = (int)jint_opt(v, "trap", 0);   /* старые записи поля не имеют */
    const JVal *args = jget(v, "args");
    if (args != NULL && args->t == JVal::ARR) {
        for (size_t i = 0; i < args->a.size() && l.nargs < 6; i++) {
            if (args->a[i].t == JVal::NUM) {
                l.args[l.nargs++] = (int)args->a[i].n;
            }
        }
    }
    const JVal *w = jget(v, "w");
    if (w != NULL && w->t == JVal::ARR) {
        for (size_t i = 0; i < w->a.size() && l.nw < 3; i++) {
            if (w->a[i].t == JVal::NUM) l.w[l.nw++] = (int)w->a[i].n;
        }
    }
}

static int slot_id_from_name(const std::string &s)
{
    if (s == "u+0x28") return 1;
    if (s == "u+0x2c") return 2;
    if (s == "u+0x38") return 3;
    if (s == "u+0x40") return 4;
    return 0;
}

static void apply_native(NativeRec &n, const JVal &v)
{
    n = NativeRec();
    n.order = (int)jint_opt(v, "order", 0);
    const JVal *site = jget(v, "site");
    if (site != NULL && site->t == JVal::STR) {
        n.site = (int)strtoul(site->s.c_str(), NULL, 16);
    } else if (site != NULL && site->t == JVal::NUM) {
        n.site = (int)site->n;
    }
    const JVal *va = jget(v, "va");
    if (va != NULL && va->t == JVal::STR) {
        n.va = (int)strtoul(va->s.c_str(), NULL, 16);
    } else if (va != NULL && va->t == JVal::NUM) {
        n.va = (int)va->n;
    }
    n.slot = slot_id_from_name(jstr_opt(v, "slot"));
    std::string rk = jstr_opt(v, "ret_kind");
    n.kind = (rk == "al") ? 1 : (rk == "f32") ? 2 : (rk == "void") ? 3 : 0;
    n.ecx = (int)jint_opt(v, "ecx", 0);
    n.ret = (int)jint_opt(v, "ret", 0);
    n.trap = (int)jint_opt(v, "trap", 0);
    const JVal *args = jget(v, "args");
    if (args != NULL && args->t == JVal::ARR) {
        for (size_t i = 0; i < args->a.size() && n.nargs < 8; i++) {
            if (args->a[i].t == JVal::NUM) n.args[n.nargs++] = (int)args->a[i].n;
        }
    }
    const JVal *w = jget(v, "w");
    if (w != NULL && w->t == JVal::ARR) {
        for (size_t i = 0; i < w->a.size() && n.nw < 3; i++) {
            if (w->a[i].t == JVal::NUM) n.w[n.nw++] = (int)w->a[i].n;
        }
    }
    const JVal *ww = jget(v, "ww");
    if (ww != NULL && ww->t == JVal::ARR) {
        for (size_t i = 0; i < ww->a.size() && n.nww < 32; i++) {
            if (ww->a[i].t == JVal::NUM) n.ww[n.nww++] = (int)ww->a[i].n;
        }
    }
}

Scenario *load_capture(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        throw std::runtime_error(std::string("cannot open capture: ") + path);
    }
    std::string text;
    {
        char buf[1 << 16];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    }
    fclose(f);

    Scenario *sc = new Scenario;
    const char *base = strrchr(path, '/');
    base = (base != NULL) ? base + 1 : path;
    sc->name = base;

    std::map<int, ResRec> res_by_seq;
    std::vector<std::pair<int, LeafRec> > leaves;   /* (seq, leaf) */
    std::vector<std::pair<int, NativeRec> > natives; /* (seq, native) */

    JParser jp(text);
    JVal v;
    int bad_sizes = 0;
    try {
        for (;;) {
            /* оборванный хвост (захват убит на записи) — не формат-ошибка:
             * «unexpected end» на последнем чанке отбрасывается с пометкой. */
            try {
                if (!jp.parse_next(v)) break;
            } catch (const std::runtime_error &e) {
                if (strstr(e.what(), "unexpected end") != NULL &&
                    sc->records > 0) {
                    sc->truncated = 1;
                    break;
                }
                throw;
            }
            if (v.t != JVal::OBJ) {
                throw std::runtime_error("capture line is not an object");
            }
            sc->records++;
            std::string kind = jstr_opt(v, "kind");
            long long ver = jint_opt(v, "v", 0);
            if (kind == "bdd0_in") {
                if (ver < 12) continue;            /* не v12-снапшот */
                TickRecord t;
                apply_in(t, v, &bad_sizes);
                /* сценарий: ключ scenario, если он есть в записи */
                std::string sn = jstr_opt(v, "scenario");
                if (!sn.empty()) sc->name = sn;
                if (t.trap) sc->input_trap++;
                sc->ticks.push_back(t);
                sc->snap_records++;
            } else if (kind == "bdd0_res") {
                ResRec r;
                apply_res(r, v);
                res_by_seq[(int)jint_opt(v, "seq", 0)] = r;
            } else if (kind == "leaf_missing") {
                /* сайт листа не захукался на захвате: поток может быть неполон
                 * для ВСЕХ seq — запоминаем причину, leaf_seq её назовёт. */
                std::string site = jstr_opt(v, "site");
                std::string va = jstr_opt(v, "va");
                std::string why = jstr_opt(v, "why");
                sc->leaf_missing_n++;
                if (sc->leaf_missing_why.empty()) {
                    char buf[220];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                                "capture leaf_missing: site 0x%s (leaf 0x%s) "
                                "not hooked: %s", site.c_str(), va.c_str(),
                                why.empty() ? "?" : why.c_str());
                    sc->leaf_missing_why = buf;
                }
            } else if (kind == "bdd0_leaf") {
                if (ver < 12) continue;
                LeafRec l;
                apply_leaf(l, v);
                leaves.push_back(std::make_pair(
                    (int)jint_opt(v, "seq", 0), l));
                sc->leaf_records++;
            } else if (kind == "bdd0_native") {
                if (ver < 12) continue;
                NativeRec n;
                apply_native(n, v);
                natives.push_back(std::make_pair(
                    (int)jint_opt(v, "seq", 0), n));
                sc->native_records++;
            } else if (kind == "native_site_skipped") {
                std::string site = jstr_opt(v, "site");
                std::string va = jstr_opt(v, "va");
                std::string why = jstr_opt(v, "why");
                sc->native_missing_n++;
                if (sc->native_missing_why.empty()) {
                    char buf[220];
                    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                                "capture native_site_skipped: site 0x%s "
                                "(target 0x%s): %s", site.c_str(), va.c_str(),
                                why.empty() ? "?" : why.c_str());
                    sc->native_missing_why = buf;
                }
            }
            /* прочие роды (cap, res, grid, eee0_*, bdd0_cap) игнорируются */
        }
    } catch (...) {
        delete sc;
        throw;
    }
    if (bad_sizes != 0) {
        delete sc;
        char buf[200];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "capture: base64 block size mismatch (mask=0x%x)",
                    (unsigned)bad_sizes);
        throw std::runtime_error(buf);
    }

    /* листья: стабильный порядок (seq, order) → окно на каждый тик */
    std::sort(leaves.begin(), leaves.end(),
              [](const std::pair<int, LeafRec> &a,
                 const std::pair<int, LeafRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    for (size_t i = 0; i < leaves.size(); i++) sc->leaves.push_back(leaves[i].second);

    /* bdd0_native: тот же стабильный порядок (seq, order) → окно на тик */
    std::sort(natives.begin(), natives.end(),
              [](const std::pair<int, NativeRec> &a,
                 const std::pair<int, NativeRec> &b) {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second.order < b.second.order;
              });
    for (size_t i = 0; i < natives.size(); i++) {
        sc->natives.push_back(natives[i].second);
    }

    size_t li = 0;
    size_t ni = 0;
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        TickRecord &t = sc->ticks[i];
        while (li < leaves.size() && leaves[li].first < t.seq) li++;
        size_t first = li;
        while (li < leaves.size() && leaves[li].first == t.seq) li++;
        t.leaf_first = (int)first;
        t.leaf_n = (int)(li - first);
        while (ni < natives.size() && natives[ni].first < t.seq) ni++;
        size_t nfirst = ni;
        while (ni < natives.size() && natives[ni].first == t.seq) ni++;
        t.native_first = (int)nfirst;
        t.native_n = (int)(ni - nfirst);
        t.native_eee0_no_ww = 0;
        t.gs_raw = 0;          /* адрес объекта состояния — из потока 442570 */
        for (size_t k = nfirst; k < ni; k++) {
            if (natives[k].second.va == 0x45eee0 &&
                natives[k].second.nww == 0) {
                t.native_eee0_no_ww = 1;
            }
            if (natives[k].second.va == 0x442570 && natives[k].second.ret != 0) {
                if (t.gs_raw == 0) t.gs_raw = (unsigned)natives[k].second.ret;
                else if (t.gs_raw != (unsigned)natives[k].second.ret) {
                    /* объект состояния переехал внутри тика: привязка невозможна */
                    t.gs_raw = 0;
                }
            }
        }
        std::map<int, ResRec>::const_iterator it = res_by_seq.find(t.seq);
        if (it != res_by_seq.end()) {
            t.has_res = 1;
            t.res_trap = it->second.trap;
            if (t.res_trap) sc->outcome_trap++;
            t.res_al = it->second.al;
            t.res_out = it->second.out;
            t.res_u170 = it->second.u170;
            t.res_m[0] = it->second.m[0];
            t.res_m[1] = it->second.m[1];
            t.res_m[2] = it->second.m[2];
            t.has_res_band = it->second.has_band;
            t.band_post_begin = it->second.band_begin;
            t.band_post_end = it->second.band_end;
        }
    }
    /* хвост оборван (захват убит mid-tick): seq без bdd0_res и он последний по
     * seq в захвате — запись снята не до конца, тик недостоверен как данные.
     * Отсутствие res ниже последнего seq — настоящая аномалия захвата. */
    int max_seq = sc->ticks.empty() ? 0 : sc->ticks[0].seq;
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        if (sc->ticks[i].seq > max_seq) max_seq = sc->ticks[i].seq;
    }
    for (size_t i = 0; i < sc->ticks.size(); i++) {
        TickRecord &t = sc->ticks[i];
        if (t.has_res) continue;
        if (t.seq == max_seq) {
            t.incomplete_tail = 1;
            sc->incomplete_tail++;
        } else {
            t.res_missing_nontail = 1;
            sc->res_missing_nontail++;
        }
    }
    return sc;
}

void scenario_free(Scenario *s) { delete s; }
