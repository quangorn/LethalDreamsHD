/* battle/ported.h — портированные логические листья тика и claim значения.
 *
 * Лист, чьё тело портировано, в replay-потоке больше НЕ запрашивается: его
 * запись bdd0_leaf снимает claim — порт считает значение сам и сообщает его
 * вызовом B0_LEAF_CLAIM. Replay-слой сопоставляет claim со следующей записью
 * потока (тот же seq/va/порядок) и продвигает поток; значение сверяет проверка
 * `leaf_value` (указатель — символьно, как в leaf_args), расхождение потока —
 * `leaf_seq`. Запрос такого листа (`native::…`) — нарушение механизма.
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

/* VA портированных листьев. */
#define B0_LEAF_BAND_HEAD 0x45c370u    /* begin==0 ? NULL : count ? ai+0x3c : NULL */
/* Длина ленты (FUN_0045af50): ещё НЕ портирована, поэтому replay берёт её
 * значение из записи потока (и только пока это так, после-состояние ленты
 * разрешено восстанавливать из bdd0_leaf — иначе сверка была бы тавтологией). */
#define B0_LEAF_BAND_COUNT 0x45af50u

#ifdef BATTLE_REPLAY
/* VA портированных листьев: список живёт только в replay-сборке. В живом
 * билде (BATTLE_REPLAY не определён) порт не судит запросы, поэтому статики
 * были бы мёртвыми. */
static const unsigned int b0_ported_leaf_va[] = {
    B0_LEAF_BAND_HEAD
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
 * перестраивает ленту внутри тика. */
#define B0_VA_UNIT_AI_TICK 0x45eee0u

/* Вход листа, меняемый ВНУТРИ тика: натив отработал — replay моделирует только
 * свои ww-записи, поэтому порт законно видит другое состояние. Такой claim не
 * сверяется: если после-состояние не восстановлено — leaf_value → SKIP с
 * причиной mutator'а; если восстановлено из потока (band_resync_from_stream) —
 * SKIP с B0_BAND_RESYNC_WHY (решение головы было бы тавтологией). Поток при
 * этом продвинут. mutator = 0 — вход листа внутри тика не меняется. */
struct B0LeafMutator {
    unsigned leaf;          /* VA портированного листа */
    unsigned mutator;       /* VA натива, меняющего вход листа */
    const char *why;
};

static const B0LeafMutator b0_leaf_mutators[] = {
    /* 45eee0 (unit-AI tick, R17 @0x44cbd0) перестраивает ленту: на входе тика
     * band_count = 0, к R22 (0x44cfec) — 1. */
    { B0_LEAF_BAND_HEAD, B0_VA_UNIT_AI_TICK,
      "band rebuilt by 45eee0 (unit-AI tick) inside the tick" }
};

#define B0_LEAF_MUTATOR_COUNT \
    ((int)(sizeof(b0_leaf_mutators) / sizeof(b0_leaf_mutators[0])))

/* Claim после восстановления ленты из потока: длину берут из bdd0_leaf, поэтому
 * решение головы (NULL / не NULL) стало бы тавтологией — сверяется только
 * идентичность указателя, но и она уже проверена как часть потока. */
#define B0_BAND_RESYNC_WHY \
    "band state reconstructed from 45af50 stream (45eee0 rebuild not captured)"

/* Хук офлайн-харнесса (tools/probes/bdd0/native_replay.cpp): порт сообщает
 * вычисленное значение листа. ecx — получатель (this), ret — значение
 * (указатель — shadow-адрес), args/nargs — необязательные аргументы листа
 * (0 — сверять нечего). В живом билде BATTLE_REPLAY не определён: вызова нет
 * вовсе, поэтому ни поведение, ни набор импортов .pyd не меняются. */
void bdd0_leaf_claim(unsigned int va, void *ecx, int ret, const int *args,
                     int nargs);
/* ret — значение листа: указатель (shadow-адрес) или int; приведение через
 * size_t сохраняет биты (сравнение в leaf_value — битовое/символьное). */
#define B0_LEAF_CLAIM(va, ecx, ret, args, nargs) \
    bdd0_leaf_claim((unsigned int)(va), (void *)(ecx), (int)(size_t)(ret), \
                    (args), (nargs))
#else
#define B0_LEAF_CLAIM(va, ecx, ret, args, nargs) ((void)0)
#endif

#endif /* BATTLE_PORTED_H */