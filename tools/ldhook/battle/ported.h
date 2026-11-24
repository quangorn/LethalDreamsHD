/* battle/ported.h — портированные логические листья тика и claim значения.
 *
 * Лист, чьё тело портировано, в replay-потоке больше НЕ запрашивается: его
 * запись bdd0_leaf снимает claim — порт считает значение сам и сообщает его
 * вызовом B0_LEAF_CLAIM (или B0_LEAF_CLAIM_W, если значимый результат листа —
 * dword'ы, записанные через указатель). Replay-слой сопоставляет claim со
 * следующей записью потока (тот же seq/va/порядок) и продвигает поток;
 * значение сверяет проверка `leaf_value` (указатель — символьно, как в
 * leaf_args), расхождение потока — `leaf_seq`. Запрос такого листа
 * (`native::…`) — нарушение механизма.
 *
 * Список — единственный источник истины: он нужен харнессу (там по нему replay
 * решает, где ждать claim, а где запрос). В живом билде (BATTLE_REPLAY не
 * определён) остаются только VA-константы и no-op макрос claim.
 */
#ifndef BATTLE_PORTED_H
#define BATTLE_PORTED_H

/* Шаг элемента ленты (FUN_0045af50: (end-begin)/0x18). */
#define B0_BAND_ELEM 0x18

/* Смещение ленты в BattleAI (FUN_0045c370: ESI = ECX + 0x3c). */
#define B0_BATTLE_AI_BAND 0x3c

/* Поля вектора ленты относительно её начала (FUN_0045af50:
 * `MOV EAX,[ECX+0x20]; … MOV ECX,[ECX+0x24]`). */
#define B0_BAND_BEGIN_OFF 0x20
#define B0_BAND_END_OFF 0x24

/* VA портированных листьев. */
#define B0_LEAF_BAND_HEAD 0x45c370u    /* begin==0 ? NULL : count ? ai+0x3c : NULL */
/* Длина ленты (FUN_0045af50): begin==0 ? 0 : (end-begin)/0x18 (знаковое). */
#define B0_LEAF_BAND_COUNT 0x45af50u
/* Позиция из ctx (FUN_0045c6f0): EAX = *(this) (game_ctx); EAX == 0 → out[0..2]
 * = 0; иначе ECX = [EAX+0xc] (указатель на тройку), out[0..2] = [ECX+0..8];
 * EAX = out. Значимый результат листа — записанная тройка (claim_W). */
#define B0_LEAF_READ_POS 0x45c6f0u
/* Float-гейт ленты (FUN_0045b150): минимум d² от (arg1,arg3) до полилинии
 * ленты, BEST init 100.0f; ECX = лента, 3 стек-аргумента (arg2 мёртв),
 * `RET 0xc`, результат — ST(0) = float32. Тело — s6_band_interp_gate в
 * tick.cpp (claim есть). В b0_ported_leaf_va НЕ входит: вход листа —
 * pos/level каждого 24-байтного элемента (`[elem+0x14]` в кучу игры), ни один
 * формат захвата (v12..v15) его не пишет, поэтому сайты 0x44c4cb/0x44c5e0
 * реплеят записанный натив (bdd0_band_geometry_modelled). Включение после
 * захвата v16 с геометрией — добавить сюда И в список ниже, разом. */
#define B0_LEAF_INTERP_GATE 0x45b150u
/* Равенство float-троек (FUN_00403e80): 1 ⟺ все три пары `[a+i*4]`/`[b+i*4]`
 * сравнились равными (unordered считается НЕравным — маска 0x44 через PF).
 * __thiscall: ECX = a (первый указатель), единственный стек-аргумент b
 * (`MOV EDX,[ESP+4]` @0x403e82), `RET 4`; EAX = 0/1, вызывающий тестирует AL. */
#define B0_LEAF_POS3_EQUAL 0x403e80u
/* Голова цепочки со статусом 2 (FUN_00456b60): спуск по `+0x28`, пока
 * tree_lookup(world, node->key_rec->key) == 2; возврат — первый узел с другим
 * статусом, NULL при обрыве цепочки. __thiscall: ECX = GridCell*, 1 стек-
 * аргумент = world, `RET 4`; EAX = GridCell* или 0. Единственный вызов —
 * 0x458142 в 0x458050 (сайт 34, leaf_sites.cpp): тело листа — s6_grid_chain_head
 * в grid.cpp, claim выпускает оно; оба вложенных листа 0x458050 на пяти
 * старых захватах не исполняются (см. B0_LEAF_GRID_KEY_LOOKUP). */
