# battle_43.ps1 — вход в бой из сейва и 3 волны призыва: все аргументы захардкожены (4:3).
# Режим 1440x1080 F=1 (k=1); последовательность шагов — тот же канвас 1024x768, что и в 16:9,
# раннер perform_action_in_game.ps1.
param([string]$Tag = "battle43", [string]$GameRoot = $env:LD_GAME, [string]$ShotsDir = $env:LD_SHOTS,
      [int]$Waves = 3, [switch]$NoKill)
$Steps = "shot:menu;click:350:107;wait:0.2;click:280:161;wait:0.2;key:13;wait:0.2;key:13;waitload;shot:overland;hover:672:542;wait:0.2;click:672:542;wait:1;hover:413:524;wait:1;click:413:524;waitattack;key:13;waitbattle;key:32;waves;shot:summons"
& (Join-Path $PSScriptRoot "perform_action_in_game.ps1") -W 1440 -H 1080 -F 1 -Tag $Tag -MenuSec 6 `
    -Steps $Steps -Waves $Waves -SpellX 473 -SpellY 733 -CastX 500 -CastY 430 `
    -HoldMs 5000 -LoadSec 10 -AttackSec 5 -BattleSec 6 -WaveGapMs 1000 -ClickGapMs 200 `
    -EndWait 10 -GameRoot $GameRoot -ShotsDir $ShotsDir -NoKill:$NoKill
