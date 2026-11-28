# play_profiled.ps1 — запуск игры с логами для РУЧНОГО боя (без автоматизации).
#
# Зачем: синтетический сценарий battle_169 не воспроизводит настоящий бой.
# Скрипт только чистит hdlog.txt и запускает игру; играет человек. Игру скрипт
# НЕ гасит.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/probes/play_profiled.ps1 `
#       -GameRoot "<tree>"    (или LD_GAME из окружения)
#
#   -Trace 2  (по умолчанию) — TICKSEC раз в секунду + полные BATTLE-строки для
#            тиков длиннее 5 ms; объём ~0.3 МБ/мин, хватает на часы боя.
#   -Trace 1  — строка на каждый тик (~0.4 МБ/с в толпе; только для коротких боёв).
#   -E0Diag 1 — E0COST/E0PATH/E0TICK по вызовам (дорого: итерация ~2.4x).
#
# Порт e0a0 в поставке включён по умолчанию (LDHOOK_SITE6=1 в коде), поэтому
# env здесь не выставляется: профилируется ровно то, что играет пользователь.
#
# Разбор после игры (игра закрыта):
#   python3 tools/probes/tick_stats.py "<GameRoot>/hdlog.txt"
param(
    [string]$GameRoot = $env:LD_GAME,
    [int]$Trace = 2,
    [int]$E0Diag = 0
)
$ErrorActionPreference = 'Stop'
if (-not $GameRoot) { throw "GameRoot не задан (нужен -GameRoot или LD_GAME)" }
$exe = Join-Path $GameRoot 'LETHALDREAMS.EXE'
if (-not (Test-Path $exe)) { throw "нет EXE: $exe" }

$running = Get-Process -Name 'LETHALDREAMS' -ErrorAction SilentlyContinue
if ($running) { throw "игра уже запущена (pid $($running.Id)) — закройте её перед профилируемым запуском" }

$hdlog = Join-Path $GameRoot 'hdlog.txt'
foreach ($f in @($hdlog, "$hdlog.1")) {
    if (Test-Path $f) { Remove-Item $f -Force }
}

$env:LDHOOK_LOG = '1'
$env:LDHOOK_BATTLE_TRACE = "$Trace"
if ($E0Diag -ne 0) { $env:LDHOOK_E0_DIAG = '1' } else { Remove-Item Env:LDHOOK_E0_DIAG -ErrorAction SilentlyContinue }

$p = Start-Process -FilePath $exe -WorkingDirectory $GameRoot -PassThru
Write-Output "STARTED pid=$($p.Id) root=$GameRoot trace=$Trace e0diag=$E0Diag"
Write-Output "играйте бой; когда закончите — закройте игру и запустите:"
Write-Output "  python3 tools/probes/tick_stats.py `"$hdlog`""
