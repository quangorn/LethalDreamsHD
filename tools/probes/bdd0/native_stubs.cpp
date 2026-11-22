/* tools/probes/bdd0/native_stubs.cpp — нативный слой реплея: не-логические
 * обёртки native.h. Компилируется ВМЕСТО tools/ldhook/battle/native.cpp.
 *
 * Офлайн игры нет, поэтому:
 *   - `base()` / `fn(VA)` / `literal(VA)` инертны: NULL / NULL / "". Ни один
 *     путь этого файла не заходит в код игры;
 *   - GUI/string/log/message/dtor/vtable-обёртки — no-op (см. таблицу ниже);
 *   - `game_state_get` и `player_get` возвращают shadow-объекты арены, иначе
 *     тело сразу разыменовало бы NULL. Их СОДЕРЖИМОЕ не смоделировано
 *     (game_state — из gs_b64, если захват её несёт; player — нули), это
 *     печатается в сводке прогона как not-modelled счётчик, а не выдаётся за
 *     проверенный факт.
 *
 * Каждая фраза «not modelled» — из списка ниже; проверки, которые от этого
 * зависят (поведенческие), в defaults отсутствуют по определению.
 *
 *   not modelled (no-op): band_free, native_45af10, clear_maps,
 *   search_params_update, band_rebuild, pathfinder_init, pathfinder_dtor,
 *   world_notify_snapshot, phase_time(*out_f=0), vcall0/vcall1, vcall38_ptr,
 *   ai_time_update, msg_ctor/msg_copy/msg_string_dtor, cursor_mode_update,
 *   state_flag_log, string_ctor/string_format/string_c_str/string_dtor,
 *   player_pos_set, log_assert, output_debug_string_a, unit_ai_tick,
 *   target_search (NULL), lower_bound/tree_* (NULL/0), register_occupied_slot,
 *   tree_lookup (0), set_rect_empty (нули+1), grid_anchor-флаг.
 */
#include <stdlib.h>
#include <string.h>

#include "../../ldhook/battle/native.h"
#include "../../ldhook/ldhook_internal.h"
#include "harness.h"

/* --------------------------- счётчики прогона ---------------------------- */
static int g_unmodelled = 0;
static int g_unit_ai_tick = 0;

int bdd0_unmodelled_calls(void) { return g_unmodelled; }
int bdd0_unmodelled_unit_ai_tick(void) { return g_unit_ai_tick; }

static void noop(const char *what)
{
    (void)what;
    g_unmodelled++;
}

/* ------------------------------ логгер ---------------------------------- */
volatile LONG g_log_level = 0;
static int trace_level(void)
{
    static int v = -1;
    if (v < 0) {
        const char *s = getenv("BDD0_TRACE");
        v = (s != NULL && *s != '\0') ? atoi(s) : 0;
    }
    return v;
}
void ld_info(const char *fmt, ...) { (void)fmt; }
void ld_debug(const char *fmt, ...) { (void)fmt; }
int ld_rate(volatile LONG *ctr, LONG first, LONG each)
{
    (void)ctr; (void)first; (void)each;
    return (trace_level() >= 2) ? 1 : 0;
}
LONGLONG ld_now_us(void) { return 0; }

/* --------------- s6-гейты, которые нужны тику (не порт site6) ------------ */
/* Порт юнит-AI (site6) офлайн выключен — ровно как прогон без LDHOOK_SITE6.
 * Если гейт когда-нибудь станет 1, harness обязан линковать move.cpp; сейчас
 * s6_unit_ai_tick — громкий счётчик, а не тихая подстановка. */
int s6_port_enabled(void) { return 0; }
int s6_unit_ai_tick(BattleAI *self, void *rec_arg)
{
    (void)self; (void)rec_arg;
    g_unit_ai_tick++;
    return 0;
}

