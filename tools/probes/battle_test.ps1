# battle_test.ps1 — вход в бой из сейва и призыв монстров удержанием ПКМ.
#
#   battle_test.ps1 [-W 3840 -H 2160 -F 1] [-Tag battle] [-MenuSec 1] [-LogLevel 1]
#       [-Steps "<dsl>"] [-BattleWait 180] [-HoldMs 5000] [-EndWait 3] [-NoKill]
#       [-GameRoot <дерево>] [-ShotsDir <каталог>]
#
# DSL шагов (через ';', координаты — канвас 1024x768):
#   wait:SEC         пауза
#   click:CX:CY      ЛКМ по канвасной точке
#   rclick:CX:CY     ПКМ по канвасной точке
#   hold:CX:CY:MS    ПКМ вниз в точке, держать MS, отпустить (каст заклинания)
#   key:VK           нажатие клавиши (13=Enter, 32=Space)
#   shot:NAME        снимок окна -> <ShotsDir>/<Tag>_NAME.png
#   waitfor:battle[:SEC]  ждать строку "SCENE battle" в hdlog.txt (курсор шевелится:
#                    hd_scale пишет SCENE только на событиях мыши)
#
# Детект боя — строка "SCENE battle" в config[game.root]/hdlog.txt. Снимается ТОЛЬКО
# окно игры, DPI-aware; кнопки/клавиши идут через mouse_event/keybd_event. Сейв
# грузится из профиля пользователя (Documents\Boolat Games\Lethal Dreams\Saves),
# скрипт его не трогает. Игра гасится после последнего шага (-NoKill оставляет).
# Требует реестровый флаг ~ HIGHDPIAWARE на <GameRoot>/LETHALDREAMS.EXE (иначе окно
# масштабируется системой и канвасные точки не совпадут).
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "battle_test",
      [int]$MenuSec = 1, [int]$LogLevel = 1,
      [string]$Steps = "shot:menu;click:350:107;wait:0.2;click:280:161;wait:0.2;key:13;wait:0.2;key:13;wait:1;shot:overland;hover:672:542;wait:0.2;click:672:542;wait:0.2;hover:413:524;wait:0.2;click:413:524;wait:1.5;key:32;waitfor:battle:10;shot:battle;click:473:733;wait:0.2;hold:500:430:5000;shot:monsters1;click:473:733;wait:0.2;hold:500:430:5000;shot:monsters2",
      [int]$BattleWait = 180, [int]$HoldMs = 5000, [double]$EndWait = 3,
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
"BATTLETEST $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII
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
  Start-Sleep -Milliseconds 200
}

function Click($x, $y) {
  Move-To $x $y
  [T]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
  [T]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
}

function RClick($x, $y) {
  Move-To $x $y
  [T]::mouse_event(0x8,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
  [T]::mouse_event(0x10,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
}

function Hold($x, $y, [int]$ms) {
  Move-To $x $y
  [T]::mouse_event(0x8,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds $ms
  [T]::mouse_event(0x10,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
}

function Key([int]$vk) {
  $sc = [byte][T]::MapVirtualKey([uint32]$vk, 0)
  [T]::keybd_event([byte]$vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 50
  [T]::keybd_event([byte]$vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 200
}

function Wait-Battle([int]$timeout) {
  $deadline = (Get-Date).AddSeconds($timeout)
  $main = $null
  $i = 0
  while ((Get-Date) -lt $deadline) {
    $t = Get-Content $hdlog -ErrorAction SilentlyContinue
    if ($t -and ($t | Select-String -Pattern 'SCENE battle' -Quiet)) { return $true }
    # SCENE пишется на событиях мыши — шевелим курсор у ЦЕНТРА окна (у краёв камера уезжает)
    $i++
    $main = Find-Main $p.Id
    if ($main) {
      $mx = [int]($main.L + $main.W / 2) + (($i % 2) * 24 - 12)
      $my = [int]($main.T + $main.H2 / 2) + (($i % 3) * 16 - 16)
      [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($mx, $my)
    }
    Start-Sleep -Milliseconds 700
    $p.Refresh()
    if ($p.HasExited) { return $false }
  }
  return $false
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
    'wait'    { Start-Sleep -Milliseconds ([int]([double]$v[1] * 1000)); Log "STEP $si wait $($v[1])s" }
    'click'   { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si click canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; Click $a[0] $a[1] }
    'hover'   { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si hover canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; Move-To $a[0] $a[1] }
    'rclick'  { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); Log "STEP $si rclick canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1]))"; RClick $a[0] $a[1] }
    'hold'    { $a = To-Abs $main ([double]$v[1]) ([double]$v[2]); $ms = $HoldMs; if ($v.Count -gt 3 -and $v[3]) { $ms = [int]$v[3] }; Log "STEP $si hold canvas $($v[1]),$($v[2]) -> abs ($($a[0]),$($a[1])) ${ms}ms"; Hold $a[0] $a[1] $ms }
    'key'     { Log "STEP $si key $($v[1])"; Key ([int]$v[1]) }
    'shot'    { Shot $main $v[1]; Log "STEP $si shot $($v[1])" }
    'waitfor' {
      $to = $BattleWait
      if ($v.Count -gt 2 -and $v[2]) { $to = [int]$v[2] }
      $seen = Wait-Battle $to
      Log "STEP $si waitfor battle seen=$seen (timeout ${to}s)"
    }
    default   { Log "STEP $si unknown '$step'" }
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
