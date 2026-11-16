# Контрольный замер ориентира: окно 1280x960 (k=1, обёртки hd_scale = identity).
# Наводит OS-курсор на глиф «Изменить» (COORDS_MM_Options, канвасный центр 675,660),
# снимает меню с лейблом, кликает ЛКМ, снимает появившееся окно настроек.
# Съёмка DPI-aware, снимается ТОЛЬКО окно игры. Игра остаётся запущенной.
param([int]$MenuSec = 6, [string]$Tag = "ref1280", [string]$Sub = "ld",
       [double]$CanvasX = 675, [double]$CanvasY = 660, [switch]$NoClick, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class R {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, IntPtr dwExtra);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][R]::SetProcessDpiAwareness(2)

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [R+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][R]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object R+RECT; [void][R]::GetWindowRect($h,[ref]$rec)
      if ([R]::IsWindowVisible($h)) {
        [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [R]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  "saved $name ($($main.W)x$($main.H2))"
}

$hdlog = "$GameRoot\hdlog.txt"
"REF start $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $hdlog -Encoding ASCII

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru

Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW"; exit 1 }
[void][R]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T))" | Add-Content -Path $hdlog -Encoding ASCII

# канвас -> экран: окно, per-axis (sx=w/1024, sy=h/768); при k=1 sx=sy
$sx = $main.W / 1024.0
$sy = $main.H2 / 768.0
$ax = [int]($main.L + $CanvasX * $sx)
$ay = [int]($main.T + $CanvasY * $sy)
"canvas ($CanvasX,$CanvasY) -> abs ($ax,$ay) sx=$sx sy=$sy" | Add-Content -Path $hdlog -Encoding ASCII

[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($ax, $ay)
Start-Sleep -Milliseconds 1200
Shot $main "$ShotDir\$($Tag)_hover.png"

if (-not $NoClick) {
  [R]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [R]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  "clicked abs ($ax,$ay)" | Add-Content -Path $hdlog -Encoding ASCII
  Start-Sleep -Milliseconds 1500
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_dialog.png"

  # курсор за пределы окна - снимок без игрового курсора поверх текста
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_dialog_nocursor.png"
}
"game left running pid=$($p.Id)"
