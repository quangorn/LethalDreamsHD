# Съёмка БОЯ: новая игра -> оверленд -> скип уроков Enter'ом -> клики по тайлам
# (персонаж идёт к цели), вход в бой определяется по строке "SCENE battle" в
# hdlog.txt (hd_scale логирует смену имени сцены на событиях мыши). Снимается
# ТОЛЬКО окно игры, DPI-aware. Игра гасится после последнего снимка (-NoKill оставляет).
#
#   probe_battleshot.ps1 <W> <H> <F> <Tag> [-Sub ld] [-NewX 200 -NewY 400 -SkipClicks 2]
#       [-CharX 349 -CharY 658 -EnterCount 2 -LoadWait 5] [-LessonEnters 6]
#       [-Clicks "x:y;x:y"] [-ClickWaits 25] [-BattleWait 120 -BattleSettle 4]
#   LD_GAME/LD_REPO/LD_SCRATCH задаются; канвас -> экран: экран_x = L + sy*cx + dx*(W/1024),
#   экран_y = T + sy*cy, sy = h/768 (из hd_scale).
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "battleshot",
      [string]$Sub = "ld", [int]$MenuSec = 6, [double]$NewX = 200, [double]$NewY = 400,
      [int]$SkipClicks = 2, [double]$SkipX = 512, [double]$SkipY = 384,
      [double]$CharX = 349, [double]$CharY = 658, [int]$EnterCount = 2, [int]$LoadWait = 5,
      [int]$LessonEnters = 6, [string]$Clicks = "", [int]$ClickWaits = 25,
      [int]$BattleWait = 120, [int]$BattleSettle = 4,
      [switch]$NoKill, [string]$ScratchRoot = $env:LD_SCRATCH,
      [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class B {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
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
[void][B]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"hd.cfg -> $line"

$hdlog = "$GameRoot\hdlog.txt"
"BATTLESHOT $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [B+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][B]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object B+RECT; [void][B]::GetWindowRect($h,[ref]$rec)
      if ([B]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][B]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [B]::EnumWindows($cb,[IntPtr]::Zero)
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  "saved $name ($($main.W)x$($main.H2))"
}

# канвас -> экран: экран_x = L + sy*cx + dx*(W/1024), экран_y = T + sy*cy
function To-Abs($main, $cx, $cy) {
  $k = $main.H2 * 1024.0 / ($main.W * 768.0)
  $dx = 512.0 - 512.0 * $k
  $sy = $main.H2 / 768.0
  return @([int]($main.L + $sy * $cx + $dx * ($main.W / 1024.0)), [int]($main.T + $sy * $cy))
}

function Click($x, $y) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
  Start-Sleep -Milliseconds 400
  [B]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [B]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Enter {
  $vk = 13; $sc = [byte][B]::MapVirtualKey($vk, 0)
  [B]::keybd_event($vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [B]::keybd_event($vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru

Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW"; exit 1 }
[void][B]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII

# --- новая игра: глиф «Вообразить» -> скип ролика -> глиф персонажа -> Enter ---
[void][B]::SetForegroundWindow($main.H)
$a = To-Abs $main $NewX $NewY
Click $a[0] $a[1]
for ($i = 1; $i -le $SkipClicks; $i++) {
  $c = To-Abs $main $SkipX $SkipY
  Click $c[0] $c[1]
}
$b = To-Abs $main $CharX $CharY
Click $b[0] $b[1]
for ($i = 1; $i -le $EnterCount; $i++) { Enter }
"NEWGAME clicks sent (glyph canvas $NewX,$NewY; slot canvas $CharX,$CharY)" | Add-Content -Path $hdlog -Encoding ASCII
Start-Sleep -Seconds $LoadWait
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 600
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_overland.png"

# --- скип обучающих уроков Enter'ом (Enter = «Далее») ---
for ($i = 1; $i -le $LessonEnters; $i++) { Enter }
"LESSON enters=$LessonEnters" | Add-Content -Path $hdlog -Encoding ASCII
Start-Sleep -Seconds 2
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_postlesson.png"

# --- клики по канвасным тайлам (персонаж идёт к цели), снимок после каждой серии ---
$si = 0
foreach ($cl in $Clicks.Split(';')) {
  if (-not $cl) { continue }
  $si++
  [void][B]::SetForegroundWindow($main.H)
  $c = To-Abs $main ([double]($cl.Split(':')[0])) ([double]($cl.Split(':')[1]))
  "CLICK $si canvas $cl -> abs ($($c[0]),$($c[1]))" | Add-Content -Path $hdlog -Encoding ASCII
  Click $c[0] $c[1]
  Start-Sleep -Seconds $ClickWaits
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_click$si.png"
}

# --- вход в бой: ждём "SCENE battle" в hdlog, снимаем окно боя ---
$deadline = (Get-Date).AddSeconds($BattleWait)
$seen = $false
while ((Get-Date) -lt $deadline) {
  $t = Get-Content $hdlog -ErrorAction SilentlyContinue
  if ($t -and ($t | Select-String -Pattern 'SCENE battle' -Quiet)) { $seen = $true; break }
  Start-Sleep -Milliseconds 700
}
"battle seen=$seen (waited)" | Add-Content -Path $hdlog -Encoding ASCII
if ($seen) {
  Start-Sleep -Seconds $BattleSettle
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_battle.png"
}
# правила съёмки: игра закрывается сразу после последнего снимка (-NoKill оставляет её)
if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  "game closed pid=$($p.Id)"
} else {
  "game left running pid=$($p.Id)"
}