/* ---------------------------- native-слой ------------------------------- */
namespace native {

unsigned char *base(void) { return NULL; }        /* офлайн образа игры нет */
void init(void) {}
void *fn(unsigned int va) { (void)va; return NULL; }

TreeNode *lower_bound(void *map, unsigned int key, void *hint)
{
    (void)map; (void)key; (void)hint; noop("lower_bound"); return NULL;
}
TreeNode *lower_bound_slots(void *map, int *out, int *key)
{
    (void)map; (void)out; (void)key; noop("lower_bound_slots"); return NULL;
}
TreeNode *tree_next(TreeNode *node) { (void)node; noop("tree_next"); return NULL; }
int tree_lookup(void *world, unsigned int key)
{
    (void)world; (void)key; noop("tree_lookup"); return 0;
}
void register_occupied_slot(void *ai, LDObject *obj)
{
    (void)ai; (void)obj; noop("register_occupied_slot");
}

void band_free(void *band) { (void)band; noop("band_free"); }
void native_45af10(void *band) { (void)band; noop("native_45af10"); }
void clear_maps(void *ai) { (void)ai; noop("clear_maps"); }
void search_params_update(void *ai) { (void)ai; noop("search_params_update"); }
void band_rebuild(void *ai, void *node, int bits)
{
    (void)ai; (void)node; (void)bits; noop("band_rebuild");
}
void *target_search(void *ai, int filter, void *cmp_pos, int arg3, int *out_code)
{
    (void)ai; (void)filter; (void)cmp_pos; (void)arg3;
    if (out_code != NULL) *out_code = 0;
    noop("target_search");
    return NULL;
}
void pathfinder_init(void *pf) { (void)pf; noop("pathfinder_init"); }
void pathfinder_dtor(void *pf) { (void)pf; noop("pathfinder_dtor"); }

int set_rect_empty(void *rect)
{
    noop("set_rect_empty");
    if (rect != NULL) memset(rect, 0, 16);
    return 1;
}

int vcall0(void *receiver, unsigned int voffset)
{
    (void)receiver; (void)voffset; noop("vcall0"); return 0;
}
int vcall1(void *receiver, unsigned int voffset, int arg)
{
    (void)receiver; (void)voffset; (void)arg; noop("vcall1"); return 0;
}
int vcall38_ptr(void *unit, void *ptr)
{
    (void)unit; (void)ptr; noop("vcall38_ptr"); return 0;
}

long av_count(void) { return 0; }
int av_filter(void) { return EXCEPTION_EXECUTE_HANDLER; }

/* PtInRect — чистый предикат WinAPI, не игровой код: моделируется точно
 * (rect = [left, top, right, bottom], полуинтервал по каждой оси). */
int pt_in_rect(const int *rect, int x, int y)
{
    if (rect == NULL) return 0;
    return (rect[0] <= x && x < rect[2] && rect[1] <= y && y < rect[3]) ? 1 : 0;
}

/* Объект состояния игры FUN_00442570: shadow-область арены "gs" (0x20 Б).
 * Содержимое — gs_b64 из захвата, если он есть; иначе нули (phase=0) и это
 * видно в сводке прогона (gs=absent). Указатель стабилен на весь тик, поэтому
 * save/write/restore в R14 сходятся. */
void *game_state_get(void *unit, int flags)
{
    (void)unit; (void)flags;
    Arena *a = arena_current();
    void *gs = arena_region_w(a, "gs");
    if (gs == NULL) noop("game_state_get(no arena)");
    return gs;
}

void world_notify_snapshot(void *unit, const void *snapshot32)
{
    (void)unit; (void)snapshot32; noop("world_notify_snapshot");
}

/* vtable+0x2c — float фазы: значение не снимается в v12, пишем 0.0f
 * (не оставляем неинициализированный слот кадра — иначе гейты плавают). */
void phase_time(void *unit, float *out_f)
{
    (void)unit; noop("phase_time");
    if (out_f != NULL) *out_f = 0.0f;
}

/* vtable+0x40 — player-getter: объект игрока офлайн не моделируется. Возврат
 * ненулевой (реальный игрок в бою существует), объект — shadow-область
 * "player" арены (нули), поэтому NULL-гейты тела ведут себя как в нативе и
 * разыменование флагов остаётся внутри объявленной памяти. */
void *player_get(void *unit, int arg)
{
    (void)unit; (void)arg; noop("player_get");
    Arena *a = arena_current();
    return arena_region_w(a, "player");
}

int string_obj_cmp(void *a, void *b)
{
    (void)a; (void)b; noop("string_obj_cmp"); return 0;   /* AL=0: не равны */
}
void ai_time_update(void *unit, const void *msg60)
{
    (void)unit; (void)msg60; noop("ai_time_update");
}
void msg_ctor(void *this_) { (void)this_; noop("msg_ctor"); }
void msg_copy(void *dst, const void *src) { (void)dst; (void)src; noop("msg_copy"); }
void msg_string_dtor(void *obj) { (void)obj; noop("msg_string_dtor"); }

void *player_flag_get(void *player, int flag)
{
    (void)flag; noop("player_flag_get"); return player;
}
void *player_flags(void *flags_obj) { noop("player_flags"); return flags_obj; }
void cursor_mode_update(void *unit, int arg)
{
    (void)unit; (void)arg; noop("cursor_mode_update");
}
void state_flag_log(void *unit, int flag) { (void)unit; (void)flag; noop("state_flag_log"); }

void *string_ctor(void *obj, const char *s)
{
    (void)s; noop("string_ctor");
    if (obj != NULL) memset(obj, 0, 12);
    return obj;
}
void *string_format(void *dst, const void *a2, const void *a3)
{
    (void)a2; (void)a3; noop("string_format");
    return dst;
}
const char *string_c_str(void *obj) { (void)obj; noop("string_c_str"); return ""; }
void string_dtor(void *obj) { (void)obj; noop("string_dtor"); }
void player_pos_set(void *player, void *triple)
{
    (void)player; (void)triple; noop("player_pos_set");
}
void log_assert(void *unit, const char *msg, int code)
{
    (void)unit; (void)msg; (void)code; noop("log_assert");
}
void output_debug_string_a(const char *s) { (void)s; noop("output_debug_string_a"); }

/* Игровой литерал по VA: офлайн строки нет — пустая строка, не «похожая». */
const char *literal(unsigned int va) { (void)va; noop("literal"); return ""; }

int unit_ai_tick(void *ai, void *rec)
{
    (void)ai; (void)rec; noop("unit_ai_tick");
    return 0;
}

void *operator_new(unsigned int size) { return calloc(1, size); }
void deallocate(void *ptr) { free(ptr); }

} /* namespace native */
