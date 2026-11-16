# Дойти до экрана выбора персонажа (newgamemenu): клик по кнопке «Новая игра» в главном меню
# (COORDS_MM_NewGame[0] = [136,272,128,256] -> центр канваса (200,400)), ждать интро-видео,
# снять окно. hd.cfg пишется как в probe_dlgshot.ps1.
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "ng",
      [string]$Sub = "ld", [double]$CanvasX = 200, [double]$CanvasY = 400,
      [int]$MenuSec = 8, [int]$AfterSec = 16, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class R2 {
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
[void][R2]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"hd.cfg -> $line"

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [R2+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][R2]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object R2+RECT; [void][R2]::GetWindowRect($h,[ref]$rec)
      if ([R2]::IsWindowVisible($h)) {
        [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [R2]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}
function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name); "saved $name ($($main.W)x$($main.H2))"
}

$hdbb = "$GameRoot\hdbb.txt"
if (Test-Path $hdbb) { Remove-Item $hdbb }

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED"; exit 1 }
$main = Find-Main $p.Id
[void][R2]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id

$k = $main.H2 * 1024.0 / ($main.W * 768.0)
$DX = 512.0 - 512.0 * $k
$sy = $main.H2 / 768.0
$ax = [int]($main.L + $sy * $CanvasX + $DX * ($main.W / 1024.0))
$ay = [int]($main.T + $sy * $CanvasY)
"canvas ($CanvasX,$CanvasY) -> abs ($ax,$ay)"
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($ax, $ay)
Start-Sleep -Milliseconds 800
[R2]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 80
[R2]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
"clicked newgame ($ax,$ay)"
# интро-видео скипается ДВУМЯ кликами (подсказка пользователя)
Start-Sleep -Seconds 4
[R2]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 90
[R2]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 700
[R2]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 90
[R2]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
"clicked skip x2"
Start-Sleep -Seconds $AfterSec
$main = Find-Main $p.Id
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 500
Shot $main "$ShotDir\$($Tag)_newgame.png"
"game left running pid=$($p.Id)"
if (Test-Path $hdbb) { "hdbb lines: " + (Get-Content $hdbb | Measure-Object -Line).Lines }
