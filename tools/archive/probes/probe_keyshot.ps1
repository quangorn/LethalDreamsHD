# Съёмка панелей оверленда по КЛАВИШАМ в любом режиме (4:3 / 16:9).
# Путь: загрузка сейва кликами (глиф «Вспомнить» -> строка сейва -> Enter) -> оверленд,
# далее шаги; каждый шаг = список клавиш, снимок ОКНА игры в конце шага.
# Шаги задаются списком строк через ';'; клавиши в строке через ','.
#  Esc/Tab/F1 - имена, буквы - как есть. По умолчанию последовательность пользователя:
#    S ; Esc,R ; Esc,E ; Esc,G ; Esc,F1 ; Esc,Tab ; Tab
# Снимается ТОЛЬКО окно игры (DPI-aware). -Strip снимает рамку окна (для 4:3-окна,
# чтобы снимок = клиентская область без заголовка).
param([int]$W = 1920, [int]$H = 1440, [int]$F = 0, [string]$Tag = "keys",
      [string]$Sub = "ld", [int]$MenuSec = 6, [double]$LoadX = 350, [double]$LoadY = 107,
      [double]$SlotX = 280, [double]$SlotY = 161, [int]$LoadWait = 10,
      [int]$KeyGapMs = 400, [int]$ShotSec = 2, [string]$Steps = "S;Esc,R;Esc,E;Esc,G;Esc,Esc,F1;Esc,Esc,Tab;Tab",
      [double]$CursorX = -1, [double]$CursorY = -1,
      [switch]$Strip, [switch]$NoKill, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class K {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
 [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int i, int v);
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
[void][K]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"hd.cfg -> $line"

$hdlog = "$GameRoot\hdlog.txt"
"KEYSHOT $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [K+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][K]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object K+RECT; [void][K]::GetWindowRect($h,[ref]$rec)
      if ([K]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][K]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [K]::EnumWindows($cb,[IntPtr]::Zero)
  foreach ($w in $found) { "WIN title='$($w.Title)' rect $($w.L),$($w.T) $($w.W)x$($w.H2)" | Add-Content -Path $hdlog -Encoding ASCII }
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Strip-Frame($main) {
  # снимает рамку (caption/sysmenu/thickframe/min-max) и ставит окно на (0,0) W x H
  $s = [K]::GetWindowLong($main.H, -16)
  $mask = 0x00C00000 -bor 0x00080000 -bor 0x04000000 -bor 0x00020000 -bor 0x00010000 -bor 0x20000000
  $s2 = $s -band (-not $mask)
  [void][K]::SetWindowLong($main.H, -16, $s2)
  [void][K]::SetWindowPos($main.H, [IntPtr]::new(-1), 0, 0, $W, $H, 0x20 -bor 0x40 -bor 0x10)
  "strip: style $s -> $s2" | Add-Content -Path $hdlog -Encoding ASCII
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
  [K]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [K]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function VK($name) {
  switch ($name) {
    'Esc' { 27 }
    'Tab' { 9 }
    'F1' { 111 }
    default { [byte][char]($name.ToUpper()) }
  }
}

function Send-Key($vk) {
  $sc = [byte][K]::MapVirtualKey($vk, 0)
  [K]::keybd_event($vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [K]::keybd_event($vk, $sc, 2, [IntPtr]::Zero)
  Start-Sleep -Milliseconds $KeyGapMs
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
if ($Strip) { Strip-Frame $main } else { [void][K]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3) }
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII

Shot $main "$ShotDir\$($Tag)_menu.png"

# --- загрузка сейва кликами (путь пользователя: глиф -> строка сейва -> Enter) ---
[void][K]::SetForegroundWindow($main.H)
$a = To-Abs $main $LoadX $LoadY
"glyph canvas ($LoadX,$LoadY) -> abs ($($a[0]),$($a[1]))" | Add-Content -Path $hdlog -Encoding ASCII
Click $a[0] $a[1]
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_list.png"

$b = To-Abs $main $SlotX $SlotY
"slot canvas ($SlotX,$SlotY) -> abs ($($b[0]),$($b[1]))" | Add-Content -Path $hdlog -Encoding ASCII
Click $b[0] $b[1]
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_sel.png"

# Enter (подтвердить загрузку)
Send-Key 13
Start-Sleep -Seconds $LoadWait
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 600
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_overland.png"

# --- шаги: клавиши и клики по canvas-координатам (токен x<canvasX>:<canvasY>) ---
$i = 0
foreach ($step in $Steps.Split(';')) {
  $i++
  [void][K]::SetForegroundWindow($main.H)
  Start-Sleep -Milliseconds 250
  if ($CursorX -ge 0 -and $CursorY -ge 0) {
    $c = To-Abs $main $CursorX $CursorY
    [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($c[0], $c[1])
    Start-Sleep -Milliseconds 300
  }
  $acts = @()
  foreach ($kn in $step.Split(',')) {
    $tk = $kn.Trim()
    if ($tk -match '^x(-?[0-9.]+):(-?[0-9.]+)$') {
      $cx = [double]$Matches[1]; $cy = [double]$Matches[2]
      $c = To-Abs $main $cx $cy
      [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($c[0], $c[1])
      Start-Sleep -Milliseconds 300
      Click $c[0] $c[1]
      Start-Sleep -Milliseconds 400
    }
    else {
      $vk = VK $tk
      $acts += $vk
      Send-Key $vk
    }
  }
  "STEP $i '$step' vk=$($acts -join ',')" | Add-Content -Path $hdlog -Encoding ASCII
  Start-Sleep -Seconds $ShotSec
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_step$i.png"
}

if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  "game closed pid=$($p.Id)"
} else {
  "game left running pid=$($p.Id)"
}
