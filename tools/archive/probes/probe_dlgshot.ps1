# Съёмка диалога настроек в любом режиме: пишет hd.cfg, запускает игру, наводит OS-курсор
# на глиф «Изменить» (COORDS_MM_Options, канвасный центр 675,660), кликает, снимает окно.
#
# Пересчёт канвас -> экран (из hd_scale, подтверждён замерами итераций):
#   k = h*1024/(w*768), DX = 512*(1-k); квант X = w/1024 (обеим режимам), квант Y контента = h/768;
#   экран_x = (h/768)*Cx + DX*(w/1024), экран_y = (h/768)*Cy.
#   (окно: sx=w/1024, EY=y -> экран sy*y; fullscreen: s=w/1024, EY=k*y -> экран s*k*y = h/768*y;
#    мышь: Mx=(phys/sx-DX)/k, My=(fullscreen: phys/s/k, окно: phys/sy) - симметрично тому же。)
# Съёмка DPI-aware, снимается ТОЛЬКО окно игры. Игра остаётся запущенной.
param([int]$W = 1280, [int]$H = 960, [int]$F = 0, [string]$Tag = "dlg",
      [string]$Sub = "ld", [double]$CanvasX = 675, [double]$CanvasY = 660,
      [int]$MenuSec = 6, [switch]$NoClick, [switch]$NoCfg, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

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

$hdcfg = "1280 960 32 0"
if (-not $NoCfg) {
  $line = "{0} {1} 32 {2}" -f $W, $H, $F
  Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
  Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
  "hd.cfg -> $line"
}

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
"DLGSHOT $Tag start $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $hdlog -Encoding ASCII

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

$k = $main.H2 * 1024.0 / ($main.W * 768.0)
$DX = 512.0 - 512.0 * $k
$sy = $main.H2 / 768.0
$ax = [int]($main.L + $sy * $CanvasX + $DX * ($main.W / 1024.0))
$ay = [int]($main.T + $sy * $CanvasY)
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) k=$k DX=$DX sy=$sy F=$F" | Add-Content -Path $hdlog -Encoding ASCII
"canvas ($CanvasX,$CanvasY) -> abs ($ax,$ay)" | Add-Content -Path $hdlog -Encoding ASCII

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

  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_dialog_nocursor.png"
}
"game left running pid=$($p.Id)"
