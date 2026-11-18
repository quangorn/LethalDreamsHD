# probe_camera.ps1 — прогон боевой камеры: новая игра -> скип обучения -> MMB-драг и
# колесо; после каждого действия снимается ТОЛЬКО окно игры (DPI-aware).
# Нужен зондовый battlecamera.pyc (пишет PROBE-* строки в hdlog.txt).
#
#   probe_camera.ps1 <W> <H> <F> <Tag> [-GameRoot <dir>] [-LessonEnters 15]
#       [-NoMMB] [-NoWheel] [-NoKill] [-MenuSec 6]
#
# Мышиные события: MMB down = 0x20, up = 0x40, wheel = 0x800 (delta 120 = один щелчок).
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "camprobe",
      [string]$GameRoot = $env:LD_GAME, [int]$MenuSec = 6,
      [double]$NewX = 200, [double]$NewY = 400, [int]$SkipClicks = 2,
      [double]$SkipX = 512, [double]$SkipY = 384,
      [double]$CharX = 349, [double]$CharY = 658, [int]$EnterCount = 2,
      [int]$LoadWait = 6, [int]$LessonEnters = 30,
      [int]$StepPx = 40, [int]$Steps = 12, [int]$SettleMs = 700,
      [switch]$NoMMB, [switch]$NoWheel, [switch]$NoKill)
$ErrorActionPreference = "Continue"
if (-not $GameRoot) { $GameRoot = "D:/tmp/cygwin/opencode/ld/tex_hook_tree" }
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
$ShotDir = "D:/tmp/cygwin/opencode/ld/cam_probe"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class C {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder buf, int max);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, int d, IntPtr e);
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][C]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
$hdlog = "$GameRoot\hdlog.txt"
"CAMPROBE $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [C+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][C]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object C+RECT; [void][C]::GetWindowRect($h,[ref]$rec)
      if ([C]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][C]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [C]::EnumWindows($cb,[IntPtr]::Zero)
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

function To-Abs($main, $cx, $cy) {
  $k = $main.H2 * 1024.0 / ($main.W * 768.0)
  $dx = 512.0 - 512.0 * $k
  $sy = $main.H2 / 768.0
  return @([int]($main.L + $sy * $cx + $dx * ($main.W / 1024.0)), [int]($main.T + $sy * $cy))
}

function Click($x, $y) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
  Start-Sleep -Milliseconds 400
  [C]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [C]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Enter {
  $vk = 13; $sc = [byte][C]::MapVirtualKey($vk, 0)
  [C]::keybd_event($vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [C]::keybd_event($vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function MoveTo($x, $y) {
  [C]::SetCursorPos($x, $y) | Out-Null
  Start-Sleep -Milliseconds 120
}

# MMB drag: зажать среднюю кнопку и вести курсор (шаги по $StepPx)
function MMBDrag($x0, $y0, $dx, $dy, $tag) {
  MoveTo $x0 $y0
  [C]::mouse_event(0x20,0,0,0,[IntPtr]::Zero)   # MIDDLEDOWN
  Start-Sleep -Milliseconds 250
  for ($i = 1; $i -le $Steps; $i++) {
    MoveTo ($x0 + $dx * $i) ($y0 + $dy * $i)
  }
  Start-Sleep -Milliseconds $SettleMs
  Shot $main "$ShotDir\$($Tag)_mmb_$tag.png"
  [C]::mouse_event(0x40,0,0,0,[IntPtr]::Zero)   # MIDDLEUP
  Start-Sleep -Milliseconds 250
}

# колесо: $notches > 0 — вверх (zoom in), < 0 — вниз
function Wheel($notches, $tag) {
  MoveTo $cx $cy
  $d = 120 * $notches
  for ($i = 0; $i -lt [Math]::Abs($notches); $i++) {
    [C]::mouse_event(0x800,0,0,$d,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 350
  }
  Start-Sleep -Milliseconds $SettleMs
  Shot $main "$ShotDir\$($Tag)_wheel_$tag.png"
}

# удержание клавиши (для сверки знака вращения с клавой)
function HoldKey([int]$vk, [int]$ms, [string]$tag) {
  $sc = [byte][C]::MapVirtualKey($vk, 0)
  [C]::keybd_event([byte]$vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds $ms
  [C]::keybd_event([byte]$vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds $SettleMs
  Shot $main "$ShotDir\$($Tag)_key_$tag.png"
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$p = Start-Process (Join-Path $GameRoot "LETHALDREAMS.EXE") -WorkingDirectory $GameRoot -PassThru
Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW"; exit 1 }
[void][C]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 400
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII
$cx = $main.L + [int]($main.W / 2)
$cy = $main.T + [int]($main.H2 / 2)

# --- новая игра: глиф «Вообразить» -> скип ролика -> глиф персонажа -> Enter ---
[void][C]::SetForegroundWindow($main.H)
$a = To-Abs $main $NewX $NewY
Click $a[0] $a[1]
for ($i = 1; $i -le $SkipClicks; $i++) { $c = To-Abs $main $SkipX $SkipY; Click $c[0] $c[1] }
$b = To-Abs $main $CharX $CharY
Click $b[0] $b[1]
for ($i = 1; $i -le $EnterCount; $i++) { Enter }
"NEWGAME clicks sent" | Add-Content -Path $hdlog -Encoding ASCII
Start-Sleep -Seconds $LoadWait
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_overland.png"

# --- скип обучения: Enter = «Далее» ---
for ($i = 1; $i -le $LessonEnters; $i++) { Enter }
"LESSON enters=$LessonEnters" | Add-Content -Path $hdlog -Encoding ASCII
Start-Sleep -Seconds 3
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_battle.png"

if (-not $NoMMB) {
  HoldKey ([int]0x57) ([int]900) "W"      # W = turn_left (эталон знака)
  Start-Sleep -Milliseconds 400
  MMBDrag ($cx + 400) $cy ([int]40) ([int]0) "right"
  Start-Sleep -Milliseconds 400
  MMBDrag ($cx - 300) $cy ([int]-40) ([int]0) "left"
  Start-Sleep -Milliseconds 400
  MMBDrag $cx ($cy + 200) ([int]0) ([int]-25) "up"
  Start-Sleep -Milliseconds 400
  MMBDrag $cx ($cy - 200) ([int]0) ([int]25) "down"
}
if (-not $NoWheel) {
  Wheel ([int]3) "up"
  Start-Sleep -Milliseconds 400
  Wheel ([int]-3) "down"
}

$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_final.png"
if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  "game closed pid=$($p.Id)"
} else {
  "game left running pid=$($p.Id)"
}