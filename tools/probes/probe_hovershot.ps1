# Съёмка ТУЛТИПОВ оверленда: загрузка сейва кликами, затем НАВЕДЕНИЕ курсора на
# канвасные точки (без клика) и снимок ОКНА игры после выдержки.
# Путь: глиф «Вспомнить» -> строка сейва -> Enter -> оверленд; далее шаги.
# Шаги — список строк через ';'; в строке токены:
#   h<canvasX>:<canvasY>  - навести курсор на канвасную точку (hover, БЕЗ клика),
#                           выдержка SettleMs, затем ShotSec до снимка;
#   c<canvasX>:<canvasY>  - клик по канвасной точке;
#   клавиша (Esc/Tab/F1/буква) - keybd_event.
# Снимается ТОЛЬКО окно игры (DPI-aware). -Strip снимает рамку окна (4:3-окно = клиент).
# Игра гасится после последнего снимка (-NoKill оставляет).
param([int]$W = 1920, [int]$H = 1440, [int]$F = 0, [string]$Tag = "tthover",
      [string]$Sub = "ld", [int]$MenuSec = 6, [double]$LoadX = 350, [double]$LoadY = 107,
      [double]$SlotX = 280, [double]$SlotY = 161, [int]$LoadWait = 10,
      [string]$Steps = "h61:596", [int]$SettleMs = 500, [int]$ShotSec = 2,
      [switch]$Strip, [switch]$NoKill, [switch]$NoLoad,
      [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class H {
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
[void][H]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"hd.cfg -> $line"

$hdlog = "$GameRoot\hdlog.txt"
"HOVERSHOT $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [H+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][H]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object H+RECT; [void][H]::GetWindowRect($h,[ref]$rec)
      if ([H]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][H]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [H]::EnumWindows($cb,[IntPtr]::Zero)
  foreach ($w in $found) { "WIN title='$($w.Title)' rect $($w.L),$($w.T) $($w.W)x$($w.H2)" | Add-Content -Path $hdlog -Encoding ASCII }
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Strip-Frame($main) {
  $s = [H]::GetWindowLong($main.H, -16)
  $mask = 0x00C00000 -bor 0x00080000 -bor 0x04000000 -bor 0x00020000 -bor 0x00010000 -bor 0x20000000
  $s2 = $s -band (-not $mask)
  [void][H]::SetWindowLong($main.H, -16, $s2)
  [void][H]::SetWindowPos($main.H, [IntPtr]::new(-1), 0, 0, $W, $H, 0x20 -bor 0x40 -bor 0x10)
  "strip: style $s -> $s2" | Add-Content -Path $hdlog -Encoding ASCII
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

function Move-To($main, $cx, $cy) {
  $c = To-Abs $main $cx $cy
  "hover canvas ($cx,$cy) -> abs ($($c[0]),$($c[1]))" | Add-Content -Path $hdlog -Encoding ASCII
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($c[0], $c[1])
  Start-Sleep -Milliseconds $SettleMs
}

function Click($x, $y) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
  Start-Sleep -Milliseconds 400
  [H]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [H]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function VK($name) {
  switch ($name) {
    'Esc' { 27 }
    'Tab' { 9 }
    'F1' { 111 }
    'Enter' { 13 }
    default { [byte][char]($name.ToUpper()) }
  }
}

function Send-Key($vk) {
  $sc = [byte][H]::MapVirtualKey($vk, 0)
  [H]::keybd_event($vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [H]::keybd_event($vk, $sc, 2, [IntPtr]::Zero)
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
if ($Strip) { Strip-Frame $main } else { [void][H]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3) }
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII

Shot $main "$ShotDir\$($Tag)_menu.png"

if (-not $NoLoad) {
  # --- загрузка сейва кликами (глиф «Вспомнить» -> строка сейва -> Enter) ---
  [void][H]::SetForegroundWindow($main.H)
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

  Send-Key 13
  Start-Sleep -Seconds $LoadWait
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_overland.png"
}

# --- шаги: наведение/клики/клавиши, снимок окна в конце каждого шага ---
$i = 0
foreach ($step in $Steps.Split(';')) {
  $i++
  [void][H]::SetForegroundWindow($main.H)
  Start-Sleep -Milliseconds 250
  foreach ($tk0 in $step.Split(',')) {
    $tk = $tk0.Trim()
    if ($tk -match '^h(-?[0-9.]+):(-?[0-9.]+)$') {
      Move-To $main ([double]$Matches[1]) ([double]$Matches[2])
    }
    elseif ($tk -match '^c(-?[0-9.]+):(-?[0-9.]+)$') {
      $c = To-Abs $main ([double]$Matches[1]) ([double]$Matches[2])
      Click $c[0] $c[1]
    }
    elseif ($tk) {
      Send-Key (VK $tk)
    }
  }
  "STEP $i '$step'" | Add-Content -Path $hdlog -Encoding ASCII
  Start-Sleep -Seconds $ShotSec
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_step$i.png"
}

# правила съёмки: игра гасится сразу после последнего снимка
if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  "game closed pid=$($p.Id)"
} else {
  "game left running pid=$($p.Id)"
}
