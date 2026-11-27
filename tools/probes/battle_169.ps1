# battle_169.ps1 — вход в бой из сейва и N волн призыва: все аргументы захардкожены (16:9).
# Режим 3840x2160 F=1; последовательность шагов — канвас 1024x768, раннер
# perform_action_in_game.ps1. Каст по земле, по 2 саммона за волну; снимок через
# -AfterWavesWait секунд после последней волны; игра гасится после последнего шага.
# Дефолт -AfterWavesWait 1 = прежний боевой гейт: снимок через 1 с после последней волны
# (прежний интервал задавал WaveGapMs=1000), т.е. тайминг сценария не меняется;
# steps-строка получает явный `wait:1` перед shot.
# Для стресса «много монстров» -AfterWavesWait 20: враг успевает сам призвать юнитов,
# и на снимке карта реально нагружена обеими сторонами.
# -HoldMs 6000 — из-за лага каст 5 с иногда не доходит, призывается 1 саммон вместо 2;
# гейт: на снимке после волн ≥2 саммона рядом с персонажем. 6 с — постоянный дефолт
# канонического боевого гейта (baseline и stress гоняют с 6 с).
# Снимок castbar: ПКМ-удержание разбито на rdown/rup, между ними shot — полоса каста
# (жёлтый сплэш-сектор `bhud_spgauge`) видна только во время удержания.
# Шаги и паузы — docs/TOOLS.md.
param([string]$Tag = "battle169", [string]$GameRoot = $env:LD_GAME, [string]$ShotsDir = $env:LD_SHOTS,
      [int]$Waves = 3, [double]$AfterWavesWait = 1, [switch]$NoKill)
$Steps = "shot:menu;click:350:107;wait:0.2;click:280:161;wait:0.2;key:13;wait:0.2;key:13;waitload;shot:overland;hover:672:542;wait:0.2;click:672:542;wait:1;hover:413:524;wait:1;click:413:524;waitattack;key:13;waitbattle;key:32;waves;click:473:733;rdown:500:430;wait:3;shot:castbar;rup;wait:$AfterWavesWait;shot:summons"
& (Join-Path $PSScriptRoot "perform_action_in_game.ps1") -W 3840 -H 2160 -F 1 -Tag $Tag -MenuSec 6 `
    -Steps $Steps -Waves $Waves -SpellX 473 -SpellY 733 -CastX 500 -CastY 430 `
    -HoldMs 6000 -LoadSec 10 -AttackSec 5 -BattleSec 6 -WaveGapMs 1000 -ClickGapMs 200 `
    -EndWait 10 -GameRoot $GameRoot -ShotsDir $ShotsDir -NoKill:$NoKill
