# INDEX.md — указатель документации проекта

`docs/NOTES.md` (1886 строк) удалён; содержание распределено по тематическим документам:
`docs/**` — актуальные знания, `docs/archive/**` — хроника, выполненные планы и отменённые
планы. Указатель — этот файл.

| куда перенесено содержание бывших заметок | файл |
|---|---|
| состояние проекта и установки (срезы дат) | `docs/STATE.md` |
| модель проверки, ярусы чекера, контрольные точки | `docs/VERIFY.md` |
| все патчи (EXE-цепочка, borderless, шрифты, UI-правки, k-схема) | `docs/PATCHES.md` |
| канонические пайплайны, recovery-декомпилят, база `decompiled_fixed/` | `docs/PIPELINE.md` |
| инструментальный инвентарь и тулчейн сессий | `docs/TOOLS.md` |
| факты о движке, форматах, поведении игры, chokepoint'ах, пути в бой | `docs/KNOWLEDGE.md` |
| наблюдения: рендер, DPI, геометрия мира и проекция, Python-слой (`python22.dll`, `Lib`, корневые `.pyc`) | `docs/OBSERVATIONS_ENGINE.md` |
| наблюдения: оверленд, боевой HUD, диалоги, съёмка экранов | `docs/OBSERVATIONS_UI.md` |
| наблюдения: шрифты, глифы, advance, коэффициент A, масштаб текста | `docs/OBSERVATIONS_FONT.md` |
| наблюдения: раскод `.btf`, packer, курсоры, иконки, апскейлы | `docs/OBSERVATIONS_TEXTURE.md` |
| наблюдения: боевой AI — автомат тика, поиск цели, A*, метрика | `docs/OBSERVATIONS_BATTLE_AI.md` |
| грабли (симптом → причина → обход) | `docs/PITFALLS.md` |
| реестр опровергнутых утверждений | `docs/CONTRADICTIONS.md` |
| план исследования: прокси-DLL вместо cave-патчей (C, логгер, размер) | `docs/PLAN_proxy_dll.md` |
| передача по `.pyd`-хукам: статус, варианты дальнейших действий, гейты | `docs/archive/HANDOFF_ldhook.md` |
| хроника итераций сессий, срезы состояния, установка апскейлов, цель съёмки R/E/F1 | `docs/archive/SESSION_LOG.md` |
| отменённые планы Q-режима (формула квадрата, план патча, код) | `docs/archive/CANCELLED_Q_MODE.md` |
| архивные документы прошлых сессий (планы, исследования, handoff) | `docs/archive/README.md` (указатель) |

## Порядок чтения

`docs/INDEX.md` → `docs/STATE.md` → `docs/VERIFY.md` → `docs/PATCHES.md` → `docs/PIPELINE.md` →
`docs/TOOLS.md` → `docs/KNOWLEDGE.md` → `docs/OBSERVATIONS_ENGINE.md` → `docs/OBSERVATIONS_UI.md` →
`docs/OBSERVATIONS_FONT.md` → `docs/OBSERVATIONS_TEXTURE.md` → `docs/OBSERVATIONS_BATTLE_AI.md` → `docs/PITFALLS.md` →
`docs/CONTRADICTIONS.md` → `docs/archive/**`.

## Назначение файлов

| файл | назначение |
|---|---|
| `docs/INDEX.md` | этот указатель: что где, порядок чтения, «где искать X» |
| `docs/STATE.md` | состояние установки и проекта (последняя проверенная версия, константы, дрейфы) |
| `docs/VERIFY.md` | модель проверки: чекер, три яруса, эталоны, известные дрейфы, контрольные точки |
| `docs/PATCHES.md` | патч-модель (база+дифф+апликатор, рецепт .pyc) и все патчи: EXE, borderless, шрифты, UI |
| `docs/PERF_BATTLE.md` | профилирование тормозов боя: замеры, тяжёлый вызов `FUN_0045e0a0`, решения по `site4`/`site5` |
| `docs/PLAN_BATTLE_AI_CPP.md` | план порта боевой логики и pathfinding в C++: границы порта, фазы 0–7, разведка REA, гейты |
| `docs/archive/TASK_phase5_handoff.md` | хендофф Фазы 5 (`44bdd0`) до гейта: состояние, инструменты, грабли, порядок работы |
| `docs/PIPELINE.md` | канонические команды и пайплайны (worker 2.2.3, cache/lib, база декомпилята) |
| `docs/TOOLS.md` | инструментальный инвентарь: что запускаемо, что архив, тулчейн сессий |
| `docs/KNOWLEDGE.md` | факты о движке/рантайме: API, форматы, механика, поведение игры |
| `docs/archive/TASK_tex_unpacker.md` | план распаковки почвенных текстур `.tex` (из `.map`) в PNG: фазы A–E, REA-точки, критерии готовности |
| `docs/archive/ISSUE_tex_upscale.md` | проблема и РЕШЕНИЕ: движок не грузил `.tex` с изменённым w/h (2x-апскейл) — эксперименты, сигнатура отказа, TLV-размеры записей, правка патчера, гейты |
| `docs/OBSERVATIONS_ENGINE.md` | наблюдения: рендер, окно/DPI, геометрия мира, проекция, Python-слой (`python22.dll`, `Lib`, корневые `.pyc`) |
| `docs/OBSERVATIONS_UI.md` | наблюдения: оверленд, бои, диалог, меню, съёмка экранов, baseline |
| `docs/OBSERVATIONS_FONT.md` | наблюдения: шрифты, глифы, advance, коэффициент A, масштаб текста |
| `docs/OBSERVATIONS_TEXTURE.md` | наблюдения: раскод/формат `.btf`, packer, курсоры, иконки, апскейлы |
| `docs/OBSERVATIONS_BATTLE_AI.md` | наблюдения: боевой AI — автомат тика `44bdd0`, цикл `45e0a0`, A* (PathState/карты/маски), метрика и выбор цели, бюджет вызовов |
| `docs/PITFALLS.md` | известные грабли: симптом → причина → обход |
| `docs/CONTRADICTIONS.md` | реестр опровергнутых утверждений (фиксировать, не «починить» молча) |
| `docs/PLAN_proxy_dll.md` | план исследования: прокси-`python22.dll` вместо cave-патчей (C, логгер, снятие лимита 238 байт) |
| `docs/archive/HANDOFF_ldhook.md` | передача по `.pyd`-хукам: что сделано/проверено, чего нет, варианты A–E и гейты |
| `docs/archive/README.md` | указатель архивных документов (планы, исследования, handoff, старые заметки) |
| `docs/archive/SESSION_LOG.md` | хроника итераций сессий (срезы состояния, замеры, установки апскейлов) |
| `docs/archive/CANCELLED_Q_MODE.md` | отменённые планы Q-режима (история, не реализация) |
| `docs/archive/NARRATIVE_2026-11.md` | дословные датированные повествования граблей и патчей (свёрнутые секции `docs/PITFALLS.md`, `docs/PATCHES.md`) |

