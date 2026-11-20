/* battle_replay.cpp — офлайн-харнесс FUN_0045e0a0 (battle_ai_ab_method.md §3.2).
 *
 * Читает JSONL-захват нативного вызова (battle/capture.cpp), строит приватную
 * реплику BattleAI/map1/map2, прогоняет порт (battle/path.cpp + battle/target.cpp)
 * с подставленными native::-обёртками и захваченными результатами нативных
 * листьев (cb40/c690/c5a0) и пишет res_port-записи.
 *
 * Сборка: tools/probes/battle_replay_build.py (MSVC x86, /O2 /fp:precise,
 * те же флаги, что tools/ldhook/build.py; макрос BATTLE_REPLAY включает
 * точки подмены листьев в battle/*.cpp). Запуск:
 *   battle_replay.exe <capture.jsonl> <res_port.jsonl>
 *
 *房间里
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>

#include "../ldhook/battle/battle.h"
#include "../ldhook/battle/native.h"

/* --------------------------- минимальный JSON --------------------------- */

static int replay_trace_level(void);      /* определён ниже (логгер) */

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

    void fail(const char *msg) const {
        char buf[256];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "json: %s at offset %u", msg, (unsigned)p);
        throw std::runtime_error(buf);
    }
    void ws() {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' ||
                                s[p] == '\r' || s[p] == '\n')) p++;
    }
    char peek() {
        ws();
        if (p >= s.size()) fail("unexpected end");
        return s[p];
    }
    void expect(char c) {
        if (peek() != c) fail("unexpected character");
        p++;
    }
    JVal parseString() {
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
                case 'u': {
                    if (p + 4 > s.size()) fail("bad unicode escape");
                    p += 4; v.s += '?'; break;
                }
                default: fail("bad escape");
                }
            } else {
                v.s += c;
            }
        }
        fail("unterminated string");
        return v;
    }
    JVal parseNum() {
        JVal v; v.t = JVal::NUM;
        size_t st = p;
        if (s[p] == '-' || s[p] == '+') p++;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') p++;
        if (p == st || p == st + 1 && (s[st] == '-' || s[st] == '+')) {
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
    JVal parseValue() {
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
                if (v.o.count(k.s)) fail("duplicate key");
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
    JVal parse() {
        JVal v = parseValue();
        ws();
        if (p != s.size()) fail("trailing data");
        return v;
    }
};

static const JVal &jget(const JVal &v, const char *key, const char *where) {
    if (v.t != JVal::OBJ) {
        throw std::runtime_error(std::string(where) + ": expected object");
    }
    std::map<std::string, JVal>::const_iterator it = v.o.find(key);
    if (it == v.o.end()) {
        throw std::runtime_error(std::string(where) + ": missing '" + key + "'");
    }
    return it->second;
}
static long long jint(const JVal &v, const char *key, const char *where) {
    const JVal &x = jget(v, key, where);
    if (x.t != JVal::NUM) {
        throw std::runtime_error(std::string(where) + "." + key + ": expected number");
    }
    return x.n;
}
static const JVal &jarr(const JVal &v, const char *key, const char *where) {
    const JVal &x = jget(v, key, where);
    if (x.t != JVal::ARR) {
        throw std::runtime_error(std::string(where) + "." + key + ": expected array");
    }
    return x;
}
static bool jisnull(const JVal &v) { return v.t == JVal::NUL; }
static bool jhas(const JVal &v, const char *key) {
    return v.t == JVal::OBJ && v.o.find(key) != v.o.end();
}
static long long jint_opt(const JVal &v, const char *key, long long def) {
    return jhas(v, key) ? jint(v, key, "record") : def;
}
static long long jnum(const JVal &v, const char *where) {
    if (v.t != JVal::NUM) {
        throw std::runtime_error(std::string(where) + ": expected number");
    }
    return v.n;
}

/* ---------------------------- формат захвата ---------------------------- */

struct CapNode {
    int x, y, filter, g, d2, h10k, cost;
    int px, py, occ_key;
    int pos_addr;                   /* LDPos* (ключ map1, identity-гейт) */
    int nbr[9][2];
};
struct PathLeaf { bool null; int w[5]; int addr; };

/* v3: значение узла map2 (PathState*) — раньше реплика ставила NULL и шла
 * «свободным путём» для всех занятых ключей. */
#define RP_AI_WORDS 32
#define RP_PS_WORDS 22
struct OccVal {
    bool has;
    int value_addr, pos_addr;
    unsigned int w[RP_PS_WORDS];
    int ptr[11];                    /* LDPos*: pos, parent, nbr[9] */
    int pos[2];
    int parent[2];
    int nbr[9][2];
    OccVal() : has(false), value_addr(0), pos_addr(0) {
        memset(w, 0, sizeof(w)); memset(ptr, 0, sizeof(ptr));
        pos[0] = pos[1] = -1;
        parent[0] = parent[1] = -1;
        for (int i = 0; i < 9; i++) { nbr[i][0] = nbr[i][1] = -1; }
    }
};

struct Call {
    JVal cap;                       /* исходная cap-запись */
    int seq, tick, unit, filter, out_ptr;
    int cmp[2];
    int cmp_addr;
    unsigned timeout_bits;
    unsigned timer_bits, final_bits, dist_bits, ctx_flags, world_scale_bits;
    int yrad, xrad, rect[4], anchor[2], trk;
    unsigned bd2;
    int ai_code;
    std::vector<CapNode> map1;
    std::vector<int> occ;
    int version;                    /* cap.v; v1/v2 имеют только occ-ключи */
    bool has_ai_words;
    unsigned int ai_words[RP_AI_WORDS];
    std::map<int, OccVal> occ_vals; /* key -> value (v3) */
    int grid_x, grid_y;
    std::vector<PathLeaf> leaves_path;
    std::vector<int> leaves_heur, leaves_timeout;
    JVal res;
    bool has_res;
};

static void read_pos(const JVal &v, int *xy, const char *where) {
    if (jisnull(v)) { xy[0] = -1; xy[1] = -1; return; }
    if (v.t != JVal::ARR || v.a.size() != 2) {
        throw std::runtime_error(std::string(where) + ": expected [x,y] or null");
    }
    xy[0] = (int)jnum(v.a[0], where);
    xy[1] = (int)jnum(v.a[1], where);
}

static void read_leaves(Call &c, const JVal &res);

static void read_call(Call &c, const JVal &cap, const JVal *res) {
    const JVal &args = jget(cap, "args", "cap");
    const JVal &ai = jget(cap, "ai", "cap");

    c.version = (int)jint_opt(cap, "v", 1);
    c.has_ai_words = false;
    memset(c.ai_words, 0, sizeof(c.ai_words));
    c.seq = (int)jint(cap, "seq", "cap");
    c.tick = (int)jint(cap, "tick", "cap");
    c.unit = (int)jint(cap, "unit", "cap");
    c.filter = (int)jint(args, "filter", "cap.args");
    read_pos(jget(args, "cmp", "cap.args"), c.cmp, "cap.args.cmp");
    c.cmp_addr = (int)jint_opt(args, "cmp_addr", 0);
    c.timeout_bits = (unsigned)jint(args, "timeout_bits", "cap.args");
    c.out_ptr = (int)jint(args, "out_ptr", "cap.args");

    c.timer_bits = (unsigned)jint(ai, "timer_bits", "cap.ai");
    c.final_bits = (unsigned)jint(ai, "final_bits", "cap.ai");
    c.dist_bits = (unsigned)jint(ai, "dist_bits", "cap.ai");
    c.yrad = (int)jint(ai, "yrad", "cap.ai");
    c.xrad = (int)jint(ai, "xrad", "cap.ai");
    const JVal &rect = jarr(ai, "rect", "cap.ai");
    if (rect.a.size() != 4) throw std::runtime_error("cap.ai.rect: expected 4 ints");
    for (int i = 0; i < 4; i++) c.rect[i] = (int)jnum(rect.a[i], "cap.ai.rect");
    read_pos(jget(ai, "anchor", "cap.ai"), c.anchor, "cap.ai.anchor");
    c.trk = (int)jint(ai, "trk", "cap.ai");
    c.bd2 = (unsigned)jint(ai, "bd2", "cap.ai");
    c.ai_code = (int)jint(ai, "code", "cap.ai");
    c.ctx_flags = (unsigned)jint(ai, "ctx_flags", "cap.ai");
    c.world_scale_bits = (unsigned)jint(ai, "world_scale_bits", "cap.ai");

    const JVal &m1 = jarr(cap, "map1", "cap");
    for (size_t i = 0; i < m1.a.size(); i++) {
        const JVal &e = m1.a[i];
        CapNode n;
        memset(&n, 0, sizeof(n));
        read_pos(jget(e, "pos", "cap.map1"), &n.x, "cap.map1.pos");
        n.filter = (int)jint(e, "filter", "cap.map1");
        n.g = (int)jint(e, "g", "cap.map1");
        n.d2 = (int)jint(e, "d2", "cap.map1");
        n.h10k = (int)jint(e, "h10k", "cap.map1");
        n.cost = (int)jint(e, "cost", "cap.map1");
        read_pos(jget(e, "parent", "cap.map1"), &n.px, "cap.map1.parent");
        n.occ_key = (int)jint(e, "occ_key", "cap.map1");
        n.pos_addr = (int)jint_opt(e, "pos_addr", 0);
        const JVal &nb = jarr(e, "nbr", "cap.map1");
        for (int k = 0; k < 9 && k < (int)nb.a.size(); k++) {
            read_pos(nb.a[k], n.nbr[k], "cap.map1.nbr");
        }
        c.map1.push_back(n);
    }
    const JVal &occ = jarr(cap, "occ", "cap");
    for (size_t i = 0; i < occ.a.size(); i++) {
        c.occ.push_back((int)jnum(occ.a[i], "cap.occ"));
    }
    /* v3: полный скалярный блок BattleAI и значения узлов map2. Их отсутствие
     * в v1/v2 — норма (старые захваты), поэтому всё под jhas(). */
    if (c.version >= 3 && jhas(cap, "ai_words")) {
        const JVal &aw = jarr(cap, "ai_words", "cap");
        if (aw.a.size() != RP_AI_WORDS) {
            throw std::runtime_error("cap.ai_words: expected 32 dwords");
        }
        for (size_t i = 0; i < aw.a.size(); i++) {
            c.ai_words[i] = (unsigned int)(int)jnum(aw.a[i], "cap.ai_words");
        }
        c.has_ai_words = true;
    }
    if (c.version >= 3 && jhas(cap, "occ_nodes")) {
        const JVal &ons = jarr(cap, "occ_nodes", "cap");
        if (ons.a.size() != occ.a.size()) {
            throw std::runtime_error("cap.occ_nodes: size differs from occ");
        }
        for (size_t i = 0; i < ons.a.size(); i++) {
            const JVal &o = ons.a[i];
            OccVal v;
            int key = (int)jint(o, "key", "cap.occ_nodes");
            const JVal &val = jget(o, "value", "cap.occ_nodes");
            if (!jisnull(val)) {
                const JVal &w = jarr(val, "w", "cap.occ_nodes.value");
                const JVal &pt = jarr(val, "ptr", "cap.occ_nodes.value");
                const JVal &po = jarr(val, "pos", "cap.occ_nodes.value");
                const JVal &pa = jarr(val, "parent", "cap.occ_nodes.value");
                const JVal &nb = jarr(val, "nbr", "cap.occ_nodes.value");
                if (w.a.size() != RP_PS_WORDS || pt.a.size() != 11 ||
                    po.a.size() != 2 || pa.a.size() != 2 || nb.a.size() != 9) {
                    throw std::runtime_error("cap.occ_nodes.value: bad sizes");
                }
                v.has = true;
                v.value_addr = (int)jint(val, "value_addr", "cap.occ_nodes.value");
                v.pos_addr = (int)jint(val, "pos_addr", "cap.occ_nodes.value");
                for (size_t k = 0; k < w.a.size(); k++) {
                    v.w[k] = (unsigned int)(int)jnum(w.a[k], "cap.occ_nodes.value.w");
                }
                for (size_t k = 0; k < pt.a.size(); k++) {
                    v.ptr[k] = (int)jnum(pt.a[k], "cap.occ_nodes.value.ptr");
                }
                for (int k = 0; k < 2; k++) {
                    v.pos[k] = (int)jnum(po.a[k], "cap.occ_nodes.value.pos");
                    v.parent[k] = (int)jnum(pa.a[k], "cap.occ_nodes.value.parent");
                }
                for (size_t k = 0; k < nb.a.size(); k++) {
                    read_pos(nb.a[k], v.nbr[k], "cap.occ_nodes.value.nbr");
                }
            }
            c.occ_vals[key] = v;
        }
    }
    const JVal &grid = jarr(cap, "grid", "cap");
    c.grid_x = c.grid_y = 0;
    if (!grid.a.empty()) {
        c.grid_x = (int)jint(grid.a[0], "maxrow", "cap.grid");
        c.grid_y = (int)jint(grid.a[0], "maxcol", "cap.grid");
    }
    c.has_res = (res != NULL);
    if (res != NULL) {
        c.res = *res;
        read_leaves(c, *res);
    }
}

static void read_leaves(Call &c, const JVal &res) {
    {
        const JVal &leaf = jget(res, "leaf", "res");
        const JVal &lp = jarr(leaf, "path", "res.leaf");
        const JVal *ptr = jhas(leaf, "ptr") ? &jget(leaf, "ptr", "res.leaf") : NULL;
        if (ptr != NULL && ptr->t != JVal::ARR) {
            ptr = NULL;
        }
        for (size_t i = 0; i < lp.a.size(); i++) {
            PathLeaf pl;
            memset(&pl, 0, sizeof(pl));
            pl.null = jisnull(lp.a[i]);
            if (!pl.null) {
                if (lp.a[i].t != JVal::ARR) {
                    throw std::runtime_error("res.leaf.path: expected array or null");
                }
                for (size_t k = 0; k < 5 && k < lp.a[i].a.size(); k++) {
                    pl.w[k] = (int)jnum(lp.a[i].a[k], "res.leaf.path");
                }
            }
            if (ptr != NULL && i < ptr->a.size()) {
                pl.addr = (int)jnum(ptr->a[i], "res.leaf.ptr");
                if (pl.addr == 0) pl.null = true;   /* NULL-маркер указателя */
            } else if (!pl.null) {
                throw std::runtime_error("res.leaf.path: 'ptr' missing "
                                         "(capture without LDPos addresses)");
            }
            c.leaves_path.push_back(pl);
        }
        const JVal &lh = jarr(leaf, "heur", "res.leaf");
        for (size_t i = 0; i < lh.a.size(); i++) {
            c.leaves_heur.push_back((int)jnum(lh.a[i], "res.leaf.heur"));
        }
        const JVal &lt = jarr(leaf, "timeout", "res.leaf");
        for (size_t i = 0; i < lt.a.size(); i++) {
            c.leaves_timeout.push_back((int)jnum(lt.a[i], "res.leaf.timeout"));
        }
    }
}

/* ------------------------------- реплика -------------------------------- */

extern "C" {
    int g_replay_override = 1;
    int g_replay_leaf_underflow = 0;
    static Call *g_cur = NULL;
    static size_t g_pi = 0, g_hi = 0, g_ti = 0;
    static std::vector<int *> g_fake_path;
    /* Identity: нативный LDPos* — ключ map1 и операнд гейта cur->pos==cmp
     * (@0x45e137). Ключ карты — реальный адрес из захвата; объект(ы) харнеса,
     * выступающие его носителем, отображаются в тот же адрес. 0x100000000|
     * occ_key — запасной ключ для захватов без адресов (координаты). */
    static std::map<unsigned long long, int *> g_pos_obj;
    static std::map<int *, unsigned long long> g_obj_key;
    static std::vector<int> g_iter_seq;      /* пары x,y; -1/-1 = null */
    static int g_exit_node[2];
    static int g_exit_code = -1, g_exit_iters = -1;
    static bool g_have_exit = false;
}

struct RNode {
    TreeNode tn;
    PathState ps;
    LDPos pos;
    PathState parentStub;
    LDPos parentPos;
    PathState nbrStub[9];
    LDPos nbrPos[9];
    RNode() { memset(this, 0, sizeof(*this)); }
};
struct RepMap2 {
    TreeNode head;
    std::vector<TreeNode *> nodes;
};
struct Rep {
    unsigned char aiBuf[0x100];     /* BattleAI + запас под +0x7c */
    unsigned char ctxBuf[0x130];    /* game_ctx: flags +0, grid +8, scale +0x120 */
    unsigned char lvlBuf[0x40];     /* GridLevel: size_x +0x2c, size_y +0x30 */
    LDPos cmpPos, anchorPos;        /* адресная идентичность cmp/anchor по pos */
    LDPos *cmpPtr;
    TreeNode m1head;
    RepMap2 m2;
    std::vector<RNode *> nodes;
    std::vector<TreeNode *> m1nodes;  /* map1: tn->value = PathState (объект) */
    std::vector<PathLeaf> pathPool;
    int nilBuf[8];
};

static TreeNode *rep_nil(Rep *r) {
    return (TreeNode *)(void *)r->nilBuf;
}

/* Текущая реплика вызова: нативный ABI map2 — объект std::map ВНУТРИ BattleAI
 * (dd70 `lea 0x74(%edi),%ecx` @0x45dec8, head по +0x78), поэтому target.cpp
 * передаёт &self->map2 = aiBuf+0x74. Харнес держит RepMap2 отдельно — резолв
 * по этому адресу. */
static Rep *g_rep = NULL;

static int occ_key_of(LDPos *p) {
    return (int)(((unsigned short)p->y << 16) | (unsigned short)p->x);
}

/* Ключи identity-карты: реальный адрес LDPos из захвата; запасной режим —
 * 0x100000000|occ_key (только для старых захватов без адресов). */
static unsigned long long pos_key_addr(int addr) {
    return (unsigned long long)(unsigned)addr;
}
static unsigned long long pos_key_coord(int x, int y) {
    return 0x100000000ULL |
           (unsigned long long)((((unsigned short)y << 16) | (unsigned short)x));
}

/* Порядок map1 задаётся КЛЮЧОМ узла — указателем LDPos (dd00 передаёт
 * &obj->pos в insert, 0xc(%узла) сравнивается в 45d810). tn->key = реальный
 * адрес из захвата (pos_addr); новые узлы получают адрес своего
 * cb40-результата (leaf ptr). Запасной режим (старый захват без адресов):
 * (x,y)-мажор — так отсортированы все map1/map1_after таких записей.
 * Порядок важен: s6_scan_map берёт строго минимум метрики (при равных —
 * первый in-order). */
static bool m1_less(TreeNode *a, TreeNode *b) {
    unsigned ka = (unsigned)a->key, kb = (unsigned)b->key;
    if (ka != 0 && kb != 0) {
        return ka < kb;
    }
    {
        PathState *pa = (PathState *)a->value;
        PathState *pb = (PathState *)b->value;
        LDPos *qa = pa ? pa->pos : NULL;
        LDPos *qb = pb ? pb->pos : NULL;
        int ax = qa ? qa->x : -1, ay = qa ? qa->y : -1;
        int bx = qb ? qb->x : -1, by = qb ? qb->y : -1;
        if (ax != bx) return ax < bx;
        return ay < by;
    }
}

static void rep_rebuild_m1(Rep *r) {
    TreeNode *nil = rep_nil(r);
    size_t i, n = r->m1nodes.size();
    std::sort(r->m1nodes.begin(), r->m1nodes.end(), m1_less);
    for (i = 0; i < n; i++) {
        TreeNode *tn = r->m1nodes[i];
        tn->left = nil;
        tn->right = (i + 1 < n) ? r->m1nodes[i + 1] : &r->m1head;
        tn->parent = (i > 0) ? r->m1nodes[i - 1] : &r->m1head;
        tn->is_nil = 0;
    }
    r->m1head.left = n ? r->m1nodes[0] : &r->m1head;
    r->m1head.parent = n ? r->m1nodes[n - 1] : &r->m1head;
    r->m1head.right = &r->m1head;
    r->m1head.is_nil = 1;
}

static void rep_rebuild_m2(Rep *r) {
    TreeNode *nil = rep_nil(r);
    size_t i, n = r->m2.nodes.size();
    std::sort(r->m2.nodes.begin(), r->m2.nodes.end(),
              [](TreeNode *a, TreeNode *b) {
                  return (unsigned)a->key < (unsigned)b->key;
              });
    for (i = 0; i < n; i++) {
        r->m2.nodes[i]->left = nil;
        r->m2.nodes[i]->right = (i + 1 < n) ? r->m2.nodes[i + 1] : &r->m2.head;
        r->m2.nodes[i]->parent = (i > 0) ? r->m2.nodes[i - 1] : &r->m2.head;
        r->m2.nodes[i]->is_nil = 0;
    }
    r->m2.head.left = n ? r->m2.nodes[0] : &r->m2.head;
    r->m2.head.parent = n ? r->m2.nodes[n - 1] : &r->m2.head;
    r->m2.head.right = &r->m2.head;
    r->m2.head.is_nil = 1;
}

/* ---------------- v3: значения узлов map2 в реплике --------------------- */
/* Native/class FUN_0045dd00 кладёт в map2 тот же PathState, что уже лежит в
 * map1 (ветка 1 dd70 @0x45ded7 читает value как PathState* и идёт в ветку 2).
 * v1/v2 захваты value не имели — реплика ставила NULL и всегда считала ключ
 * свободным. Здесь identity восстанавливается по pos_addr. */
static std::map<int *, PathState *> g_pos_ps;
static std::vector<RNode *> g_occ_stubs;

static int *rp_pos_obj(int addr, int x, int y)
{
    unsigned long long key = (addr != 0) ? pos_key_addr(addr)
                                         : pos_key_coord(x, y);
    std::map<unsigned long long, int *>::const_iterator it = g_pos_obj.find(key);
    if (it != g_pos_obj.end()) return it->second;
    int *obj = (int *)calloc(8, sizeof(int));
    obj[3] = x; obj[4] = y;
    g_pos_obj[key] = obj;
    g_obj_key[obj] = key;
    return obj;
}

static PathState *rp_ps_ref(int addr, int x, int y)
{
    if (x == -1 && y == -1) return NULL;         /* NULL-сосед в захвате */
    int *obj = rp_pos_obj(addr, x, y);
    std::map<int *, PathState *>::const_iterator it = g_pos_ps.find(obj);
    if (it != g_pos_ps.end()) return it->second;
    RNode *rn = new RNode;
    g_occ_stubs.push_back(rn);
    rn->pos.x = obj[3]; rn->pos.y = obj[4];
    rn->ps.pos = (LDPos *)(void *)obj;
    g_pos_ps[obj] = &rn->ps;
    return &rn->ps;
}

static PathState *rp_make_occ_ps(const OccVal &v)
{
    int *pobj = rp_pos_obj(v.pos_addr, v.pos[0], v.pos[1]);
    std::map<int *, PathState *>::const_iterator known = g_pos_ps.find(pobj);
    /* Обычный dd00: один и тот же объект в map1 и map2 — переиспользуем его. */
    if (v.value_addr != 0 && known != g_pos_ps.end()) return known->second;
    RNode *rn = new RNode;
    g_occ_stubs.push_back(rn);
    memcpy(&rn->ps, v.w, sizeof(v.w));           /* 22 dword == sizeof(PathState) */
    rn->pos.x = v.pos[0]; rn->pos.y = v.pos[1];
    rn->ps.pos = (LDPos *)(void *)pobj;
    rn->ps.parent = rp_ps_ref(v.ptr[1], v.parent[0], v.parent[1]);
    for (int k = 0; k < 9; k++) {
        rn->ps.nbr[k] = rp_ps_ref(v.ptr[2 + k], v.nbr[k][0], v.nbr[k][1]);
    }
    g_pos_ps[pobj] = &rn->ps;
    return &rn->ps;
}

static void rep_build(Rep *r, const Call &c) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *nil = rep_nil(r);
    size_t i;
    std::map<int, RNode *> bypos;

    memset(r, 0, sizeof(*r));
    g_pos_ps.clear();
    for (size_t si = 0; si < g_occ_stubs.size(); si++) delete g_occ_stubs[si];
    g_occ_stubs.clear();
    /* v3: сырой скалярный блок BattleAI (dd70/e0a0 читают и поля за +0x38).
     * Указательные поля ниже перезаписываются объектами реплики. */
    if (c.has_ai_words) {
        memcpy(r->aiBuf, c.ai_words, sizeof(c.ai_words));
    }
    nil->is_nil = 1;
    nil->left = nil; nil->right = nil; nil->parent = nil;

    memcpy(&ai->timer_start_sec, &c.timer_bits, 4);
    memcpy(&ai->final_threshold, &c.final_bits, 4);
    memcpy(&ai->dist_threshold, &c.dist_bits, 4);
    ai->y_radius = c.yrad;
    ai->x_radius = c.xrad;
    ai->canvas_rect.left = c.rect[0];
    ai->canvas_rect.top = c.rect[1];
    ai->canvas_rect.right = c.rect[2];
    ai->canvas_rect.bottom = c.rect[3];
    ai->tracking = (unsigned char)c.trk;
    ai->distance_threshold = c.bd2;
    ai->anchor = &r->anchorPos;        /* уточняется после построения map1 */
    r->anchorPos.x = c.anchor[0];
    r->anchorPos.y = c.anchor[1];
    r->cmpPos.x = c.cmp[0];
    r->cmpPos.y = c.cmp[1];
    r->cmpPtr = &r->cmpPos;
    *(int *)((unsigned char *)ai + 0x38) = c.ai_code;

    /* game_ctx */
    *(unsigned int *)(r->ctxBuf + 0) = c.ctx_flags;
    *(unsigned int *)(r->ctxBuf + 0x120) = c.world_scale_bits;
    *(void **)(r->ctxBuf + 8) = (void *)r->lvlBuf;
    ai->game_ctx = (void *)r->ctxBuf;
    *(int *)(r->lvlBuf + 0x2c) = c.grid_x;
    *(int *)(r->lvlBuf + 0x30) = c.grid_y;

    /* map1 */
    for (i = 0; i < c.map1.size(); i++) {
        const CapNode &src = c.map1[i];
        RNode *n = new RNode;
        n->pos.x = src.x; n->pos.y = src.y;
        n->ps.pos = &n->pos;
        n->ps.filter = src.filter;
        n->ps.g_cost = src.g;
        n->ps.dist2 = src.d2;
        n->ps.h10000 = src.h10k;
        n->ps.metric = src.cost;
        n->ps.min_sentinel = 0xcb18967f;
        n->ps.max_sentinel = 0x4b18967f;
        n->parentPos.x = src.px; n->parentPos.y = src.py;
        n->parentStub.pos = &n->parentPos;
        n->ps.parent = (src.px == -1 && src.py == -1) ? NULL : &n->parentStub;
        for (int k = 0; k < 9; k++) {
            n->nbrPos[k].x = src.nbr[k][0];
            n->nbrPos[k].y = src.nbr[k][1];
            n->nbrStub[k].pos = &n->nbrPos[k];
            /* (-1,-1) — NULL-сосед в захвате (cap_read_ps_pos пишет -1,-1). */
            n->ps.nbr[k] = (src.nbr[k][0] == -1 && src.nbr[k][1] == -1)
                           ? NULL : &n->nbrStub[k];
        }
        r->nodes.push_back(n);
        n->tn.value = &n->ps;
        n->tn.key = src.pos_addr;
        r->m1nodes.push_back(&n->tn);
        bypos[occ_key_of(&n->pos)] = n;
    }
    /* Связки: nbr/parent указывают на узлы с той же позицией; если узла в
     * map1 нет — создаём заглушку с теми же координатами (filter=0). */
    for (i = 0; i < c.map1.size(); i++) {
        RNode *n = r->nodes[i];
        const CapNode &src = c.map1[i];
        int k;
        if (src.px != -1 || src.py != -1) {
            std::map<int, RNode *>::const_iterator it = bypos.find(occ_key_of(&n->parentPos));
            if (it != bypos.end()) {
                n->ps.parent = &it->second->ps;
            }
        }
        for (k = 0; k < 9; k++) {
            if (src.nbr[k][0] == -1 && src.nbr[k][1] == -1) continue;
            std::map<int, RNode *>::const_iterator it = bypos.find(occ_key_of(&n->nbrPos[k]));
            if (it != bypos.end()) {
                n->ps.nbr[k] = &it->second->ps;
            }
        }
    }
    /* cmp/anchor: нативная gate1 — сравнение УКАЗАТЕЛЕЙ cur->pos == cmp;
     * identity воспроизводится по реальным адресам LDPos из захвата. */
    g_pos_obj.clear();
    g_obj_key.clear();
    for (i = 0; i < c.map1.size(); i++) {
        RNode *n = r->nodes[i];
        unsigned long long k = (c.map1[i].pos_addr != 0)
            ? pos_key_addr(c.map1[i].pos_addr)
            : pos_key_coord(n->pos.x, n->pos.y);
        g_pos_obj[k] = (int *)(void *)&n->pos;
        g_obj_key[(int *)(void *)&n->pos] = k;
        g_pos_ps[(int *)(void *)&n->pos] = &n->ps;
    }
    {
        unsigned long long key;
        std::map<unsigned long long, int *>::const_iterator it;
        r->cmpPos.x = c.cmp[0]; r->cmpPos.y = c.cmp[1];
        key = (c.cmp_addr != 0) ? pos_key_addr(c.cmp_addr)
                                : pos_key_coord(c.cmp[0], c.cmp[1]);
        it = g_pos_obj.find(key);
        if (it == g_pos_obj.end()) {
            g_pos_obj[key] = (int *)(void *)&r->cmpPos;
            g_obj_key[(int *)(void *)&r->cmpPos] = key;
            r->cmpPtr = &r->cmpPos;
        } else {
            r->cmpPtr = (LDPos *)(void *)it->second;
        }
    }
    {
        unsigned long long key = pos_key_coord(c.anchor[0], c.anchor[1]);
        std::map<unsigned long long, int *>::const_iterator it =
            g_pos_obj.find(key);
        ai->anchor = (it == g_pos_obj.end())
                     ? &r->anchorPos : (LDPos *)(void *)it->second;
    }
    rep_rebuild_m1(r);
    ai->cand_head = &r->m1head;
    ai->reserved_70 = (int)r->nodes.size();

    for (i = 0; i < c.occ.size(); i++) {
        TreeNode *tn = new TreeNode;
        memset(tn, 0, sizeof(*tn));
        tn->key = c.occ[i];
        tn->is_nil = 0;
        r->m2.nodes.push_back(tn);
    }
    /* v3: восстанавливаем значения занятых узлов (ветка 2 dd70). Старые
     * захваты и явный null оставляют value == NULL, как и раньше. */
    if (!c.occ_vals.empty()) {
        for (i = 0; i < r->m2.nodes.size(); i++) {
            std::map<int, OccVal>::const_iterator oi =
                c.occ_vals.find(c.occ[i]);
            if (oi == c.occ_vals.end() || !oi->second.has) continue;
            r->m2.nodes[i]->value = rp_make_occ_ps(oi->second);
        }
    }
    rep_rebuild_m2(r);
    ai->map2 = (void *)&r->m2;
    ai->occ_head = &r->m2.head;

    /* leaf-поток текущего вызова */
    g_cur = const_cast<Call *>(&c);
    g_pi = g_hi = g_ti = 0;
    g_fake_path.clear();
    g_iter_seq.clear();
    g_have_exit = false;
    g_replay_leaf_underflow = 0;      /* флаг — на вызов, не на прогон */
}

static const Call *rep_cur(void) { return g_cur; }

/* ---------------------- точки подмены (BATTLE_REPLAY) ------------------- */

extern "C" int replay_leaf_next(int kind) {
    const Call *c = rep_cur();
    if (c == NULL) { g_replay_leaf_underflow = 1; return 0; }
    if (kind == 1) {
        if (g_hi < c->leaves_heur.size()) return c->leaves_heur[g_hi++];
        g_replay_leaf_underflow = 1; return 0;
    }
    if (g_ti < c->leaves_timeout.size()) return c->leaves_timeout[g_ti++];
    g_replay_leaf_underflow = 1; return 0;
}

extern "C" void *replay_path_next(void) {
    const Call *c = rep_cur();
    if (c == NULL || g_pi >= c->leaves_path.size()) {
        g_replay_leaf_underflow = 1;
        return NULL;
    }
    PathLeaf pl = c->leaves_path[g_pi++];
    if (pl.null) return NULL;
    /* Объект-носитель реального адреса LDPos из захвата: нативный cb40
     * возвращает тот же LDPos, что потом лежит в pfs->pos, поэтому identity
     * cur->pos == cmp — это равенство адресов, а не координат. */
    unsigned long long key = (pl.addr != 0)
        ? pos_key_addr(pl.addr)
        : pos_key_coord(pl.w[3], pl.w[4]);
    std::map<unsigned long long, int *>::const_iterator it = g_pos_obj.find(key);
    if (it != g_pos_obj.end()) return it->second;
    int *obj = (int *)calloc(8, sizeof(int));   /* +0x00..+0x1f */
    for (int k = 0; k < 5; k++) obj[k] = pl.w[k];
    g_fake_path.push_back(obj);
    g_pos_obj[key] = obj;
    g_obj_key[obj] = key;
    return obj;
}

extern "C" void *replay_grid_cell(int x, int y) {
    /* cb40 подменён; ячейка нужна только как не-NULL маркер. */
    static int cell[16];
    cell[3] = x; cell[4] = y;    /* +0x0c/+0x10 */
    return cell;
}

extern "C" void replay_note_rescan(void *cand) {
    int xy[2];
    if (cand == NULL) { xy[0] = -1; xy[1] = -1; }
    else {
        LDPos *p = *(LDPos **)cand;
        xy[0] = p ? p->x : -1;
        xy[1] = p ? p->y : -1;
    }
    g_iter_seq.push_back(xy[0]);
    g_iter_seq.push_back(xy[1]);
}

extern "C" void replay_note_exit(void *node, int code, int iters) {
    if (node == NULL) {
        g_exit_node[0] = -1; g_exit_node[1] = -1;
    } else {
        LDPos *p = *(LDPos **)node;
        g_exit_node[0] = p ? p->x : -1;
        g_exit_node[1] = p ? p->y : -1;
    }
    g_exit_code = code;
    g_exit_iters = iters;
    g_have_exit = true;
}

/* --------------------------- native:: stub'ы ---------------------------- */

namespace native {
unsigned char *base(void) { return (unsigned char *)0x400000; }
void init(void) {}
void *fn(unsigned int va) { (void)va; return NULL; }
TreeNode *lower_bound(void *map, unsigned int key, void *hint) {
    (void)hint;
    return s6_map_lower_bound(&((RepMap2 *)map)->head, key);
}
TreeNode *lower_bound_slots(void *map, int *out, int *key) {
    RepMap2 *m;
    TreeNode *found;
    if (g_rep != NULL &&
        map == (void *)((unsigned char *)g_rep->aiBuf + 0x74)) {
        m = &g_rep->m2;
    } else {
        m = (RepMap2 *)map;
    }
    found = &m->head;
    /* Нативный FUN_00434bc0 (lower_bound) + проверка @0x45ded2: занятым
     * считается только ТОЧНЫЙ ключ; lower_bound ушёл вправо → head (end). */
    for (size_t i = 0; i < m->nodes.size(); i++) {
        unsigned ek = (unsigned)m->nodes[i]->key;
        if (ek == (unsigned)*key) {
            found = m->nodes[i];
            break;
        }
        if (ek > (unsigned)*key) {
            break;                      /* exact-match нет → head */
        }
    }
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "lb: key=%d size=%u found=%s val=%p\n", *key,
                (unsigned)m->nodes.size(),
                found == &m->head ? "head" : "node", found->value);
    }
    *out = (int)(size_t)found;
    return found;
}
TreeNode *tree_next(TreeNode *node) { return s6_tree_next(node); }
int tree_lookup(void *world, unsigned int key) { (void)world; (void)key; return 0; }
void register_occupied_slot(void *ai, LDObject *obj) {
    Rep *r = (Rep *)ai;
    PathState *ps = (PathState *)obj;
    LDPos *p;
    TreeNode *tn;
    TreeNode *m2tn;

    if (ps == NULL) return;
    p = ps->pos;
    /* FUN_0045dd00: объект в map1 (ключ = LDPos*, ключ узла = pos_addr) и в
     * map2 (ключ = координаты). map1 хранит сам объект — identity
     * cur->pos == cmp сравнивает указатели. */
    tn = new TreeNode;
    memset(tn, 0, sizeof(*tn));
    tn->value = ps;
    tn->is_nil = 0;
    {
        /* Ключ map1 = адрес LDPos (нативный dd00); у new-узла это адрес его
         * cb40-результата из replay_path_next. Нет адреса — 0 (старый захват,
         * тогда порядок задаёт (x,y)-фолбэк в m1_less). */
        std::map<int *, unsigned long long>::const_iterator ai_it =
            g_obj_key.find((int *)(void *)p);
        tn->key = (ai_it != g_obj_key.end())
                  ? (int)(unsigned)ai_it->second : 0;
    }
    r->m1nodes.push_back(tn);
    m2tn = new TreeNode;
    memset(m2tn, 0, sizeof(*m2tn));
    m2tn->key = p ? occ_key_of(p) : 0;
    m2tn->value = obj;
    m2tn->is_nil = 0;
    r->m2.nodes.push_back(m2tn);
    if (replay_trace_level() >= 2) {
        fprintf(stderr, "register: obj=%p pos=(%d,%d) key=%d m2size=%u\n",
                (void *)obj, p ? p->x : -1, p ? p->y : -1, m2tn->key,
                (unsigned)r->m2.nodes.size());
    }
    rep_rebuild_m1(r);
    rep_rebuild_m2(r);
    ((BattleAI *)(void *)r->aiBuf)->reserved_70 = (int)r->m1nodes.size();
}
int vcall0(void *receiver, unsigned int off) { (void)receiver; (void)off; return 0; }
int vcall1(void *receiver, unsigned int off, int arg) {
    (void)receiver; (void)off; (void)arg; return 0;
}
long av_count(void) { return 0; }
int av_filter(void) { return EXCEPTION_EXECUTE_HANDLER; }
int grid_query(void *world, int a1, int a2, int a3, int a4) {
    (void)world; (void)a1; (void)a2; (void)a3; (void)a4; return 0;
}
int pt_in_rect(const int *rect, int x, int y) {
    return (rect[0] <= x && x < rect[2] && rect[1] <= y && y < rect[3]) ? 1 : 0;
}
void *operator_new(unsigned int size) { return calloc(1, size); }
void deallocate(void *ptr) { free(ptr); }
} /* namespace native */

