/* battle/move.cpp — тик юнит-AI `FUN_0045eee0` (PLAN_BATTLE_AI_CPP.md, Фаза 4).
 *
 * Функция — не «перемещение»: за тик она обновляет ctx из rec, применяет
 * band/reset/map-clear флаги, севит search-параметры и spec-окно, берёт
 * grid-якорь и anchor-pathfind, прогоняет два поиска цели `e0a0` (filter 1 по
 * якорю, filter 2 по pos первого узла) и пост-обрабатывает: перестройка ленты,
 * сдвиг кода 1→2, порог dist². Имя — `s6_unit_ai_tick` (п. 4.3).
 *
 * Порт тела — дизасм 1.2 (все ветки, включая пост-обработку результата
 * FUN_0045e0a0 и хвост 0x45f3a5), нативные листья — по инвентарю 1.4, один
 * детур на функцию (инвариант 5) — `install_tick_hook` (4.2).
 */
#include <string.h>

#include "../ldhook_internal.h"
#include "battle.h"
#include "native.h"
#include "trace.h"

#ifdef BATTLE_REPLAY
extern "C" unsigned long replay_timer_restart_ms(void);
#endif

/* --- 4.2: детур FUN_0045eee0 и диспетчер e0a0 ---------------------------- */

volatile LONG g_s7_tick_on = 0;      /* 1 = порт s6_unit_ai_tick, 0 = трамплин */
void *g_s7_tick_cont = NULL;         /* 0x45eee5 — продолжение после 5 Б пролога */

/* Диспетчер e0a0 с НАТИВНЫМ ABI (thiscall, ECX = BattleAI, 4 стек-аргумента,
 * ret 0x10). Гейт — s6_port_enabled() (target.cpp; тот же, что у site6):
 * включён → порт s6_target_search, выключен → нативный 0x45e0a0 через
 * native::target_search (поведение = то, что сделала бы игра). Аргументы —
 * ровно те, что уже стоят в call-site порта: filter, cmp_pos, float-биты,
 * out_code. */
static int s6_e0a0_dispatch(BattleAI *self, int filter, void *cmp_pos, int arg3,
                            int *out_code)
{
    if (s6_port_enabled()) {
        return (int)(INT_PTR)s6_target_search(self, (void *)(INT_PTR)filter,
                                              cmp_pos, (void *)(INT_PTR)arg3,
                                              out_code);
    }
    return (int)(INT_PTR)native::target_search(self, filter, cmp_pos, arg3,
                                               out_code);
}

/* cdecl-обёртка порта для накед-хука: (this_, rec) → код 0..4. */
int s6_unit_ai_tick(BattleAI *self, void *rec_arg);    /* порт 4.1 */

static int s7_tick_run(BattleAI *self, void *rec_arg)
{
    return s6_unit_ai_tick(self, rec_arg);
}

/* Трамплин выключенного порта: воспроизводит украденный пролог (5 Б:
 * `sub $0x58,%esp; push %ebx; push %ebp`, 0x45eee0..0x45eee4) и переходит на
 * 0x45eee5 (`push %esi`) — нативное тело исполняется целиком, состояние
 * стека/регистров как на входе 0x45eee0. */
__declspec(naked) void s7_tick_tramp(void)
{
    __asm {
        sub  esp, 0x58
        push ebx
        push ebp
        jmp  dword ptr [g_s7_tick_cont]
    }
}

/* Хук на вход 0x45eee0 (E9 из 5 Б). ABI: __thiscall, ECX = BattleAI,
 * [esp+4] = rec, `ret $4`, EAX = код 0..4. Переключатель — g_s7_tick_on
 * (ставится install_tick_hook при LDHOOK_SITE6_EEE0=1, дефолт гейта 0): 1 → порт s6_unit_ai_tick, 0 → трамплин с нативным телом
 * (хук в этом режиме не активен). Ре-энтри гарда нет — как у site6 (полная
 * замена, тело натива не исполняется). */
__declspec(naked) void s7_tick_hook(void)
{
    __asm {
        cmp  dword ptr [g_s7_tick_on], 0
        je   s7_tick_off
        push eax                       /* esp: E-4 */
        push edx                       /* E-8 */
        push ecx                       /* E-12: [esp+0x10] = rec (E+4) */
        push dword ptr [esp + 16]      /* arg2: rec */
        push ecx                       /* arg1: this */
        call s7_tick_run               /* cdecl, результат в eax */
        add  esp, 8                    /* снять this и rec */
        pop  ecx
        pop  edx
        add  esp, 4                    /* пропустить сохранённый eax — результат уже в eax */
        ret  4                         /* нативный хвост: thiscall, 1 стек-аргумент */
    s7_tick_off:
        jmp  s7_tick_tramp             /* esp = E, ecx = this — как на входе */
    }
}

/* float ↔ int-биты слота (x87-операнды `FLDS/FSTPS [ESP+off]` над int-слотами
 * кадра; паттерн path.cpp). */
static void s6_bits_set(int *slot, float f)
{
    memcpy(slot, &f, sizeof(int));
}

static float s6_bits_get(int slot)
{
    float f;

    memcpy(&f, &slot, sizeof(float));
    return f;
}

/* FUN_0045c330 (0x45c330..0x45c364) — сброс AI-состояния, тонкий порт (инвентарь
 * §5: «порт (тривиальный)»; §8.2 — этот файл). __thiscall, ECX = BattleAI,
 * 0 стек-аргументов (`RET` @0x45c364). Порядок записей = листинг: +0x38, +0x04,
 * +0x08, +0x10, +0x14, +0x0c (все dword = EBX = 0, `xor %ebx,%ebx` @0x45c334),
 * SetRectEmpty(&this+0x18) (@0x45c34c; lea/push — @0x45c336/0x45c339), затем
 * +0x28, +0x2c (dword 0), байт +0x30 (`mov %bl,0x30(%esi)` @0x45c358) и
 * `+0x34 = 0x0098967f` (@0x45c35b). Поле +0x00 (game_ctx) не трогается.
 * Единственный нативный лист — IAT SetRectEmpty (native::set_rect_empty). */
void s6_ai_reset(BattleAI *self)
{
    unsigned char *p = (unsigned char *)self;

    *(int *)(p + 0x38) = 0;                            /* @0x45c33a mov %ebx,0x38(%esi) */
    *(int *)(p + 0x04) = 0;                            /* @0x45c33d mov %ebx,0x4(%esi) */
    *(int *)(p + 0x08) = 0;                            /* @0x45c340 mov %ebx,0x8(%esi) */
    *(int *)(p + 0x10) = 0;                            /* @0x45c343 mov %ebx,0x10(%esi) */
    *(int *)(p + 0x14) = 0;                            /* @0x45c346 mov %ebx,0x14(%esi) */
    *(int *)(p + 0x0c) = 0;                            /* @0x45c349 mov %ebx,0xc(%esi) */
    native::set_rect_empty(p + 0x18);                  /* @0x45c34c call *0x4ec268 (&this+0x18) */
    *(int *)(p + 0x28) = 0;                            /* @0x45c352 mov %ebx,0x28(%esi) */
    *(int *)(p + 0x2c) = 0;                            /* @0x45c355 mov %ebx,0x2c(%esi) */
    *(unsigned char *)(p + 0x30) = 0;                  /* @0x45c358 mov %bl,0x30(%esi) */
    *(unsigned int *)(p + 0x34) = 0x0098967fu;         /* @0x45c35b movl $0x98967f,0x34(%esi) */
}