#define B0_LEAF_CHAIN_HEAD 0x456b60u
/* Поиск ячейки сетки по ключу (FUN_00457e20): x0/y0 = trunc((arg − смещение) *
 * (1.0f/scale_mul)) при PC=53, idx = rows[x0][y0], ячейка листа/cells+idx*0x34,
 * цепочка +0x28 → s6_chain_last, path A по +0x24 (value = [rec+0x18], цепочка
 * +0x1c) против arg4; 3×3-скан возвращает слот [ESP+0x10] (к тому моменту 0).
 * Ret NaN/out-of-range → int64-indefinite (0x4d17e0), поэтому младшие 32 бита 0;
 * arg2 (S+8) не читается (в block[4] его кладёт 0x458050); block[8] = arg3 —
 * живой y0. __thiscall: ECX = GridLevel*, 4 стек-аргумента, RET 0x10;
 * EAX = GridCell* или 0. Единственный вызывающий — s6_grid_anchor_lookup
 * (сайт 34, 0x45811a): тело — s6_grid_key_lookup в grid.cpp. Порт 0x458050
 * claim НЕ выпускает (его нет в b0_ported_leaf_va) — он вызывает этот лист из
 * ветки tree+key, и лист заявляется сам; в офлайн-харнессе 0x458050 сегодня
 * реплеится записанным нативом (модели сетки в захвате нет), поэтому оба
 * вложенных листа в захватах v12..v15 не исполняются вовсе. */
#define B0_LEAF_GRID_KEY_LOOKUP 0x457e20u

#ifdef BATTLE_REPLAY
/* VA портированных листьев: список живёт только в replay-сборке. В живом
 * билде (BATTLE_REPLAY не определён) порт не судит запросы, поэтому статики
 * были бы мёртвыми. */
static const unsigned int b0_ported_leaf_va[] = {
    B0_LEAF_BAND_HEAD,
    B0_LEAF_BAND_COUNT,
    B0_LEAF_READ_POS,
    B0_LEAF_POS3_EQUAL,
    B0_LEAF_CHAIN_HEAD,
    B0_LEAF_GRID_KEY_LOOKUP
};

#define B0_PORTED_LEAF_COUNT \
    ((int)(sizeof(b0_ported_leaf_va) / sizeof(b0_ported_leaf_va[0])))

/* 1 — тело листа портировано: его запись потока снимает claim, не запрос. */
static int b0_leaf_ported(unsigned int va)
{
    int i;

    for (i = 0; i < B0_PORTED_LEAF_COUNT; i++) {
        if (b0_ported_leaf_va[i] == va) return 1;
    }
    return 0;
}

/* Тик юнит-AI (FUN_0045eee0, R17 @0x44cbd0): единственный натив, который
 * перестраивает ленту внутри тика. С capture v13 после-состояние ленты
 * (begin/end) записано в bdd0_res ("band") и восстанавливается реплеем —
 * claim головы/длины сверяется. Старые файлы без "band" восстанавливаются
 * resync'ом из записи 45af50 (нужно для последовательности вызовов листьев;
 * claims этих тиков → SKIP, тавтологии нет). */
#define B0_VA_UNIT_AI_TICK 0x45eee0u

/* Захват без записи 0x45eee0 в `bdd0_native` (например, снятый с
 * LDHOOK_BATTLE_CAPTURE_E0=0: точка записи жила в e0a0-слое). Порт всё равно
 * вызывает натив в R17, поэтому harness подставляет запись, восстановленную из
 * независимого после-состояния `bdd0_res`: единственное поле post-state, которое
 * тело читает после вызова, — BattleAI+0x38 (= unit+0x170, `bdd0_res.u170`).
 * Значение подаётся в арену, поэтому сравнение u170 в res_match перестаёт быть
 * независимым и уходит в «несравнимо» с этой причиной (SKIP со счётом). */
#define B0_EEE0_SYNTH_WHY \
    "45eee0 post-state not captured in bdd0_native: u170 taken from bdd0_res"

/* Вход листа, меняемый ВНУТРИ тика: натив отработал — replay моделирует только
 * свои ww-записи, поэтому порт законно видит другое состояние. Такой claim не
 * сверяется: если после-состояние восстановлено из bdd0_res ("band") — leaf_value
 * сравнивает (g_band_applied); если только resync'ом из потока (старый захват) —
 * SKIP с B0_BAND_RESYNC_WHY; если не восстановлено вовсе — SKIP с причиной
 * mutator'а. Поток при этом продвинут. mutator = 0 — вход листа внутри тика не
 * меняется. Голова и длина ленты читают одну и ту же ленту — оба в списке. */
struct B0LeafMutator {
    unsigned leaf;          /* VA портированного листа */
    unsigned mutator;       /* VA натива, меняющего вход листа */
    const char *why;
};

