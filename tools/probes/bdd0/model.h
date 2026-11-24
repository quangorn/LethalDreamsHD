/* tools/probes/bdd0/model.h — контракт модельного нативного слоя (генератор
 * синтетического захвата bdd0_synth.exe).
 *
 * Модель — НЕ натив: она возвращает детерминированные значения, вычислимые из
 * снимка (ctx/BattleAI/лента/gs), и записывает каждый вызов листа в поток
 * bdd0_leaf в ПРОСТРАНСТВЕ ИГРЫ (адреса raw, не shadow). Поэтому записанный
 * сценарий самосогласован: replay-слой харнесса отдаёт те же значения и
 * материализует те же указатели.
 */
#ifndef BDD0_MODEL_H
#define BDD0_MODEL_H

#include "harness.h"

/* Сырые (игровые) адреса сценария: модель отдаёт их в `ret` вместо shadow. */
struct ModelEnv {
    unsigned self_game;    /* адрес unit */
    unsigned e0_game;      /* unit+0xe0 */
    unsigned e0_44_game;
    unsigned tree_game;
    unsigned band_game;    /* band_begin */
    unsigned scratch_game; /* стек-скретч вызывающего (out-тройки) */
    /* v14: после-состояние ленты — 45eee0 перестраивает её внутри тика.
     * band_post_game != 0 → модель пишет bdd0_native ww-пару [0x5c,0x60]
     * (unit+0x194/+0x198 = BattleAI+0x3c+0x20/+0x24), как это делает натив;
     * иначе (0) — старая запись без ww (resync-fallback). */
    unsigned band_post_game;
    int band_post_n;
    /* Сверх длины: байты после post_band_n*0x18 — для проверки усечения
     * (end-begin не кратно 0x18). 0 — обычная лента. */
    int band_post_extra;
};

struct ModelCfg {
    int grid_query;        /* 44bbf0: возврат (0 = «сетки нет») */
    int anchor_null;       /* 458050: возврат NULL */
    float interp_add;      /* 45b150: ST(0) = arg1 + interp_add */
    int moved;             /* 45b3e0: возврат */
};

void model_set(const ModelEnv &env, const ModelCfg &cfg);
void model_leaf_reset(void);
int model_leaf_count(void);
const LeafRec *model_leaf_at(int i);
int model_native_count(void);
const NativeRec *model_native_at(int i);

#endif
