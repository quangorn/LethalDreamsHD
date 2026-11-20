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
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>

#include "../ldhook/battle/battle.h"
#include "../ldhook/battle/native.h"

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
    int nbr[9][2];
};
struct PathLeaf { bool null; int w[5]; };

struct Call {
    JVal cap;                       /* исходная cap-запись */
    int seq, tick, unit, filter, out_ptr;
    int cmp[2];
    unsigned timeout_bits;
    unsigned timer_bits, final_bits, dist_bits, ctx_flags, world_scale_bits;
    int yrad, xrad, rect[4], anchor[2], trk;
    unsigned bd2;
    int ai_code;
    std::vector<CapNode> map1;
    std::vector<int> occ;
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

    c.seq = (int)jint(cap, "seq", "cap");
    c.tick = (int)jint(cap, "tick", "cap");
    c.unit = (int)jint(cap, "unit", "cap");
    c.filter = (int)jint(args, "filter", "cap.args");
    read_pos(jget(args, "cmp", "cap.args"), c.cmp, "cap.args.cmp");
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
    static std::map<int, int *> g_pos_obj;   /* ключ occ → канонический LDPos */
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

static int occ_key_of(LDPos *p) {
    return (int)(((unsigned short)p->y << 16) | (unsigned short)p->x);
}

static void rep_rebuild_m1(Rep *r) {
    TreeNode *nil = rep_nil(r);
    size_t i, n = r->m1nodes.size();
    for (i = 0; i < n; i++) {
        TreeNode *tn = r->m1nodes[i];
        tn->left = nil;
        tn->right = (i + 1 < n) ? r->m1nodes[i + 1] : &r->m1head;
        tn->parent = (i > 0) ? r->m1nodes[i - 1] : &r->m1head;
        tn->key = 0;                        /* native dd00: map1 key = 0 */
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

static void rep_build(Rep *r, const Call &c) {
    BattleAI *ai = (BattleAI *)(void *)r->aiBuf;
    TreeNode *nil = rep_nil(r);
    size_t i;
    std::map<int, RNode *> bypos;

    memset(r, 0, sizeof(*r));
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
     * восстанавливаем через канонический объект координат (см. replay_path_next). */
    g_pos_obj.clear();
    for (i = 0; i < c.map1.size(); i++) {
        RNode *n = r->nodes[i];
        g_pos_obj[occ_key_of(&n->pos)] = (int *)(void *)&n->pos;
    }
    {
        int key = occ_key_of(&r->cmpPos);
        std::map<int, int *>::const_iterator it = g_pos_obj.find(key);
        if (it == g_pos_obj.end()) {
            r->cmpPos.x = c.cmp[0]; r->cmpPos.y = c.cmp[1];
            g_pos_obj[key] = (int *)(void *)&r->cmpPos;
            r->cmpPtr = &r->cmpPos;
        } else {
            r->cmpPtr = (LDPos *)(void *)it->second;
        }
    }
    {
        int key = occ_key_of(&r->anchorPos);
        std::map<int, int *>::const_iterator it = g_pos_obj.find(key);
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
    /* Канонический объект на координаты: нативный cb40 возвращает сам LDPos
     * ячейки сетки, один и тот же при повторных запросах; identity cur->pos ==
     * cmp держится на равенстве указателей, поэтому копии недопустимы. */
    int key = (int)(((unsigned short)pl.w[4] << 16) | (unsigned short)pl.w[3]);
    std::map<int, int *>::const_iterator it = g_pos_obj.find(key);
    if (it != g_pos_obj.end()) return it->second;
    int *obj = (int *)calloc(8, sizeof(int));   /* +0x00..+0x1f */
    for (int k = 0; k < 5; k++) obj[k] = pl.w[k];
    g_fake_path.push_back(obj);
    g_pos_obj[key] = obj;
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
    RepMap2 *m = (RepMap2 *)map;
    TreeNode *found = &m->head;
    for (size_t i = 0; i < m->nodes.size(); i++) {
        if ((unsigned)m->nodes[i]->key >= (unsigned)*key) {
            found = m->nodes[i];
            break;
        }
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
    /* FUN_0045dd00: объект в map1 (key=0, value=obj) и в map2 (key=coords).
     * map1 хранит сам объект — identity cur->pos == cmp сравнивает указатели. */
    tn = new TreeNode;
    memset(tn, 0, sizeof(*tn));
    tn->value = ps;
    tn->is_nil = 0;
    r->m1nodes.push_back(tn);
    m2tn = new TreeNode;
    memset(m2tn, 0, sizeof(*m2tn));
    m2tn->key = p ? occ_key_of(p) : 0;
    m2tn->value = obj;
    m2tn->is_nil = 0;
    r->m2.nodes.push_back(m2tn);
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

/* Логгер/утилиты, нужные battle/*.cpp */
volatile LONG g_log_level = 0;
void ld_info(const char *fmt, ...) { (void)fmt; }
void ld_debug(const char *fmt, ...) { (void)fmt; }
int ld_rate(volatile LONG *ctr, LONG first, LONG each) {
    (void)ctr; (void)first; (void)each; return 0;
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
        delete r;
    }
    fclose(fout);
    if (rc == 0) {
        printf("replay: %u calls -> %s\n", (unsigned)calls.size(), outpath);
    }
    return rc;
}
