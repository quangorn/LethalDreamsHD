# Iteration 1 run: launch, wait for menu, move OS cursor to known window-relative
# points (logging them to hdlog.txt between the python probe lines), capture shots.
param([int]$MenuSec = 5, [string]$Tag = "p1", [string]$Sub = "ld", [switch]$Borderless, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class W2 {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
 [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int i, int v);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][W2]::SetProcessDpiAwareness(2)

$hdlog = "$GameRoot\hdlog.txt"
"RUN $Tag start $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $hdlog -Encoding ASCII

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [W2+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][W2]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object W2+RECT; [void][W2]::GetWindowRect($h,[ref]$rec)
      if ([W2]::IsWindowVisible($h)) {
        [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [W2]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW"; exit 1 }
if ($Borderless) {
  # true windowed -> borderless: снять рамку, topmost, 0,0, весь desktop; modeset не трогается
  $s = [W2]::GetWindowLong($main.H, -16)
  $mask = 0x00C00000 -bor 0x00080000 -bor 0x04000000 -bor 0x00020000 -bor 0x00010000 -bor 0x20000000
  $s2 = $s -band (-not $mask)
  [void][W2]::SetWindowLong($main.H, -16, $s2)
  [void][W2]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 3840, 2160, 0x20 -bor 0x40 -bor 0x10)
  Start-Sleep -Milliseconds 300
  $main = Find-Main $p.Id
  "borderless: style $s -> $s2" | Add-Content -Path $hdlog -Encoding ASCII
}
[void][W2]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
"window size=$($main.W)x$($main.H2) at ($($main.L),$($main.T))" | Add-Content -Path $hdlog -Encoding ASCII

# window-relative fractions -> OS cursor positions
$fracs = @(@(0.10,0.10), @(0.25,0.30), @(0.50,0.50), @(0.75,0.60), @(0.90,0.85), @(0.40,0.95))
foreach ($f in $fracs) {
  $ax = [int]($main.L + $main.W * $f[0])
  $ay = [int]($main.T + $main.H2 * $f[1])
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($ax, $ay)
  "OSPOS rel=($($f[0]),$($f[1])) abs=($ax,$ay)" | Add-Content -Path $hdlog -Encoding ASCII
  Start-Sleep -Milliseconds 1200
}

# перед съёмкой: курсор ОС убираем за пределы окна, окно игры поднимаем topmost
# (SWP_NOMOVE|SWP_NOSIZE = 3, WM_SIZE не генерируется), ждём стабилизации кадра
$main = Find-Main $p.Id
if ($main) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 300
  [void][W2]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
  Start-Sleep -Milliseconds 500
}
$main = Find-Main $p.Id
if ($main) {
  # только окно, полный рабочий стол не снимаем (указание пользователя)
  $fname = "$ShotDir\{0}_win.png" -f $Tag
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($fname)
  "saved " + $fname
}
Start-Sleep -Seconds 1
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode } else { "running; killing" ; Stop-Process -Id $p.Id -Force }