/* Логгер/утилиты, нужные battle/*.cpp. LD_REPLAY_TRACE=1/2 включает вывод
 * порта в stderr (1 — info, 2 — info+debug), по умолчанию тишина. */
volatile LONG g_log_level = 0;
static int replay_trace_level(void) {
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("LD_REPLAY_TRACE");
        v = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    return v;
}
static void replay_trace(const char *fmt, va_list ap) {
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void ld_info(const char *fmt, ...) {
    va_list ap;
    if (replay_trace_level() < 1) return;
    va_start(ap, fmt);
    replay_trace(fmt, ap);
    va_end(ap);
}
void ld_debug(const char *fmt, ...) {
    va_list ap;
    if (replay_trace_level() < 2) return;
    va_start(ap, fmt);
    replay_trace(fmt, ap);
    va_end(ap);
}
int ld_rate(volatile LONG *ctr, LONG first, LONG each) {
    (void)ctr; (void)first; (void)each;
    return (replay_trace_level() >= 2) ? 1 : 0;   /* LD_REPLAY_TRACE=2 → всё */
}
LONGLONG ld_now_us(void) { return 0; }
void battle_trace_e0a0_enter(void *this_, int filter) { (void)this_; (void)filter; }
void battle_trace_e0a0_exit(void *node) { (void)node; }
void battle_trace_e0a0_latch(void *cand) { (void)cand; }
void battle_trace_visit(void) {}
void battle_capture_e0a0_enter(void *, int, void *, void *, void *) {}
void battle_capture_latch(void *) {}
void battle_capture_e0a0_exit(void *) {}

/* ------------------------------- вывод ---------------------------------- */

static void write_out_node(FILE *f, int x, int y, bool is_null) {
    if (is_null) fputs("null", f);
    else fprintf(f, "[%d,%d]", x, y);
}

static void write_out_map1(FILE *f, Rep *r) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *head = ai->cand_head;
    TreeNode *cur;
    bool first = true;
    fputc('[', f);
    if (head != NULL) {
        cur = head->left;
        for (size_t guard = 0; cur != NULL && cur != head && guard < 200000;
             guard++) {
            PathState *ps = (PathState *)cur->value;
            if (ps != NULL) {
                LDPos *p = ps->pos;
                int x = p ? p->x : -1, y = p ? p->y : -1;
                PathState *pp = ps->parent;
                LDPos *ppp = pp ? pp->pos : NULL;
                fprintf(f, "%s{\"pos\":[%d,%d],\"filter\":%d,\"g\":%d,"
                        "\"d2\":%d,\"h10k\":%d,\"cost\":%d,\"parent\":[%d,%d],"
                        "\"nbr\":[",
                        first ? "" : ",", x, y, ps->filter, ps->g_cost,
                        ps->dist2, ps->h10000, ps->metric,
                        ppp ? ppp->x : -1, ppp ? ppp->y : -1);
                for (int k = 0; k < 9; k++) {
                    PathState *nb = ps->nbr[k];
                    LDPos *nbp = nb ? nb->pos : NULL;
                    fprintf(f, "%s[%d,%d]", k ? "," : "",
                            nbp ? nbp->x : -1, nbp ? nbp->y : -1);
                }
                fprintf(f, "],\"occ_key\":%d}",
                        (int)(((unsigned short)y << 16) | (unsigned short)x));
                first = false;
            }
            cur = s6_tree_next(cur);
        }
    }
    fputc(']', f);
}