static const B0LeafMutator b0_leaf_mutators[] = {
    /* 45eee0 (unit-AI tick, R17 @0x44cbd0) перестраивает ленту: на входе тика
     * band_count = 0, к R22 (0x44cfec) — 1. Голова и длина читают одну и ту же
     * ленту, поэтому вход меняется у обоих листьев. */
    { B0_LEAF_BAND_HEAD, B0_VA_UNIT_AI_TICK,
      "band rebuilt by 45eee0 (unit-AI tick) inside the tick" },
    { B0_LEAF_BAND_COUNT, B0_VA_UNIT_AI_TICK,
      "band rebuilt by 45eee0 (unit-AI tick) inside the tick" }
};

#define B0_LEAF_MUTATOR_COUNT \
    ((int)(sizeof(b0_leaf_mutators) / sizeof(b0_leaf_mutators[0])))

/* Claim после восстановления ленты из потока: длину берут из bdd0_leaf, поэтому
 * решение головы (NULL / не NULL) стало бы тавтологией — сверяется только
 * идентичность указателя, но и она уже проверена как часть потока. */
#define B0_BAND_RESYNC_WHY \
    "band state reconstructed from 45af50 stream (45eee0 rebuild not captured)"

/* Out-записи листа 45c6f0 восстановлены из потока: тройка `[ctx+0xc]` живёт в
 * куче игры (в снимок не попадает), replay кладёт её в арену "ctx_pos" из `w`
 * записи. Значение такой заявки производно от записи, поэтому leaf_value
 * считает эти dword'ы НЕсравнимыми с причиной (SKIP со счётом, не PASS);
 * ret/args/ecx и порядок заявок остаются реальными сверками.
 * С capture v15 тройка снимается независимо в `bdd0_in` "ctx_pos" — тогда
 * причина неприменима: заявка сверяется побитово, как обычное значение. */
#define B0_POS_RESYNC_WHY \
    "ctx triple reconstructed from the 45c6f0 record w (ctx heap object not captured)"

/* Листья, чьи out-записи восстановлены из потока (см. B0_POS_RESYNC_WHY). */
struct B0LeafWRecon {
    unsigned leaf;
    const char *why;
};

static const B0LeafWRecon b0_leaf_w_recon[] = {
    { B0_LEAF_READ_POS, B0_POS_RESYNC_WHY }
};

#define B0_LEAF_W_RECON_COUNT \
    ((int)(sizeof(b0_leaf_w_recon) / sizeof(b0_leaf_w_recon[0])))

static const char *b0_leaf_w_recon_why(unsigned int va)
{
    int i;

    for (i = 0; i < B0_LEAF_W_RECON_COUNT; i++) {
        if (b0_leaf_w_recon[i].leaf == va) return b0_leaf_w_recon[i].why;
    }
    return NULL;
}

/* Хук офлайн-харнесса (tools/probes/bdd0/native_replay.cpp): порт сообщает
 * вычисленное значение листа. ecx — получатель (this), ret — значение
 * (указатель — shadow-адрес), args/nargs — необязательные аргументы листа
 * (0 — сверять нечего).
 *
 * w/nw — dword'ы, которые лист записал ЧЕРЕЗ указатель в буфер вызывающего
 * (out-тройка 45c6f0); nw <= 3, NULL/0 — лист ничего не писал. Канал нужен
 * там, где ЗНАЧИМЫЙ результат листа — не возврат, а записанные dword'ы:
 * leaf_value сверяет их с массивом `w` записи потока (нет `w` в записи —
 * SKIP с причиной, не PASS). Листья без out-записей пользуются прежним
 * B0_LEAF_CLAIM (тот же хук, nw = 0); новый out-канал добавлять не нужно.
 *
 * В живом билде BATTLE_REPLAY не определён: вызова нет вовсе, поэтому ни
 * поведение, ни набор импортов .pyd не меняются. */
void bdd0_leaf_claim_w(unsigned int va, void *ecx, int ret, const int *args,
                       int nargs, const int *w, int nw);
/* ret — значение листа: указатель (shadow-адрес) или int; приведение через
 * size_t сохраняет биты (сравнение в leaf_value — битовое/символьное). */
#define B0_LEAF_CLAIM(va, ecx, ret, args, nargs) \
    bdd0_leaf_claim_w((unsigned int)(va), (void *)(ecx), (int)(size_t)(ret), \
                      (args), (nargs), NULL, 0)
/* Значение листа включает dword'ы, записанные через указатель (out). */
#define B0_LEAF_CLAIM_W(va, ecx, ret, args, nargs, w, nw) \
    bdd0_leaf_claim_w((unsigned int)(va), (void *)(ecx), (int)(size_t)(ret), \
                      (args), (nargs), (w), (nw))
#else
#define B0_LEAF_CLAIM(va, ecx, ret, args, nargs) ((void)0)
#define B0_LEAF_CLAIM_W(va, ecx, ret, args, nargs, w, nw) ((void)0)
#endif

#endif /* BATTLE_PORTED_H */