## Где искать X

| что нужно | где искать |
|---|---|
| состояние установки, md5 exe, цепочка патчей, дрейфы | `docs/STATE.md` |
| эталоны и ярусы проверки, команды чекера, контрольные точки | `docs/VERIFY.md` |
| канонические команды и пайплайны | `docs/PIPELINE.md`, `docs/TOOLS.md` |
| рецепт .pyc и модель патчей (diff/апликатор) | `docs/PATCHES.md` |
| грабли (съёмка, Frida, cygwin-пути, PowerShell, 7z, кэши, декомпилят) | `docs/PITFALLS.md` |
| тормоза боя, CPU-профиль, тяжёлые вызовы | `docs/PERF_BATTLE.md`, `tools/perf/` |
| порт боевой логики и поиска пути в C++: границы порта, фазы, гейты; офлайн-харнесс тика | `docs/PLAN_BATTLE_AI_CPP.md`, `tools/ldhook/README.md` (bdd0-харнесс), `docs/TOOLS.md` («Боевой офлайн-харнесс»), `docs/PIPELINE.md` п. 8 |
| опровергнутые утверждения и ложные выводы прошлых сессий | `docs/CONTRADICTIONS.md` |
| переход с cave-патчей на DLL: прокси `python22.dll`, C вместо ассмблера, логгер, лимит `.text` | `docs/PLAN_proxy_dll.md` |
| что передать новому агенту по `.pyd`-хукам: статус, следующий хук, чек-лист перехода | `docs/archive/HANDOFF_ldhook.md` |
| раскладка UI и baseline-снимки (оверленд, бой, диалоги) | `docs/OBSERVATIONS_UI.md` |
| геометрия мира, DPI, рендер, матрица проекции | `docs/OBSERVATIONS_ENGINE.md` |
| Python-слой: какая DLL стоит, зачем `Lib`, что за корневые `.pyc`, обязательность слоя, восстановление | `docs/OBSERVATIONS_ENGINE.md` (раздел «Python-слой»), `docs/PITFALLS.md` |
| масштаб текста, природа глифов, коэффициент A | `docs/OBSERVATIONS_FONT.md` |
| формат `.btf`, packer, апскейлы, курсоры/иконки | `docs/OBSERVATIONS_TEXTURE.md` |
| факты о движке: API, форматы `.fdf`/`.tt`, chokepoint'ы, путь в бой | `docs/KNOWLEDGE.md` |
| почва карт и `.tex`-пайплайн (извлечение, раскод, изменённые размеры) | `docs/OBSERVATIONS_TEXTURE.md` («Почвенные текстуры карт»), `docs/TOOLS.md` («tex-пайплайн»); решение — `docs/archive/ISSUE_tex_upscale.md`, грабли — `docs/PITFALLS.md` |
| история сессий (что делала каждая итерация, по датам) | `docs/archive/SESSION_LOG.md`, `docs/archive/NARRATIVE_2026-11.md` |
| отменённые/снятые планы (Q-режим) | `docs/archive/CANCELLED_Q_MODE.md` |
| задачи и исследования прошлых сессий (выполненные) | `docs/archive/README.md` |

## Формат записей в перенесённых документах

Формат записи: каждый пункт помечен статусом
`[P]` — проверено мной экспериментом/замером в этой сессии;
`[U]` — сказано пользователем (принимаем как факт наблюдения);
`[?r]` — из старых доков (`docs/archive/NOTES_OLD.md`), **не перепроверено, на веру не принимать**.
Всё, что противоречит `docs/archive/NOTES_OLD.md`, приоритетно; архив — в `docs/archive/NOTES_OLD.md`.

# Lethal Dreams HD — актуальные проверенные знания

Маркеры `[P]`, `[U]`, `[?r]` сохранены дословно (источник — распределённый `docs/NOTES.md`). Ссылки на файлы снабжены адресом и статусом: путь репозитория (источник в git),
`config[game.root]/…` (файл установки), артефакт прогона
(`scratch_dir('ld')/…`, `paths.cache_dir('cave')/…`, `paths.btf_backup_dir()/…`,
`paths.assets_dir(…)` — пересоздаётся скриптами, в git не входит),
«приватный архив сессий …, entry …», «утрачен: покрытия нет ни в git, ни в приватных
архивах сессий».

Правила проекта — `AGENTS.md`; быстрый старт и структура репозитория — `README.md`.