/* FUN_0045c570 (0x45c570..0x45c597) — рестарт игрового таймера AI, тонкий порт
 * (инвентарь §5: «порт» + нативный лист timeGetTime). __thiscall, ECX = BattleAI,
 * 0 стек-аргументов (`ret` @0x45c597). Схема: `call *0x4ec27c` (@0x45c574,
 * timeGetTime) → `test eax,eax` (@0x45c57a) → `fildl` (@0x45c580; <0 —
 * `faddl [0x4ecd10]` = 2^32 @0x45c586) → `fmull [0x4ecd08]` = 0.001
 * (@0x45c58c) → `fstps 0x4(%esi)` (@0x45c592): this+0x04 = секунды. Цепочка в
 * 80-битном x87 без промежуточного double — инлайн-асм (как s6_check_timeout). */
static const double s6_tr_two32 = 4294967296.0;   /* 0x4ecd10 */
static const double s6_tr_milli = 0.001;          /* 0x4ecd08 */

static void s6_timer_restart(BattleAI *self)
{
#ifdef BATTLE_REPLAY
    int ms = (int)replay_timer_restart_ms();/* @0x45c574 — не двигает курсор leaves_timer */
#else
    int ms = (int)s6_time_ms();                    /* @0x45c574 call *0x4ec27c */
#endif

    __asm {
        fild    dword ptr [ms]                     /* @0x45c580 fildl 0x4(%esp) — знаковое */
        mov     eax, [ms]
        test    eax, eax
        jge     short s6_tr_nofix                  /* @0x45c584 jge 0x45c58c */
        fadd    qword ptr [s6_tr_two32]            /* @0x45c586 faddl 0x4ecd10 = 2^32 */
    s6_tr_nofix:
        fmul    qword ptr [s6_tr_milli]            /* @0x45c58c fmull 0x4ecd08 = 0.001 */
        mov     eax, [self]
        fstp    dword ptr [eax + 4]                /* @0x45c592 fstps 0x4(%esi) — this+0x04 */
    }
}

/* FUN_0045eee0 = `s6_unit_ai_tick` — тик юнит-AI (__thiscall, ECX = BattleAI*,
 * 1 стек-аргумент rec, RET 4). Кадр: F = entry−104 (sub $0x58 + push
 * ebx/ebp/esi/edi @0x45eee0..0x45eeee); слоты F+k моделируются массивом dword
 * по паттерну LS(k) из path.cpp. rec — стек-аргумент [entry+4] = слот F+0x6c.
 *
 * 4.1a = B0 (ленивый ctx через игровой operator new), B1 (6 dword-копий
 * rec→ctx), B2 (аллокация sub-объекта ctx[0xc] + 4-dword копия из rec[0xc]).
 * 4.1b = B3 (x87-гейт world[0x120] → AND флагов ctx, биты 1/5), B4 (бит6 →
 * band_free/native_45af10 по &self[0x3c]), B5 (бит7 → s6_ai_reset/clear_maps).
 * Регистр-носитель нуля: EBX = 0 с @0x45eeea до B16 (перечитывается @0x45f3da).
 *
 * 4.1c — B6/B7/B8, 4.1d — B9, 4.1e — B10, 4.1f — B11/B12, 4.1g — B13/B14,
 * 4.1h — B15/B16/B17 и три эпилога (0x45f36f..0x45f427). Сигнатура нативная
 * (int, один стек-аргумент, `ret $4`), возврат = код 0..4 как в EAX эпилогов
 * 0x45f382/0x45f413/0x45f425. Тело — под harness-__try (§8.1 п.9, паттерн
 * s6_ai_update). Детур и wiring — `install_tick_hook` (ldhook.cpp, 4.2). */
