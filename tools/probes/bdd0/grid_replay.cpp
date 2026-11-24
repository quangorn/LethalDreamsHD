/* tools/probes/bdd0/grid_replay.cpp — replay-хуки battle/grid.cpp для офлайн-харнесса.
 *
 * grid.cpp под BATTLE_REPLAY объявляет две внешние сущности (шапка файла):
 *   g_replay_override      — 1: s6_grid_lookup отдаёт ячейку из модели,
 *   replay_grid_cell(x,y)  — сама модель.
 * Живут они в tools/probes/battle_replay.cpp (прогон боевого слоя целиком); в
 * офлайн-харнессе/синтетике этот файл не линкуется, а grid.cpp теперь линкуется
 * (тело тика зовёт порт 0x458050). Поэтому — минимальные определения:
 * переопределения нет (0), модель пуста.
 *
 * s6_grid_lookup (= 0x4570d0) не вызывается ни одной ветвью портированного тела:
 * индексы считают сами s6_grid_key_lookup и s6_grid_anchor_lookup. Тихо
 * подставлять «живую» ячейку здесь нельзя — это сделало бы непроверенное
 * значение похожим на проверенное. */
extern "C" int g_replay_override = 0;

extern "C" void *replay_grid_cell(int x, int y)
{
    (void)x;
    (void)y;
    return 0;
}
