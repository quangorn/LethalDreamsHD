# probe_load.ps1 — загрузка сейва вместо новой игры.
# Запускает игру, кликает "Recall" (Load Game), выбирает слот, ждёт загрузки.
#
#   probe_load.ps1 <W> <H> <F> <Tag> [-Sub ld] [-LoadX 200 -LoadY 300]
#       [-SlotX 349 -SlotY 400 -Slot 1] [-LoadWait 15]
#   LD_GAME/LD_REPO/LD_SCRATCH задаются.
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "loadtest",
      [string]$Sub = "ld", [double]$LoadX = 200, [double]$LoadY = 300,
      [double]$SlotX = 349, [double]$SlotY = 400, [int]$Slot = 1,
      [int]$LoadWait = 15, [int]$MenuSec = 6,
      [switch]$NoKill, [string]$ScratchRoot = $env:LD_SCRATCH,
      [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class L {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
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
[void][L]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII

$hdlog = "$GameRoot\hdlog.txt"
"LOADTEST $Tag start $(Get-Date -Format 'HH:mm:ss') $line" | Set-Content -Path $hdlog -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [L+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][L]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object L+RECT; [void][L]::GetWindowRect($h,[ref]$rec)
      if ([L]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][L]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [L]::EnumWindows($cb,[IntPtr]::Zero)
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  "saved $name ($($main.W)x$($main.H2))" | Add-Content -Path $hdlog -Encoding ASCII
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
  [L]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [L]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Enter {
  $vk = 13; $sc = [byte][L]::MapVirtualKey($vk, 0)
  [L]::keybd_event($vk, $sc, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [L]::keybd_event($vk, $sc, 2, [IntPtr]::Zero)
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
[void][L]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII
[void][L]::SetForegroundWindow($main.H)
Start-Sleep -Milliseconds 500
Shot $main "$ShotDir\$($Tag)_menu.png"

# --- Клик "Recall" (Load Game) ---
$a = To-Abs $main $LoadX $LoadY
"CLICK Load canvas $LoadX,$LoadY -> abs ($($a[0]),$($a[1]))" | Add-Content -Path $hdlog -Encoding ASCII
Click $a[0] $a[1]
Start-Sleep -Seconds 2
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_loadmenu.png"

# --- Выбор слота (Enter для подтверждения) ---
"ENTER slot=$Slot" | Add-Content -Path $hdlog -Encoding ASCII
$b = To-Abs $main $SlotX $SlotY
"CLICK slot canvas $SlotX,$SlotY -> abs ($($b[0]),$($b[1]))" | Add-Content -Path $hdlog -Encoding ASCII
Click $b[0] $b[1]
Start-Sleep -Seconds 1
Enter
Start-Sleep -Seconds 1
Enter
"LOAD wait=$LoadWait" | Add-Content -Path $hdlog -Encoding ASCII

# --- Ждём загрузки ---
$deadline = (Get-Date).AddSeconds($LoadWait)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 1000
  $p.Refresh()
  if ($p.HasExited) { "EXITED during load code=0x{0:X}" -f $p.ExitCode; break }
}
if (-not $p.HasExited) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  if ($main) { Shot $main "$ShotDir\$($Tag)_loaded.png" }
}

# --- Движение к бою: клики по тайлам ---
$BattleWait = 120
$Clicks = $env:LD_CLICKS
if (-not $Clicks) { $Clicks = "512:384;512:300;600:384;512:200;400:384" }
$si = 0
foreach ($cl in $Clicks.Split(';')) {
  if (-not $cl) { continue }
  $si++
  [void][L]::SetForegroundWindow($main.H)
  $c = To-Abs $main ([double]($cl.Split(':')[0])) ([double]($cl.Split(':')[1]))
  "CLICK $si canvas $cl -> abs ($($c[0]),$($c[1]))" | Add-Content -Path $hdlog -Encoding ASCII
  Click $c[0] $c[1]
  Start-Sleep -Seconds 5
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_move$si.png"
}

# --- Ждём "SCENE battle" в hdlog ---
$deadline = (Get-Date).AddSeconds($BattleWait)
$seen = $false
while ((Get-Date) -lt $deadline) {
  $t = Get-Content $hdlog -ErrorAction SilentlyContinue
  if ($t -and ($t | Select-String -Pattern 'SCENE battle' -Quiet)) { $seen = $true; break }
  Start-Sleep -Milliseconds 700
}
"battle seen=$seen (waited)" | Add-Content -Path $hdlog -Encoding ASCII
if ($seen) {
  Start-Sleep -Seconds 4
  $main = Find-Main $p.Id
  if ($main) { Shot $main "$ShotDir\$($Tag)_battle.png" }
}

if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
  "game closed"
} else {
  "game left running pid=$($p.Id)"
}