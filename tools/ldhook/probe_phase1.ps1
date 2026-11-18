# Фаза 1 (PLAN_proxy_dll.md §4): прогон ldhook.pyd (site1 hook) на scratch-дереве.
# Дойти до экрана, где вызывается FUN_0041cc90 (4x-шрифт): «Новая игра» -> экран
# выбора персонажа. Проверить hdlog.txt на "hook FIRED", снять окно.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/ldhook/probe_phase1.ps1 `
#     -GameRoot "<scratch-tree>" -Tag "phase1_ng"
# Фаза 3: ldhook пишет в общий hdlog.txt (там же, где Python-патчи) с строками
# "ldhook: HH:MM:SS.mmm t<tid> I|D ..."; уровень — env LDHOOK_LOG (0/1/2).
param([string]$GameRoot = $env:LD_GAME, [string]$Tag = "phase1",
      [double]$CanvasX = 200, [double]$CanvasY = 400,
      [int]$MenuSec = 8, [int]$AfterSec = 16)
if (-not $GameRoot) { throw "укажите -GameRoot <scratch-tree> (или LD_GAME)" }
if (-not (Test-Path (Join-Path $GameRoot "LETHALDREAMS.EXE"))) { throw "нет LETHALDREAMS.EXE: $GameRoot" }

$ShotDir = "$GameRoot\shots"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }
$LogFile = "$ShotDir\$Tag.log"
"PHASE1 $Tag start $(Get-Date -Format 'HH:mm:ss') GameRoot=$GameRoot" | Set-Content -Path $LogFile -Encoding ASCII

Add-Type @"
using System;using System.Runtime.InteropServices;
public class P1 {
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][P1]::SetProcessDpiAwareness(2)
Add-Type -AssemblyName System.Drawing,System.Windows.Forms

function Log($m) { "$m" | Add-Content -Path $LogFile -Encoding ASCII; Write-Host $m }
function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [P1+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][P1]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_ -and [P1]::IsWindowVisible($h)) {
      $rec = New-Object P1+RECT; [void][P1]::GetWindowRect($h,[ref]$rec)
      [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
    return $true
  }
  [P1]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}
function Shot($main, $name) {
  if (-not $main) { return }
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name); $g.Dispose(); $bmp.Dispose()
  Log "saved $name ($($main.W)x$($main.H2))"
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$p = Start-Process (Join-Path $GameRoot "LETHALDREAMS.EXE") -WorkingDirectory $GameRoot -PassThru
Log "launched pid=$($p.Id)"
Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { Log ("EXITED code=0x{0:X}" -f $p.ExitCode); exit 1 }
$main = Find-Main $p.Id
if (-not $main) { Log "NO WINDOW"; Stop-Process -Id $p.Id -Force; exit 1 }
[void][P1]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
Log "window $($main.W)x$($main.H2) at ($($main.L),$($main.T))"

$k = $main.H2 * 1024.0 / ($main.W * 768.0)
$DX = 512.0 - 512.0 * $k
$sy = $main.H2 / 768.0
$ax = [int]($main.L + $sy * $CanvasX + $DX * ($main.W / 1024.0))
$ay = [int]($main.T + $sy * $CanvasY)
Log "canvas ($CanvasX,$CanvasY) -> abs ($ax,$ay)  k=$k"
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($ax, $ay)
Start-Sleep -Milliseconds 800
[P1]::mouse_event(0x2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 80; [P1]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
Log "clicked newgame ($ax,$ay)"
# интро-видео скипается двумя кликами
Start-Sleep -Seconds 4
[P1]::mouse_event(0x2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 90; [P1]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 700
[P1]::mouse_event(0x2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 90; [P1]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
Log "clicked skip x2"
Start-Sleep -Seconds $AfterSec
$p.Refresh()
if ($p.HasExited) { Log "EXITED after nav"; exit 1 }
$main = Find-Main $p.Id
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 500
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_newgame.png"

$ll = Join-Path $GameRoot "hdlog.txt"
if (Test-Path $ll) {
  $c = (Get-Content $ll -Raw)
  Log "--- hdlog.txt (ldhook lines) ---"
  ($c -split "`n") | Where-Object { $_ -match 'ldhook:' } | ForEach-Object { Log $_ }
  if ($c -match "FIRED") { Log "GATE: site1 hook FIRED" } else { Log "GATE: hook did NOT fire (timing/scope)" }
} else { Log "no hdlog.txt" }

Stop-Process -Id $p.Id -Force
Log "game closed pid=$($p.Id)"