static void write_out_occ(FILE *f, Rep *r) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *head = ai->occ_head;
    TreeNode *cur;
    bool first = true;
    fputc('[', f);
    if (head != NULL) {
        cur = head->left;
        for (size_t guard = 0; cur != NULL && cur != head && guard < 200000;
             guard++) {
            fprintf(f, "%s%d", first ? "" : ",", cur->key);
            first = false;
            cur = s6_tree_next(cur);
        }
    }
    fputc(']', f);
}

static void write_res_port(FILE *f, const Call &c, Rep *r) {
    int out_code = 0;
    fprintf(f, "{\"v\":1,\"kind\":\"res_port\",\"seq\":%d,\"node\":", c.seq);
    write_out_node(f, g_exit_node[0], g_exit_node[1], g_exit_node[0] == -1);
    /* s6_target_search пишет код в out_code; дублируем из replay-нотиса. */
    (void)out_code;
    fprintf(f, ",\"code\":%d,\"iters\":%d,\"iter_seq\":[",
            g_exit_code, g_exit_iters);
    for (size_t i = 0; i < g_iter_seq.size(); i += 2) {
        bool null = (g_iter_seq[i] == -1 && g_iter_seq[i + 1] == -1);
        fprintf(f, "%s", i ? "," : "");
        write_out_node(f, g_iter_seq[i], g_iter_seq[i + 1], null);
    }
    fputs("],\"map1_after\":", f);
    write_out_map1(f, r);
    fputs(",\"occ_after\":", f);
    write_out_occ(f, r);
    fprintf(f, ",\"leaf_used\":[%u,%u,%u],\"leaf_cap\":[%u,%u,%u]"
            ",\"leaf_underflow\":%d}\n",
            (unsigned)g_pi, (unsigned)g_hi, (unsigned)g_ti,
            (unsigned)c.leaves_path.size(), (unsigned)c.leaves_heur.size(),
            (unsigned)c.leaves_timeout.size(), g_replay_leaf_underflow);
}

