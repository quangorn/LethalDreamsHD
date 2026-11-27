# perform_action_in_game.ps1 — раннер шагов над игрой: вход в бой, призыв монстров, съёмка окна.
# Последовательность действий, координаты и паузы приходят аргументами; обёртки
# `battle_169.ps1` (3840x2160 F=1) и `battle_43.ps1` (1440x1080 F=1) держат их захардкоженными.
#
#   perform_action_in_game.ps1 [-W 3840 -H 2160 -F 1] [-Tag battle] [-MenuSec 6] [-LogLevel 1]
#       [-Steps "<dsl>"] [-HoldMs 5000] [-EndWait 10] [-NoKill]
#       [-GameRoot <дерево>] [-ShotsDir <каталог>]
#       [-Waves 3] [-SpellX 473 -SpellY 733 -CastX 500 -CastY 430]
#       [-LoadSec 10] [-AttackSec 5] [-BattleSec 6] [-WaveGapMs 1000] [-ClickGapMs 200]
#       [-RestoreCfg "<W H BPP F>"]  — записать hd.cfg после прогона (обёртка 4:3 возвращает 4K)
#
# DSL шагов (через ';', координаты — канвас 1024x768, общий для 4:3 и 16:9):
#   wait:SEC         пауза SEC секунд
#   waitload         пауза -LoadSec (загрузка сейва после Enter)
#   waitattack       пауза -AttackSec (после клика «Атака»)
#   waitbattle       пауза -BattleSec (после подтверждения «Атака?», до входа в бой)
#   click:CX:CY      ЛКМ по канвасной точке
#   hover:CX:CY      наведение курсора без клика (hit-test тултипа/глифа)
#   rclick:CX:CY     ПКМ по канвасной точке
#   hold:CX:CY:MS    ПКМ вниз в точке, держать MS, отпустить (каст заклинания)
#   rdown:CX:CY      ПКМ вниз в точке и НЕ отпускать (каст идёт, между шагами)
#   rup              отпустить ПКМ в текущей позиции курсора (каст завершается)
#   key:VK           нажатие клавиши: 13=Enter (подтверждение диалога), 32=Space (пауза боя)
#   wave:SX:SY:CX:CY волна призыва: клик по слоту (SX,SY), ПКМ-удержание на цели (CX,CY)
#   waves            N волн подряд из -Waves/-SpellX/-SpellY/-CastX/-CastY/-HoldMs
#   shot:NAME        снимок окна -> <ShotsDir>/<Tag>_NAME.png
#   cpu:SEC          замер CPU процесса игры за SEC секунд (строка CPU в лог)
#
# Space (VK 32) ставит и снимает паузу боя; отправляется явно шагом key:32. Бой стартует на паузе
# (`ForcePause` в `ui/battlemenu.py`), поэтому ровно один key:32 сразу после входа в бой. Во время
# боя key:32 не посылать, кроме снятия стартовой паузы. Диалоги подтверждаются Enter'ом (key:13).
#
# Вход в бой — фиксированные шаги и паузы (детект боя по строке "SCENE battle" в hdlog.txt
# ненадёжен: в прогонах 19.11.2026 не срабатывал ни разу). Снимается ТОЛЬКО окно игры,
# DPI-aware; кнопки/клавиши идут через mouse_event/keybd_event. Сейв
# грузится из профиля пользователя (Documents\Boolat Games\Lethal Dreams\Saves),
# скрипт его не трогает. Игра гасится после последнего шага (-NoKill оставляет).
# Требует реестровый флаг ~ HIGHDPIAWARE на <GameRoot>/LETHALDREAMS.EXE (иначе окно
# масштабируется системой и канвасные точки не совпадут).
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "battle",
      [int]$MenuSec = 6, [int]$LogLevel = 1,
      [string]$Steps = "shot:menu;click:350:107;wait:0.2;click:280:161;wait:0.2;key:13;wait:0.2;key:13;waitload;shot:overland;hover:672:542;wait:0.2;click:672:542;wait:1;hover:413:524;wait:1;click:413:524;waitattack;key:13;waitbattle;key:32;waves;shot:summons",
      [int]$HoldMs = 5000, [double]$EndWait = 10,
      [int]$Waves = 3, [double]$SpellX = 473, [double]$SpellY = 733,
      [double]$CastX = 500, [double]$CastY = 430,
      [double]$LoadSec = 10, [double]$AttackSec = 5, [double]$BattleSec = 6,
      [int]$WaveGapMs = 1000, [int]$ClickGapMs = 200,
      [string]$RestoreCfg = "",
      [switch]$NoKill, [string]$GameRoot = $env:LD_GAME, [string]$ShotsDir = $env:LD_SHOTS)