int s6_unit_ai_tick(BattleAI *self, void *rec_arg)
{
    int l[34];                       /* F−0x18..F+0x6c: index 0 = слот F−0x18 (temp B9) */
    ld_crash_enter(1);               /* LDHOOK_CRASHLOG: stage 1, depth++ (no-op off) */
    unsigned char *ctx;
    const unsigned char *rec;
    void *ebp;                       /* EBP кадра: PathState/узел B10 → B14 (dst copy, [ebp]->pos) */
    int cand;                        /* EDI кадра: кандидат B11 → B13/B14 (node1), затем node2 */

#define LS(k) (l[((k) + 0x18) >> 2])
#define LS_BYTES(k) ((unsigned char *)(l + (((k) + 0x18) >> 2)))

    LS(0x6c) = (int)(INT_PTR)rec_arg;   /* rec = [entry+4] = слот F+0x6c */

    /* Тело — под harness-__try по плану §8.1 п.9 (рамка 0x44bdd0 в игре;
     * паттерн s6_ai_update: EXCEPTION_EXECUTE_HANDLER → возврат 0). Границы
     * __try отмечены на нулевом отступе, текст блоков не переиндентирован. */
    __try {

    /* B0 — ленивая аллокация ctx (0x1c байт) игровым operator new.
     * EBX = 0 (@0x45eeea) — носитель нуля для записей ниже. */
    if (self->game_ctx == NULL) {                        /* @0x45eee8/0x45eeec/0x45eeef */
        ctx = (unsigned char *)native::operator_new(0x1c);   /* @0x45eef1/0x45eef3; освобождение — dtor, ещё не перенесён */
        if (ctx != NULL) {                               /* @0x45eefb/0x45eefd je 0x45ef0d */
            *(int *)(ctx + 0x04) = 0;                    /* @0x45eeff */
            *(int *)(ctx + 0x08) = 0;                    /* @0x45ef02 */
            *(int *)(ctx + 0x0c) = 0;                    /* @0x45ef05 */
            *(int *)(ctx + 0x10) = 0;                    /* @0x45ef08 */
        } else {
            ctx = NULL;                                  /* @0x45ef0d xor %eax,%eax */
        }
        self->game_ctx = ctx;                            /* @0x45ef0f mov %eax,(%esi) — обе ветви */
    }

    /* B1 — шесть dword-копий rec → ctx; порядок листинга:
     * +0x00, +0x14, +0x18, +0x08, +0x10, +0x04. */
    rec = (const unsigned char *)(INT_PTR)LS(0x6c);      /* @0x45ef11 mov 0x6c(%esp),%edi */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef15 */
    *(int *)(ctx + 0x00) = *(const int *)(rec + 0x00);   /* @0x45ef17/0x45ef19 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef1b */
    *(int *)(ctx + 0x14) = *(const int *)(rec + 0x14);   /* @0x45ef1d/0x45ef20 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef23 */
    *(int *)(ctx + 0x18) = *(const int *)(rec + 0x18);   /* @0x45ef25/0x45ef28 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef2b */
    *(int *)(ctx + 0x08) = *(const int *)(rec + 0x08);   /* @0x45ef2d/0x45ef30 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef33 */
    *(int *)(ctx + 0x10) = *(const int *)(rec + 0x10);   /* @0x45ef35/0x45ef38 */

    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef3b */
    *(int *)(ctx + 0x04) = *(const int *)(rec + 0x04);   /* @0x45ef3d/0x45ef40 */

    /* B2 — sub-объект ctx[0xc] (new 0x10) + 4-dword копия из объекта rec[0xc]. */
    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef43 */
    if (*(void **)(ctx + 0x0c) == NULL) {                /* @0x45ef45/0x45ef48 jne 0x45ef64 */
        void *sub = native::operator_new(0x10);          /* @0x45ef4a/0x45ef4c; освобождение — dtor, ещё не перенесён */
        if (sub != NULL) {                               /* @0x45ef54/0x45ef56 je 0x45ef5d */
            *(int *)((unsigned char *)sub + 0x0c) = 0;   /* @0x45ef58 */
        }
        ctx = (unsigned char *)self->game_ctx;           /* @0x45ef5f */
        *(void **)(ctx + 0x0c) = sub;                    /* @0x45ef61 mov %eax,0xc(%ecx) */
    }
    /* Копия выполняется без проверки dst на NULL — как в оригинале
     * (new 0x10 вернул NULL → разыменование @0x45ef6e, AV). */
    ctx = (unsigned char *)self->game_ctx;               /* @0x45ef64 */
    {
        unsigned char *dst = (unsigned char *)*(void **)(ctx + 0x0c);   /* @0x45ef66 */
        const unsigned char *src =
            (const unsigned char *)*(const void **)(rec + 0x0c);        /* @0x45ef69 */
        *(int *)(dst + 0x00) = *(const int *)(src + 0x00);  /* @0x45ef6c/0x45ef6e */
        *(int *)(dst + 0x04) = *(const int *)(src + 0x04);  /* @0x45ef70/0x45ef73 */
        *(int *)(dst + 0x08) = *(const int *)(src + 0x08);  /* @0x45ef76/0x45ef79 */
        *(int *)(dst + 0x0c) = *(const int *)(src + 0x0c);  /* @0x45ef7c/0x45ef7f */
    }

    /* B3 (0x45ef82..0x45efa4) — x87-гейт масштаба мира → сброс одного из битов
     * флагов ctx. Гейт: FLDS world[0x120] (@0x45ef85; world = *(rec+4)
     * @0x45ef82 — читается из rec, не из ctx), FCOMPS 0.0f [0x4ec37c]
     * @0x45ef8b, FNSTSW %ax @0x45ef91, TEST $0x41,%ah @0x45ef93 (C3|C0 статуса
     * x87). `jne`@0x45ef9a взят ⟺ test != 0 ⟺ NOT(ST строго > 0) — т.е. меньше,
     * равно или unordered(NaN); тот же гейт `JNZ(маска 0x41)`, что в path.cpp
     * (п. 1.1.7): строгое `>` для fall-through, всё прочее — взятие.
     * Записи: fall-through `&= ~0x02` (bit 1, `and $0xfffffffd` @0x45ef9c);
     * взятие `&= ~0x20` (bit 5, `and $0xffffffdf` @0x45efa1); одна запись
     * @0x45efa4. Флаги читаются в ECX до ветвления (@0x45ef96/0x45ef98). */
    {
        const unsigned char *world =
            (const unsigned char *)*(const void **)(rec + 0x04);   /* @0x45ef82 mov 0x4(%edi),%ecx */
        float world_scale;
        int flags;

        memcpy(&world_scale, world + 0x120, sizeof(float));  /* @0x45ef85 flds 0x120(%ecx) */
        ctx = (unsigned char *)self->game_ctx;               /* @0x45ef96 mov (%esi),%eax */
        flags = *(int *)(ctx + 0x00);                        /* @0x45ef98 mov (%eax),%ecx */
        if (world_scale > 0.0f) {                            /* @0x45ef8b/0x45ef91/0x45ef93, jne @0x45ef9a NOT taken */
            flags &= ~0x02;                                  /* @0x45ef9c and $0xfffffffd (fall-through) */
        } else {
            flags &= ~0x20;                                  /* @0x45efa1 and $0xffffffdf (jne взят) */
        }
        *(int *)(ctx + 0x00) = flags;                        /* @0x45efa4 mov %ecx,(%eax) */
    }

    /* B4 (0x45efa6..0x45efc1) — бит 6 флагов ctx: чистка ленты по &self[0x3c]
     * (EDI = `lea 0x3c(%esi)` @0x45efb1 — переиспользуется как this двух
     * thiscall-вызовов). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efa6 mov (%esi),%edx */
    if (((*(const unsigned int *)ctx >> 6) & 1) != 0) {      /* @0x45efa8/0x45efaa/0x45efad/0x45efaf je 0x45efc2 */
        void *band = (void *)((unsigned char *)self + 0x3c); /* @0x45efb1 lea 0x3c(%esi),%edi */

        native::band_free(band);                             /* @0x45efb4 mov %edi,%ecx / @0x45efb6 call 0x45b6a0 */
        native::native_45af10(band);                         /* @0x45efbb mov %edi,%ecx / @0x45efbd call 0x45af10 */
    }

    /* B5 (0x45efc2..0x45efdb) — бит 7 флагов ctx: сброс AI-состояния (тонкий порт
     * s6_ai_reset) и очистка карт (ECX = self в обоих вызовах: `mov %esi,%ecx`
     * @0x45efce/0x45efd5). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efc2 mov (%esi),%ecx */
    if (((*(const unsigned int *)ctx >> 7) & 1) != 0) {      /* @0x45efc4/0x45efc6/0x45efc9/0x45efcc je 0x45efdc */
        s6_ai_reset(self);                                   /* @0x45efce mov %esi,%ecx / @0x45efd0 call 0x45c330 */
        native::clear_maps(self);                            /* @0x45efd5 mov %esi,%ecx / @0x45efd7 call 0x45ca70 */
    }

    /* B6 (0x45efdc..0x45f065) — масштабы таймаута S(0x10)/S(0x18)/S(0x1c) и
     * ветка кодов {1,2,4}. ctx перечитывается; EDI = 32-бит флаги ctx
     * (@0x45efde), EAX = ctx[0x14] как int-биты (@0x45efe0). Слоты сеются в
     * порядке листинга: S(0x1c)=0, S(0x10)=ctx[0x14], S(0x18)=0; бит 4 →
     * x87-перезапись S(0x1c)/S(0x10). */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45efdc mov (%esi),%ecx */
    {
        unsigned int flags = *(const unsigned int *)ctx;     /* @0x45efde mov (%ecx),%edi */
        int ctx14 = *(const int *)(ctx + 0x14);              /* @0x45efe0 mov 0x14(%ecx),%eax */
        int code;                                            /* EAX ветки кодов (@0x45f020) */

        LS(0x1c) = 0;                                        /* @0x45efeb movl $0x0,0x1c(%esp) */
        LS(0x10) = ctx14;                                    /* @0x45eff3 mov %eax,0x10(%esp) */
        LS(0x18) = 0;                                        /* @0x45eff7 movl $0x0,0x18(%esp) */
        if (((flags >> 4) & 1) != 0) {                       /* @0x45efe3/0x45efe5/0x45efe8/0x45efff je 0x45f019 */
            float c14 = s6_bits_get(ctx14);                  /* @0x45f001 flds 0x14(%ecx) */
            float s1c = c14 * 0.0f;                          /* @0x45f004 fmuls [0x4ec37c] = 0.0f */
            s6_bits_set(&LS(0x1c), s1c);                     /* @0x45f00a fstps 0x1c(%esp) */
            s6_bits_set(&LS(0x10), c14 - s1c);               /* @0x45f00e flds 0x14 / @0x45f011 fsubs 0x1c / @0x45f015 fstps 0x10 */
        }
        LS(0x14) = (int)(INT_PTR)((unsigned char *)self + 0x38);  /* @0x45f019 lea 0x38(%esi),%eax / @0x45f01c mov %eax,0x14(%esp) */
        code = *(const int *)((unsigned char *)self + 0x38); /* @0x45f020 mov (%eax),%eax — код выхода e0a0 */
        if (code == 1 || code == 2 || code == 4) {           /* @0x45f022/0x45f025/0x45f027/0x45f02a/0x45f02c/0x45f02f je 0x45f056 */
            LS(0x18) = LS(0x10);                             /* @0x45f056 mov 0x10(%esp),%edx / @0x45f05a mov %edx,0x18(%esp) */
            LS(0x10) = 0;                                    /* @0x45f05e movl $0x0,0x10(%esp) */
        } else if (((flags >> 7) & 1) == 0) {                /* @0x45f031/0x45f033/0x45f036/0x45f038 jne 0x45f066 */
            float s10 = s6_bits_get(LS(0x10));               /* @0x45f03a/0x45f03e flds 0x10(%esp) ×2 (ST1 — копия старого) */
            float p = s10 * 0.6f;                            /* @0x45f042 fmuls [0x4ef4fc] = 0.6f */
            s6_bits_set(&LS(0x10), p);                       /* @0x45f048 fstps 0x10(%esp) */
            s6_bits_set(&LS(0x18), s10 - p);                 /* @0x45f04c fsubs 0x10(%esp) — старый S10 − новый / @0x45f050 fstps 0x18 */
        }
    }

    /* B7 (0x45f066..0x45f0c3) — grid-root `this+0x28` и посев 13-dword spec-окна
     * с базой F+0x34 (= spec+0x00..+0x30). Источник — ctx[0x10] (ECX здесь = ctx
     * с @0x45efdc, не world), запись @0x45f0b6 безусловная, fallback при нуле. */
    {
        int grid = *(const int *)(ctx + 0x10);               /* @0x45f066 mov 0x10(%ecx),%eax */
        unsigned char *dst = (unsigned char *)self + 0x28;   /* @0x45f06b lea 0x28(%esi),%edi */

        LS(0x34) = 0;                                        /* @0x45f06e spec+0x00 */
        LS(0x38) = 0;                                        /* @0x45f072 spec+0x04 */
        LS(0x3c) = 0;                                        /* @0x45f076 spec+0x08 */
        LS(0x40) = 0;                                        /* @0x45f07a spec+0x0c */
        LS(0x44) = 0;                                        /* @0x45f082 spec+0x10 */
        LS(0x48) = 0;                                        /* @0x45f08a spec+0x14 */
        LS(0x4c) = 0;                                        /* @0x45f092 spec+0x18 */
        LS(0x50) = 0;                                        /* @0x45f096 spec+0x1c */
        LS(0x54) = 0;                                        /* @0x45f09a spec+0x20 */
        LS(0x58) = 0;                                        /* @0x45f09e spec+0x24 */
        LS(0x5c) = (int)0xcb18967fu;                         /* @0x45f0a2 spec+0x28 = −1e7f */
        LS(0x60) = 0x4b18967f;                               /* @0x45f0aa spec+0x2c = +1e7f */
        LS(0x64) = 0;                                        /* @0x45f0b2 spec+0x30 */
        *(int *)dst = grid;                                  /* @0x45f0b6 mov %eax,(%edi) — безусловно */
        if (grid == 0) {                                     /* @0x45f069 cmp %ebx,%eax / @0x45f0b8 jne 0x45f0c5 */
            const unsigned char *world =
                (const unsigned char *)*(const void **)(ctx + 0x04);   /* @0x45f0ba mov 0x4(%ecx),%eax */
            *(int *)dst = *(const int *)(world + 0x134);     /* @0x45f0bd mov 0x134(%eax),%ecx / @0x45f0c3 mov %ecx,(%edi) */
        }
    }

    native::search_params_update(self);                      /* @0x45f0c5 mov %esi,%ecx / @0x45f0c7 call 0x45c8c0 */

    /* B9 (0x45f0cc..0x45f13a) — гейт bit7, grid-якорь 458050 и pathfind 45cb40.
     * Кадр здесь опускается до F−0x18: 16-байтовый temp — слоты F−0x18..F−8
     * (= l[0..5] после re-base), затем LS(0x20) = &temp. */
    ctx = (unsigned char *)self->game_ctx;                   /* @0x45f0cc mov (%esi),%eax */
    if (((*(const unsigned int *)ctx >> 7) & 1) != 0) {      /* @0x45f0ce/0x45f0d0/0x45f0d3/0x45f0d6 je 0x45f13d (бит7=0 → B10) */
        const unsigned char *sub;
        void *anchor;
        void *path;
        int metric;

        *LS_BYTES(0x6c) = 0;                                 /* @0x45f0d8 mov %bl,0x6c(%esp) — БАЙТ rec-слота */
        sub = (const unsigned char *)*(const void **)(ctx + 0x0c);   /* @0x45f0df mov 0xc(%eax),%eax */
        /* 16-байтовый temp @F−0x18 = копия 4 dword объекта ctx[0xc] (sub-объект B2). */
        *(int *)(LS_BYTES(-0x18) + 0x00) = *(const int *)(sub + 0x00);  /* @0x45f0e8/0x45f0ef */
        *(int *)(LS_BYTES(-0x18) + 0x04) = *(const int *)(sub + 0x04);  /* @0x45f0f1/0x45f0f4 */
        *(int *)(LS_BYTES(-0x18) + 0x08) = *(const int *)(sub + 0x08);  /* @0x45f0f7/0x45f0fa */
        *(int *)(LS_BYTES(-0x18) + 0x0c) = *(const int *)(sub + 0x0c);  /* @0x45f0fd/0x45f100 */
        LS(0x20) = (int)(INT_PTR)(void *)LS_BYTES(-0x18);    /* @0x45f108 mov %esp,0x38(%esp) (esp=F−0x18 → F+0x20) */
        /* arg6 = ctx[4] (world, @0x45f0dc/0x45f0e2), arg5 = &LS(0x6c)
         * (@0x45f0e3/0x45f0e7 — callee пишет байт-флаг через этот слот,
         * @0x4580fd/0x458161), arg1..arg4 = temp dword 0..3; ECX = ctx[8]
         * (@0x45f103/0x45f105). Ret $0x18. */
        anchor = native::grid_anchor(
            *(void **)(ctx + 0x08),                          /* ECX = ctx[8] */
            *(const int *)(LS_BYTES(-0x18) + 0x00),          /* @0x45f10c arg1 */
            *(const int *)(LS_BYTES(-0x18) + 0x04),          /* arg2 */
            *(const int *)(LS_BYTES(-0x18) + 0x08),          /* arg3 */
            *(const int *)(LS_BYTES(-0x18) + 0x0c),          /* arg4 (callee использует как указатель) */
            &LS(0x6c),                                       /* arg5 = &rec-слота */
            *(void **)(ctx + 0x04));                         /* arg6 = world */
        self->anchor = (LDPos *)anchor;                      /* @0x45f113 mov %eax,0x2c(%esi) */
        if (anchor == NULL) {                                /* @0x45f111 cmp %ebx,%eax / @0x45f116 je 0x45f379 */
            LD_CRASH_RET(0);                                 /* эпилог 0x45f379..0x45f382: EAX=0, ret $4 */
        }
        metric = *(const int *)((const unsigned char *)anchor + 0x14);  /* @0x45f11c mov 0x14(%eax),%edx */
        LS(0x34) = (int)(INT_PTR)anchor;                     /* @0x45f11f mov %eax,0x34(%esp) — spec+0x00 */
        LS(0x44) = metric;                                   /* @0x45f12b mov %edx,0x4c(%esp) — spec+0x10 = anchor[0x14] */
        /* 45cb40 = s6_path_search: ECX = self (@0x45f129), arg1 = &spec
         * (@0x45f124/0x45f128), arg2 = 0/EBX (@0x45f123); порт имеет 2 параметра:
         * этот слот callee сам перезаписывает как out-скретч lower_bound
         * (`lea 0x38(esp)`@0x45cc0b, store `mov %ecx,0x38(esp)`@0x45cc07) и
         * значение не читает — поэтому нуль вызывающего не передаётся. Ret 8. */
        path = s6_path_search(self, (PathState *)LS_BYTES(0x34));  /* @0x45f12f call 0x45cb40 */
        /* @0x45f134 neg (CF=1 при eax≠0) / @0x45f136 sbb al,al (CF→0xff) /
         * @0x45f138 inc al (0xff→0x00; при eax=0: 0x00→0x01) — итог 1 ⟺ path == NULL. */
        *(unsigned char *)((unsigned char *)self + 0x30) = (unsigned char)(path == NULL);  /* @0x45f13a mov %al,0x30(%esi) */
    }

    /* B10 (0x45f13d..0x45f21a) — lower_bound #1 по карте this+0x68 + создание
     * PathState (ctor 45c2e0) либо выбор существующего узла, затем сборка
     * spec-окна. Слоты lower_bound: out = &F+0x6c (второй push @0x45f145),
     * key = &this+0x28 (первый push @0x45f13d; EDI = `lea 0x28(%esi)` @0x45f06b).
     * В 0x434bc0 ключ читается из arg2 (`mov 0x18(%esp),%edi`@0x434bcf, key
     * `mov (%edi),%eax`@0x434bfd, store @0x434bf7), result пишется в arg1
     * (`mov 0x14(%esp),%eax`@0x434c0a, `mov %ecx,(%eax)`@0x434c10). Карта §8.1
     * п.4 описывает это наоборот — листинг главнее. grid-root = *(this+0x28)
     * (запись B7 @0x45f0b6) остаётся нетронутым: он — pos спеки. EBP —
     * PathState/узел, его читает B11 (0x45f21d). */
    {
        int node;

        native::lower_bound_slots(&self->map1,               /* @0x45f142 ecx = lea 0x68(%esi) */
                                  &LS(0x6c),                 /* @0x45f13e lea 0x70(%esp) / @0x45f145 push — out */
                                  (int *)((unsigned char *)self + 0x28));  /* @0x45f13d push %edi = &this+0x28 — key */
        node = LS(0x6c);                                     /* @0x45f14b mov (%eax),%eax — eax = out-указатель */
        if (node == (int)(INT_PTR)self->cand_head) {         /* @0x45f14d cmp 0x6c(%esi) / @0x45f150 jne 0x45f21a */
            PathState *pfs;
            void *world;
            int grid;
            float ws, g14;
            int dist2, h, h10000, cost;

            pfs = s6_pfs_ctor();                             /* @0x45f156/0x45f158 call 0x45c2e0 (ECX = self телом не читается) */
            ctx = (unsigned char *)self->game_ctx;           /* @0x45f15d mov (%esi),%ecx */
            world = *(void **)(ctx + 0x04);                  /* @0x45f15f mov 0x4(%ecx),%ecx — world */
            ebp = pfs;                                       /* @0x45f164 mov %eax,%ebp */
            grid = *(const int *)((unsigned char *)self + 0x28);  /* @0x45f166 mov (%edi),%eax — grid-root */
            LS(0x34) = grid;                                 /* @0x45f168 mov %eax,0x34(%esp) — spec+0x00 = grid-root */
            LS(0x38) = 0;                                    /* @0x45f171 mov %ebx,0x3c(%esp) — spec+0x04 parent */
            LS(0x3c) = 1;                                    /* @0x45f175 movl $1,0x40(%esp) — spec+0x08 filter */
            /* world vtable, слот +0x38, 1 арг = &S(0x28) (buf world-notify, 3 dword:
             * S(0x28..0x30) заполняет callee; писателя в листинге нет). Цель
             * статически не разрешена — вызов через тот же слот (п. 1.5/§8.1). */
            native::vcall1(world, 0x38, (int)(INT_PTR)&LS(0x28));  /* @0x45f162 mov (%ecx),%edx / @0x45f16c lea 0x28(%esp) / @0x45f170 push / @0x45f17d call *0x38(%edx) */
            s6_grid_scale_compute((LDPos *)(INT_PTR)grid,    /* @0x45f185 mov (%edi),%ecx — receiver = grid-root */
                                  (float *)LS_BYTES(0x20));  /* @0x45f180 lea 0x20(%esp) / @0x45f184 push / @0x45f187 call 0x456b20 */
            /* spec+0x0c = S(0x28) − S(0x20) (дельта масштаба). */
            s6_bits_set(&LS(0x40), s6_bits_get(LS(0x28)) - s6_bits_get(LS(0x20)));  /* @0x45f18c flds 0x28 / @0x45f190 fsubs 0x20 / @0x45f19e fstps 0x40 */
            /* spec+0x10 = world[0x120] + grid-root[0x14] (out_metric). */
            memcpy(&ws, (const unsigned char *)world + 0x120, sizeof(float));  /* @0x45f1a2 flds 0x120(%eax) */
            memcpy(&g14, (const unsigned char *)(INT_PTR)grid + 0x14, sizeof(float));  /* @0x45f199 mov (%edi),%edi / @0x45f1a9 fadds 0x14(%edi) */
            s6_bits_set(&LS(0x44), ws + g14);                /* @0x45f1af fstps 0x4c(%esp) */
            LS(0x4c) = 0;                                    /* @0x45f1b7 mov %ebx,0x54(%esp) — spec+0x18 g_cost */
            /* spec+0x14 = S(0x30) − S(0x24). */
            s6_bits_set(&LS(0x48), s6_bits_get(LS(0x30)) - s6_bits_get(LS(0x24)));  /* @0x45f1b3 flds 0x38 / @0x45f1bb fsubs 0x2c / @0x45f1bf fstps 0x50 */
            /* dist² = c480(grid-root, anchor): arg1 — последний push (%edi @0x45f1ac),
             * arg2 — anchor (@0x45f1a8). */
            dist2 = s6_distance2((void *)(INT_PTR)grid,
                                 (void *)(INT_PTR)self->anchor);  /* @0x45f1a8/0x45f1ac/0x45f1c3 */
            LS(0x50) = dist2;                                /* @0x45f1cc mov %eax,%edi / @0x45f1d1 mov %edi,0x54(%esp) — spec+0x1c */
            h = s6_heuristic_gate(self, (int *)LS_BYTES(0x34));  /* @0x45f1c8 lea 0x34(%esp) / @0x45f1ce push / @0x45f1d5 call 0x45c690 — arg = &spec */
            h10000 = h * 10000;                              /* @0x45f1da imul $0x2710,%eax,%eax */
            LS(0x54) = h10000;                               /* @0x45f1e0 mov %eax,0x54(%esp) — spec+0x20 */
            cost = h10000 + dist2;                           /* @0x45f1e4 add %edi,%eax */
            LS(0x58) = cost;                                 /* @0x45f1eb mov %eax,0x5c(%esp) — spec+0x24 */
            LS(0x5c) = (int)0xcb18967fu;                     /* @0x45f1f7 — spec+0x28 = −1e7f */
            LS(0x60) = 0x4b18967f;                           /* @0x45f1ff — spec+0x2c = +1e7f */
            LS(0x64) = 0;                                    /* @0x45f207 — spec+0x30 */
            s6_copy_fields((int)(INT_PTR)pfs,                /* @0x45f1ef push %ebp — dst (порт: 1-й арг) */
                           (int)(INT_PTR)LS_BYTES(0x34),     /* @0x45f1f0 lea 0x3c / @0x45f1f4 push — src = &spec */
                           0x1fff);                          /* @0x45f1e6 push $0x1fff / @0x45f20b call 0x45c4b0 */
            native::register_occupied_slot(self, (LDObject *)ebp);  /* @0x45f210 push %ebp / @0x45f213 call 0x45dd00 */
            /* @0x45f218 jmp 0x45f21d — слияние с веткой существующего узла (B11). */
        } else {
            ebp = *(void **)((unsigned char *)(INT_PTR)node + 0x10);  /* @0x45f21a mov 0x10(%eax),%ebp — PathState* узла */
        }
        (void)ebp;   /* регистр живёт до B11 (0x45f21d) — трансляция продолжается в 4.1f */
    }

    /* B11 (0x45f21d..0x45f269) — lower_bound #2 и поиск #1 e0a0(filter=1).
     * Слоты #2 (0x434bc0, та же конвенция, что в B10): первый push %eax =
     * `lea 0x2c(%esi)` @0x45f21d/0x45f220 = &self->anchor — KEY; второй push
     * %ecx = `lea 0x70(%esp)` @0x45f221/0x45f225 = &F+0x6c — OUT. EAX на выходе
     * = out-указатель, поэтому node = *(out) = LS(0x6c) (`mov (%eax),%eax`
     * @0x45f230). EDI = кандидат: 0 или node->[0x10]. */
    {
        int node2;
        int *code_ptr;

        cand = 0;                                            /* @0x45f229 xor %edi,%edi — до call */
        native::lower_bound_slots(&self->map1,               /* @0x45f226 ecx = lea 0x68(%esi) */
                                  &LS(0x6c),                 /* @0x45f221 lea 0x70(%esp) / @0x45f225 push — out */
                                  (int *)((unsigned char *)self + 0x2c));  /* @0x45f21d lea 0x2c(%esi) / @0x45f220 push — key = &self->anchor */
        node2 = LS(0x6c);                                    /* @0x45f230 mov (%eax),%eax — eax = out-указатель */
        if (node2 != (int)(INT_PTR)self->cand_head) {         /* @0x45f232 cmp 0x6c(%esi) / @0x45f235 je 0x45f23a */
            cand = *(const int *)((unsigned char *)(INT_PTR)node2 + 0x10);  /* @0x45f237 mov 0x10(%eax),%edi */
        }
        /* Гейт `flds S(0x10); fcomps 0.0f; fnstsw; test $0x44,%ah; jnp`:
         * маска C3|C2 — jnp взят ⟺ C3≠C2 ⟺ равно (C3=1,C2=0) → пропуск поиска;
         * иначе (больше/меньше/unordered: C3=C2) поиск идёт. Итог: skip ⟺
         * bits == 0.0f, включая NaN как «не равно» → вызов. */
        if (s6_bits_get(LS(0x10)) != 0.0f) {                  /* @0x45f23a flds 0x10 / @0x45f23e fcomps [0x4ec37c] / @0x45f246 test $0x44 / @0x45f249 jnp 0x45f26b */
            s6_timer_restart(self);                           /* @0x45f24b/0x45f24d call 0x45c570 — ДО e0a0 */
            cand = s6_e0a0_dispatch(
                self,                                         /* @0x45f262 mov %esi,%ecx */
                1,                                            /* @0x45f260 push $1 — arg1 filter */
                self->anchor,                                 /* @0x45f25a mov 0x2c(%esi),%ecx / @0x45f25f push — arg2 cmp_pos */
                LS(0x10),                                     /* @0x45f256 mov 0x10(%esp),%eax / @0x45f25e push — arg3 float bits */
                (int *)(INT_PTR)LS(0x14));                    /* @0x45f252 mov 0x14(%esp),%edx / @0x45f25d push — arg4 out_code = &self+0x38 */
        }                                                     /* @0x45f269 mov %eax,%edi */
        /* B12 (0x45f26b..0x45f292) — код == 1 → эвристика 45c5e0 по spec-окну.
         * Если гейт выше пропустил поиск, cand остаётся 0 и разыменования
         * `0x10(%edi)`/`(%edi)` здесь падают так же, как в нативе; SEH-рамку
         * функции (`__try` по плану §8.1 п.9) добавляет конец 4.1, здесь её нет. */
        code_ptr = (int *)(INT_PTR)LS(0x14);                  /* @0x45f26b mov 0x14(%esp),%edx — &self+0x38 */
        if (*code_ptr == 1) {                                 /* @0x45f26f cmpl $1,(%edx) / @0x45f272 jne 0x45f295 */
            int hres;

            LS(0x44) = *(const int *)((const unsigned char *)(INT_PTR)cand + 0x10);  /* @0x45f274 mov 0x10(%edi),%ecx / @0x45f27d mov %ecx,0x44(%esp) — spec+0x10 */
            LS(0x34) = *(const int *)((const unsigned char *)(INT_PTR)cand + 0x00);  /* @0x45f277 mov (%edi),%eax / @0x45f284 mov %eax,0x38(%esp) — spec+0x00 */
            hres = s6_heuristic(self, (int *)LS_BYTES(0x34));  /* @0x45f279 lea 0x34(%esp) / @0x45f281 push / @0x45f282 mov %esi,%ecx / @0x45f288 call 0x45c5e0 */
            /* @0x45f28d test %eax,%eax / @0x45f28f seta %al (unsigned above 0) */
            *(unsigned char *)((unsigned char *)self + 0x30) = (unsigned char)((unsigned int)hres != 0u);  /* @0x45f292 mov %al,0x30(%esi) */
        }
    }

    /* B13 (0x45f295..0x45f2f0) — гейт S(0x18) (маска 0x44 — тот же приём, что
     * в B11), NULL-путь cand == 0 → эпилог 0x45f373, propagation-цикл по map1
     * (итератор в S(0x6c), шаг — in-order successor 47f800 с записью обратно в
     * слот). Маска 0x44 = AH.C3 (0x40) | AH.C2 (0x04): FCOMPS даёт C3=1,C2=0
     * для equal, C3=C2=0 для less/greater, C3=C2=1 для unordered. Маскированный
     * байт = 0x40 только при equal → popcount 1 → PF=0 → `jnp` взят; 0x00 и
     * 0x44 → popcount чётный → PF=1 → `jnp` не взят. Значит пропуск ⟺ ровно
     * 0.0f; отрицательные и NaN идут в поиск (x87-проба: >0→AH 0x00, <0→0x01,
     * ==0→0x40, NaN→0x45). */
    if (s6_bits_get(LS(0x18)) != 0.0f) {                 /* @0x45f295 flds 0x18 / @0x45f299 fcomps [0x4ec37c] / @0x45f2a1 test $0x44 / @0x45f2a4 jnp 0x45f36f */
        if (cand == 0) {                                 /* @0x45f2aa cmp %ebx,%edi / @0x45f2ac je 0x45f373 */
            *(int *)(INT_PTR)LS(0x14) = 0;               /* @0x45f373 mov 0x14(%esp),%ecx / @0x45f377 mov %ebx,(%ecx) — *out_code = 0 */
            LD_CRASH_RET(0);                             /* @0x45f379 EAX=0 → эпилог 0x45f382 */
        }
        LS(0x6c) = *(const int *)((const unsigned char *)self->cand_head);  /* @0x45f2b2 mov 0x6c(%esi),%ecx / @0x45f2b5 mov (%ecx),%eax / @0x45f2b9 mov %eax,0x6c(%esp) — итератор = head->[0] */
        if (LS(0x6c) != (int)(INT_PTR)self->cand_head) { /* @0x45f2b7 cmp %ecx,%eax / @0x45f2bd je 0x45f2f2 — пустая карта */
            for (;;) {                                   /* виток: @0x45f2f0 jne 0x45f2c0 */
                int inner = *(const int *)((const unsigned char *)(INT_PTR)LS(0x6c) + 0x10);  /* @0x45f2c0 mov 0x10(%eax),%eax — Candidate узла */

                if (*(const int *)((const unsigned char *)(INT_PTR)inner + 0x08) == 1) {  /* @0x45f2c3 cmpl $1,0x8(%eax) / @0x45f2c7 je 0x45f2e0 */
                    /* filter == 1 — копия не делается, сразу шаг итератора */
                } else {
                    LS(0x3c) = 0;                        /* @0x45f2d3 mov %ebx,0x48(%esp) — spec+0x08 filter = 0 */
                    LS(0x38) = 0;                        /* @0x45f2d7 mov %ebx,0x44(%esp) — spec+0x04 parent = 0 */
                    s6_copy_fields(inner,                /* @0x45f2cb push %eax — dst = inner (Candidate) */
                                   (int)(INT_PTR)LS_BYTES(0x34),  /* @0x45f2cc lea 0x3c(%esp) / @0x45f2d0 push — src = &spec */
                                   6);                   /* @0x45f2c9 push $6 / @0x45f2db call 0x45c4b0 — маска 6 = +0x04 parent, +0x08 filter */
                }
                LS(0x6c) = (int)(INT_PTR)s6_tree_next((TreeNode *)(INT_PTR)LS(0x6c));  /* @0x45f2e0 lea 0x6c(%esp) / @0x45f2e4 call 0x47f800 / @0x45f2e9 mov 0x6c(%esp),%eax */
                if (LS(0x6c) == (int)(INT_PTR)self->cand_head) {  /* @0x45f2ed cmp 0x6c(%esi) / @0x45f2f0 jne 0x45f2c0 */
                    break;
                }
            }
        }
        /* B14 (0x45f2f2..0x45f36d) — вторая спека (filter 2) и e0a0 #2 по pos
         * node1; последняя запись EDI = node2 (@0x45f36d) — вход B15. */
        {
            const unsigned char *world;
            int ps_pos, n1_pos, d2;
            float ws, p14;

            ctx = (unsigned char *)self->game_ctx;       /* @0x45f2f2 mov (%esi),%edx */
            world = (const unsigned char *)*(const void **)(ctx + 0x04);  /* @0x45f2f4 mov 0x4(%edx),%ecx — world */
            memcpy(&ws, world + 0x120, sizeof(float));   /* @0x45f2f7 flds 0x120(%ecx) */
            ps_pos = *(const int *)((const unsigned char *)ebp + 0x00);  /* @0x45f2fd mov 0x0(%ebp),%eax — PathState->pos */
            n1_pos = *(const int *)((const unsigned char *)(INT_PTR)cand + 0x00);  /* @0x45f300 mov (%edi),%edx — node1->pos */
            memcpy(&p14, (const unsigned char *)(INT_PTR)ps_pos + 0x14, sizeof(float));  /* @0x45f302 fadds 0x14(%eax) */
            s6_bits_set(&LS(0x44), ws + p14);            /* @0x45f309 fstps 0x4c(%esp) — spec+0x10 = world[0x120] + pos[0x14] */
            LS(0x38) = 0;                                /* @0x45f30d mov %ebx,0x40(%esp) — spec+0x04 parent */
            LS(0x3c) = 2;                                /* @0x45f311 movl $2,0x44(%esp) — spec+0x08 filter */
            LS(0x40) = 0;                                /* @0x45f319 movl $0,0x48(%esp) — spec+0x0c */
            LS(0x48) = 0;                                /* @0x45f321 movl $0,0x50(%esp) — spec+0x14 */
            LS(0x4c) = 0;                                /* @0x45f329 mov %ebx,0x54(%esp) — spec+0x18 g_cost */
            /* c480(PathState->pos, node1->pos): arg1 — последний push %eax @0x45f306,
             * arg2 — %edx @0x45f305. */
            d2 = s6_distance2((void *)(INT_PTR)ps_pos,
                              (void *)(INT_PTR)n1_pos);  /* @0x45f305/0x45f306/0x45f32d */
            LS(0x50) = d2;                               /* @0x45f337 mov %eax,0x54(%esp) — spec+0x1c dist² */
            LS(0x58) = d2;                               /* @0x45f33b mov %eax,0x5c(%esp) — spec+0x24 cost = dist² */
            LS(0x4c) = 0;                                /* @0x45f347 mov %ebx,0x60(%esp) при esp=F−12 → F+0x4c = spec+0x18 (повторно после 0x45f329); spec+0x20 здесь НЕ обнуляется (утверждение карты §3.4/B14 — арифметическая ошибка), spec+0x2c не трогается */
            s6_copy_fields((int)(INT_PTR)ebp,            /* @0x45f33f push %ebp — dst = PathState */
                           (int)(INT_PTR)LS_BYTES(0x34), /* @0x45f340 lea 0x3c(%esp) / @0x45f344 push — src = &spec */
                           0x3fe);                       /* @0x45f332 push $0x3fe / @0x45f34b call 0x45c4b0 — +0x04..+0x20 */
            s6_timer_restart(self);                      /* @0x45f350/0x45f352 call 0x45c570 — до e0a0 #2 */
            cand = s6_e0a0_dispatch(
                self,                                    /* @0x45f366 mov %esi,%ecx */
                2,                                       /* @0x45f364 push $2 — arg1 filter */
                (void *)(INT_PTR)n1_pos,                 /* @0x45f35f mov (%edi),%eax / @0x45f363 push — arg2 node1->pos */
                LS(0x18),                                /* @0x45f35b mov 0x18(%esp),%edx / @0x45f362 push — arg3 float bits */
                (int *)(INT_PTR)LS(0x14));               /* @0x45f357 mov 0x14(%esp),%ecx / @0x45f361 push — arg4 out_code */
        }                                                /* @0x45f36d mov %eax,%edi — EDI = node2 (вход B15) */
    }

    /* B15 (0x45f36f..0x45f384) — NULL-гейт node2 == 0: *out_code = 0, return 0
     * (эпилог 0x45f379..0x45f382). */
    if (cand == 0) {                                     /* @0x45f36f cmp %ebx,%edi / @0x45f371 jne 0x45f385 */
        *(int *)(INT_PTR)LS(0x14) = 0;                   /* @0x45f373 mov 0x14(%esp),%ecx / @0x45f377 mov %ebx,(%ecx) */
        LD_CRASH_RET(0);                                 /* @0x45f379 xor %eax,%eax → эпилог 0x45f37b..0x45f382 (EAX=0) */
    }

    /* B16 (0x45f385..0x45f3d0) — база метрики (ctx[0x18] либо world[0x120] по
     * маске 0x44: jnp взят ⟺ ctx[0x18] == 0.0f → world[0x120]), +pos[0x14],
     * copy mask 0x10 (только spec+0x10 → node2+0x10), таймер, перестройка
     * ленты 45e9c0. */
    {
        const int n2pos = *(const int *)((const unsigned char *)(INT_PTR)cand + 0x00);  /* @0x45f3a5 mov (%edi),%eax — node2->pos */
        float base, p14;

        ctx = (unsigned char *)self->game_ctx;           /* @0x45f385 mov (%esi),%ecx */
        if (s6_bits_get(*(const int *)(ctx + 0x18)) != 0.0f) {  /* @0x45f387 flds 0x18 / @0x45f38a fcomps [0x4ec37c] / @0x45f392 test $0x44 / @0x45f395 jnp 0x45f39c */
            memcpy(&base, ctx + 0x18, sizeof(float));    /* @0x45f397 flds 0x18(%ecx) / @0x45f39a jmp 0x45f3a5 */
        } else {
            const unsigned char *world =
                (const unsigned char *)*(const void **)(ctx + 0x04);  /* @0x45f39c mov 0x4(%ecx),%edx */
            memcpy(&base, world + 0x120, sizeof(float)); /* @0x45f39f flds 0x120(%edx) */
        }
        memcpy(&p14, (const unsigned char *)(INT_PTR)n2pos + 0x14, sizeof(float));  /* @0x45f3a7 flds 0x14(%eax) */
        s6_bits_set(&LS(0x44), p14 + base);              /* @0x45f3ac fadd %st(1),%st / @0x45f3b3 fstps 0x4c(%esp) — spec+0x10 = pos[0x14] + base */
        s6_copy_fields((int)(INT_PTR)cand,               /* @0x45f3ae push %edi — dst = node2 */
                       (int)(INT_PTR)LS_BYTES(0x34),     /* @0x45f3af lea 0x3c(%esp) / @0x45f3b7 push — src = &spec */
                       0x10);                            /* @0x45f3aa push $0x10 / @0x45f3bc call 0x45c4b0 — маска 0x10 = только +0x10 (out_metric) */
        /* @0x45f3ba fstp %st(0) — сброс оставшейся базы (в C++ не нужен). */
        s6_timer_restart(self);                          /* @0x45f3c1/0x45f3c3 call 0x45c570 */
        native::band_rebuild(self,                       /* @0x45f3ce mov %esi,%ecx / @0x45f3cd push %edi — arg1 = node2 */
                             (void *)(INT_PTR)cand,
                             LS(0x1c));                  /* @0x45f3c8 mov 0x1c(%esp),%edx / @0x45f3cc push — arg2 = float bits S(0x1c); @0x45f3d0 call 0x45e9c0 (ret 8) */
    }

    /* B17 (0x45f3d5..0x45f427) — сдвиг кода 1→2, dist² vs this+0x34, код 4;
     * все три эпилога возвращают EAX = *(this+0x38). */
    {
        int *code = (int *)(INT_PTR)LS(0x14);            /* @0x45f3da mov 0x14(%esp),%ebx — указатель кода */
        int anchor = (int)(INT_PTR)self->anchor;         /* @0x45f3d5 mov 0x2c(%esi),%eax */

        if (*(const int *)((const unsigned char *)(INT_PTR)cand + 0x00) != anchor) {  /* @0x45f3d8 cmp %eax,(%edi) / @0x45f3de je 0x45f3eb */
            if (*code == 1) {                            /* @0x45f3e0 cmpl $1,(%ebx) / @0x45f3e3 jne 0x45f3eb */
                *code = 2;                               /* @0x45f3e5 movl $2,(%ebx) — цель сместилась */
            }
        }
        if (*code == 1 || *code == 2) {                  /* @0x45f3eb mov (%ebx),%ecx / @0x45f3ed cmp 1 / @0x45f3f2 cmp 2 / @0x45f3f5 jne 0x45f41c */
            int d2;
            /* c480(node2->pos, anchor): arg1 — последний push @0x45f3fa, arg2 — anchor @0x45f3f7. */
            d2 = s6_distance2((void *)(INT_PTR)*(const int *)((const unsigned char *)(INT_PTR)cand + 0x00),
                              (void *)(INT_PTR)anchor);  /* @0x45f3f7/0x45f3f8/0x45f3fa/0x45f3fd */
            if ((unsigned int)d2 > self->distance_threshold) {  /* @0x45f402 cmp 0x34(%esi),%eax / @0x45f405 ja 0x45f416 */
                *code = 4;                               /* @0x45f416 movl $4,(%ebx) */
            } else {
                self->distance_threshold = (unsigned int)d2;   /* @0x45f407 mov %eax,0x34(%esi) — новый best */
            }
        }
        LD_CRASH_RET(*code);                             /* @0x45f40a/0x45f41c mov (%ebx),%eax → эпилоги 0x45f413 / 0x45f425 */
    }

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        LD_CRASH_RET(0);                                 /* harness-рамка (0x44bdd0 в игре), как s6_ai_update */
    }

#undef LS_BYTES
#undef LS
}