/* --------------------------------- main --------------------------------- */

static bool read_line(FILE *f, std::string &out) {
    out.clear();
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') return true;
        out.push_back((char)ch);
    }
    out.clear();          /* хвостовая недописанная запись (игра погашена
                             посреди записи) не является строкой захвата */
    return false;
}

int main(int argc, char **argv) {
    const char *inpath, *outpath;
    FILE *fin, *fout;
    std::string line;
    int rc = 0;
    std::map<int, Call *> caps;
    std::vector<Call *> calls;
    char err[512];

    if (argc != 3) {
        fprintf(stderr, "usage: battle_replay <capture.jsonl> <res_port.jsonl>\n");
        return 2;
    }
    inpath = argv[1]; outpath = argv[2];
    if (replay_trace_level() >= 2) {
        g_log_level = 2;                /* LD_DEBUG (ldhook_internal.h) */
    }
    fin = fopen(inpath, "rb");
    if (fin == NULL) {
        fprintf(stderr, "ERROR: cannot open capture '%s'\n", inpath);
        return 2;
    }
    fout = fopen(outpath, "wb");
    if (fout == NULL) {
        fprintf(stderr, "ERROR: cannot open output '%s'\n", outpath);
        fclose(fin);
        return 2;
    }
    try {
        while (read_line(fin, line)) {
            JVal v = JParser(line).parse();
            const JVal &kv = jget(v, "kind", "record");
            if (kv.t != JVal::STR) {
                throw std::runtime_error("record.kind: expected string");
            }
            std::string kind = kv.s;
            int seq = (int)jint(v, "seq", "record");
            if (kind == "cap") {
                if (caps.count(seq)) throw std::runtime_error("duplicate cap seq");
                Call *c = new Call;
                c->cap = v;
                read_call(*c, v, NULL);
                caps[seq] = c;
                calls.push_back(c);
            } else if (kind == "res") {
                if (!caps.count(seq)) throw std::runtime_error("res without cap");
                Call *c = caps[seq];
                if (c->has_res) throw std::runtime_error("duplicate res seq");
                c->has_res = true;
                c->res = v;
                read_leaves(*c, v);
            } else if (kind == "error") {
                throw std::runtime_error("capture contains error record");
            }
        }
    } catch (const std::exception &e) {
        _snprintf_s(err, sizeof(err), _TRUNCATE, "ERROR: bad capture: %s", e.what());
        fprintf(stderr, "%s\n", err);
        fclose(fin); fclose(fout);
        return 2;
    }
    fclose(fin);

    for (size_t i = 0; i < calls.size(); i++) {
        Call *c = calls[i];
        if (!c->has_res) {
            if (i + 1 == calls.size()) {
                /* Захват обрезан при гашении игры — последний вызов без res. */
                fprintf(stderr, "WARN: trailing cap seq=%d without res ignored\n",
                        c->seq);
                break;
            }
            fprintf(stderr, "ERROR: cap seq=%d has no res record\n", c->seq);
            rc = 2;
            break;
        }
        Rep *r = new Rep;
        try {
            rep_build(r, *c);
            g_rep = r;
            BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
            void *cmpPos = (void *)r->cmpPtr;
            int out_code = 0;
            s6_target_search(ai, (void *)(size_t)c->filter, cmpPos,
                             (void *)(size_t)c->timeout_bits, &out_code);
            write_res_port(fout, *c, r);
        } catch (const std::exception &e) {
            fprintf(stderr, "ERROR: replay seq=%d failed: %s\n", c->seq, e.what());
            rc = 2;
        }
        g_rep = NULL;
        delete r;
    }
    fclose(fout);
    if (rc == 0) {
        printf("replay: %u calls -> %s\n", (unsigned)calls.size(), outpath);
    }
    return rc;
}