if (-not $GameRoot) { throw "GameRoot не задан: -GameRoot <дерево> или LD_GAME" }
if (-not (Test-Path (Join-Path $GameRoot "LETHALDREAMS.EXE"))) { throw "Нет LETHALDREAMS.EXE в $GameRoot" }
if (-not $ShotsDir) { $ShotsDir = Join-Path (Split-Path $GameRoot -Parent) "shots" }
if (-not (Test-Path $ShotsDir)) { New-Item -ItemType Directory -Path $ShotsDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class T {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder buf, int max);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][T]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path (Join-Path $GameRoot "hd.cfg") -Value $line -Encoding ASCII
$hdlog = Join-Path $GameRoot "hdlog.txt"
"ACTIONS $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII
$env:LDHOOK_LOG = "$LogLevel"

function Log([string]$s) { Write-Output $s; Add-Content -Path $hdlog -Value $s -Encoding ASCII }

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [T+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][T]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object T+RECT; [void][T]::GetWindowRect($h,[ref]$rec)
      if ([T]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][T]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [T]::EnumWindows($cb,[IntPtr]::Zero)
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  if (-not $main) { Log "SHOT skip '$name' (no window)"; return }
  # курсор паркуется в ЦЕНТР окна: у краёв игра крутит/двигает камеру
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point([int]($main.L + $main.W / 2), [int]($main.T + $main.H2 / 2))
  Start-Sleep -Milliseconds 250
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $path = Join-Path $ShotsDir "$($Tag)_$name.png"
  $bmp.Save($path); $bmp.Dispose(); $g.Dispose()
  Log "SHOT $path ($($main.W)x$($main.H2))"
}

# канвас -> экран: экран_x = L + sy*cx + dx*(W/1024), экран_y = T + sy*cy
function To-Abs($main, $cx, $cy) {
  $k = $main.H2 * 1024.0 / ($main.W * 768.0)
  $dx = 512.0 - 512.0 * $k
  $sy = $main.H2 / 768.0
  return @([int]($main.L + $sy * $cx + $dx * ($main.W / 1024.0)), [int]($main.T + $sy * $cy))
}

function Move-To($x, $y) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point([int]$x, [int]$y)
  Start-Sleep -Milliseconds $ClickGapMs
}

function Click($x, $y) {
  Move-To $x $y
  [T]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
  [T]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
}

function RClick($x, $y) {
  Move-To $x $y
  [T]::mouse_event(0x8,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
  [T]::mouse_event(0x10,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
}

function Hold($x, $y, [int]$ms) {
  Move-To $x $y
  [T]::mouse_event(0x8,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ms
  [T]::mouse_event(0x10,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
}

# ПКМ-удержание разбито на два шага: каст идёт между ними, и в середину можно
# вставить shot (полоса каста видна только во время удержания).
function RDown($x, $y) {
  Move-To $x $y
  [T]::mouse_event(0x8,0,0,0,[IntPtr]::Zero)
}

function RUp() {
  [T]::mouse_event(0x10,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
}

function Key([int]$vk) {
  $sc = [byte][T]::MapVirtualKey([uint32]$vk, 0)
  [T]::keybd_event([byte]$vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 50
  [T]::keybd_event([byte]$vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds $ClickGapMs
}

function Cpu([int]$sec) {
  # CPU-замер: CPU-секунды процесса игры за sec секунд (доля одного ядра)
  $g = Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $g) { Log "CPU skip (no game process)"; return }
  $c1 = $g.CPU
  Start-Sleep -Seconds $sec
  $g.Refresh()
  $d = $g.CPU - $c1
  Log ("CPU pid={0} {1:N2}s over {2}s ({3:P0} of one core)" -f $g.Id, $d, $sec, ($d / $sec))
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$p = Start-Process (Join-Path $GameRoot "LETHALDREAMS.EXE") -WorkingDirectory $GameRoot -PassThru
Log "LAUNCH pid=$($p.Id) root=$GameRoot LDHOOK_LOG=$LogLevel"

Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { Log ("EXITED menu code=0x{0:X}" -f $p.ExitCode); exit 1 }
$main = Find-Main $p.Id
if (-not $main) { Log "NO WINDOW"; exit 1 }
[void][T]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 500
$main = Find-Main $p.Id
Log "WINDOW $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F"

$si = 0
foreach ($step in $Steps.Split(';')) {
  $step = $step.Trim()
  if (-not $step) { continue }
  $si++
  $v = $step.Split(':')
  $verb = $v[0].ToLower()
  $main = Find-Main $p.Id
  if (-not $main) { Log "STEP $si $step -> NO WINDOW"; break }
  [void][T]::SetForegroundWindow($main.H)
  switch ($verb) {
    'wait'       { Start-Sleep -Milliseconds ([int]([double]$v[1] * 1000)); Log "STEP $si wait $($v[1])s" }
    'waitload'   { Start-Sleep -Milliseconds ([int]($LoadSec * 1000)); Log "STEP $si waitload ${LoadSec}s" }
    'waitattack' { Start-Sleep -Milliseconds ([int]($AttackSec * 1000)); Log "STEP $si waitattack ${AttackSec}s" }
    'waitbattle' { Start-Sleep -Milliseconds ([int]($BattleSec * 1000)); Log "STEP $si waitbattle ${BattleSec}s" }
    'click'      { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si click canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; Click $a[0] $a[1] }
    'hover'      { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si hover canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; Move-To $a[0] $a[1] }
    'rclick'     { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si rclick canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; RClick $a[0] $a[1] }
    'hold'       { $ms = $HoldMs; if ($v.Count -gt 3 -and $v[3]) { $ms = [int]$v[3] }; Log "STEP $si hold canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1])) ${ms}ms"; Hold $a[0] $a[1] $ms }
    'rdown'      { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si rdown canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; RDown $a[0] $a[1] }
    'rup'        { Log "STEP $si rup (release at cursor)"; RUp }
    'key'        { Log "STEP $si key $($v[1])"; Key ([int]$v[1]) }
    'wave'       { $ms = $HoldMs; if ($v.Count -gt 5 -and $v[5]) { $ms = [int]$v[5] }
                   $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); $b = To-Abs $main ([double]$v[3]) ([double]$v[4])
                   Log "STEP $si wave canvas slot $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1])) cast $($v[3]),$($v[4]) -> abs ($($b[0]),$($b[1])) ${ms}ms"
                   Click $a[0] $a[1]; Hold $b[0] $b[1] $ms; Start-Sleep -Milliseconds $WaveGapMs }
    'waves'      { $w = 0
                   while ($w -lt $Waves) { $w++
                     $a = To-Abs $main $SpellX $SpellY; $b = To-Abs $main $CastX $CastY
                     Log "STEP $si wave $w canvas slot $SpellX,$SpellY -> abs ($($a[0]),$($a[1])) cast $CastX,$CastY -> abs ($($b[0]),$($b[1])) ${HoldMs}ms"
                     Click $a[0] $a[1]; Hold $b[0] $b[1] $HoldMs; Start-Sleep -Milliseconds $WaveGapMs } }
    'cpu'        { $sec = 3; if ($v.Count -gt 1 -and $v[1]) { $sec = [int]$v[1] }; Log "STEP $si cpu $($sec)s"; Cpu $sec }
    'shot'       { Shot $main $v[1]; Log "STEP $si shot $($v[1])" }
    default      { Log "STEP $si unknown '$step'" }
  }
  $p.Refresh()
  if ($p.HasExited) { Log ("EXITED step=$si code=0x{0:X}" -f $p.ExitCode); break }
}

if ($EndWait -gt 0) {
  Start-Sleep -Milliseconds ([int]($EndWait * 1000))
  Log "END wait ${EndWait}s"
}
if (-not $NoKill) {
  taskkill /F /IM LETHALDREAMS.EXE 2>$null | Out-Null
  Log "KILL taskkill /F /IM LETHALDREAMS.EXE"
} else {
  Log "LEAVE running pid=$($p.Id)"
}
if ($RestoreCfg) {
  Set-Content -Path (Join-Path $GameRoot "hd.cfg") -Value $RestoreCfg -Encoding ASCII
  Log "CFG restore $RestoreCfg"
}
