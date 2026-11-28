# Зонд сигнатуры отказа: запуск игры из дерева, путь новой игры (глиф -> скип ->
# глиф персонажа -> Enter), съёмка окна на каждом шаге, и ГЛАВНОЕ — фиксация
# способа завершения процесса: ExitCode процесса, событие Application Error /
# Application Hang в журнале Application (faulting module + RVA), отчёт WER на диске.
# Окно снимается ТОЛЬКО окном, съёмка DPI-aware; игра гасится сразу после съёмки.
#
#   probe_exitcode.ps1 <W> <H> <F> <Tag> [-Sub ld] [-Mode new]
#       [-MenuSec 6 -SkipClicks 2 -CharX 349 -CharY 658 -EnterCount 2 -LoadWait 8]
#   LD_GAME=<дерево> LD_REPO=<repo> LD_SCRATCH=<scratch>; снимки и `<Tag>_exit.txt`
#   в <scratch>/<Sub>/, хвост hdlog — `<Tag>_hdlog.txt`.
param([int]$W = 3840, [int]$H = 2160, [int]$F = 1, [string]$Tag = "exitcode",
      [string]$Sub = "ld", [int]$MenuSec = 6, [double]$NewX = 200, [double]$NewY = 400,
      [int]$SkipClicks = 2, [double]$SkipX = 512, [double]$SkipY = 384,
      [double]$CharX = 349, [double]$CharY = 658, [int]$EnterCount = 2,
      [int]$LoadWait = 8, [int]$WerWait = 20,
      [switch]$NoKill, [string]$ScratchRoot = $env:LD_SCRATCH,
      [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }
if (-not $env:LDHOOK_LOG) { $env:LDHOOK_LOG = '1' }   # гейт сайтов (install_site*/FIRED) пишется при уровне ≥ 1;
                                                       # числовая трассировка site5/site6 — только LDHOOK_LOG=2

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }
$ExitFile = "$ShotDir\$($Tag)_exit.txt"
$hdlog = "$GameRoot\hdlog.txt"

function Log([string]$s) {
  Write-Output $s
  Add-Content -Path $ExitFile -Value $s
}

function Copy-HdLog {
  if (Test-Path $hdlog) {
    Copy-Item $hdlog "$ShotDir\$($Tag)_hdlog.txt" -Force
    Log ("hdlog copied ($((Get-Content $hdlog).Count) lines)")
  }
}

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class X {
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
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][X]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"EXITCODEPROBE $Tag start $(Get-Date -Format 'HH:mm:ss') $line tree=$GameRoot" | Set-Content -Path $ExitFile -Encoding ASCII

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [X+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][X]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object X+RECT; [void][X]::GetWindowRect($h,[ref]$rec)
      if ([X]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][X]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [X]::EnumWindows($cb,[IntPtr]::Zero)
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  Log "saved $name ($($main.W)x$($main.H2))"
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
  [X]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [X]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Enter {
  [X]::keybd_event(0x0D,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [X]::keybd_event(0x0D,0,2,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

#ExitCode процесса (после завершения) + смысл кода
function Exit-Line([object]$p) {
  $code = $p.ExitCode
  $u = ([long]$code) -band 0xFFFFFFFF
  $hex = "{0:X8}" -f $u
  $mean = ''
  switch ($hex) {
    "00000000" { $mean = 'clean exit(0)' }
    "C0000005" { $mean = 'ACCESS_VIOLATION' }
    "C000041D" { $mean = 'FATAL_USER_EXCEPTION (unhandled SEH)' }
    "C0000409" { $mean = 'STACK_BUFFER_OVERRUN' }
    "C0000006" { $mean = 'PRIVILEGED_INSTRUCTION' }
    "80070005" { $mean = 'E_ACCESSDENIED' }
    default { $mean = 'code' }
  }
  return "EXITED code=0x$hex dec=$code $mean pid=$($p.Id)"
}

function Check-Exit([string]$step, [object]$p) {
  $p.Refresh()
  if ($p.HasExited) {
    Log ("at step '$step': " + (Exit-Line $p))
    return $true
  }
  return $false
}

# События журнала Application (faulting module/RVA) и отчёты WER на диске.
function Dump-Crash([datetime]$since, [string]$app) {
  Log "--- Application log events since $since ---"
  $events = @()
  try {
    $events = Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$since} `
             -ErrorAction SilentlyContinue | Where-Object { $_.Message -match $app }
  } catch { Log "Get-WinEvent: $($_.Exception.Message)" }
  foreach ($e in $events) {
    $tg = if ($e.TimeGenerated) { $e.TimeGenerated.ToString('HH:mm:ss') } else { 'n/a' }
    $pv = if ($e.ProviderName) { $e.ProviderName } else { 'n/a' }
    Log ("EVENT [" + $tg + "] provider=" + $pv + " id=" + $e.Id +
         " level=" + ($(if ($e.LevelDisplayName) { $e.LevelDisplayName } else { 'n/a' })))
    Log ($e.Message -replace "`r`n", ' | ')
  }
  if (@($events).Count -eq 0) { Log "no Application-log events for $app" }
  Log "--- WER report dirs ---"
  foreach ($d in @('C:/ProgramData/Microsoft/Windows/WER/ReportArchive',
                   'C:/ProgramData/Microsoft/Windows/WER/ReportQueue',
                   'C:/ProgramData/Microsoft/Windows/WER')) {
    $hits = Get-ChildItem -Path $d -Recurse -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -match 'LETHALDREAMS' }
    foreach ($f in $hits) {
      Log ("WER " + $f.FullName + " mtime=" + $f.LastWriteTime.ToString('HH:mm:ss'))
    }
  }
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$since = Get-Date
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Log ("launched pid=" + $p.Id + " tree=" + $dir + " hd.cfg=" + $line)

Start-Sleep -Seconds $MenuSec
if (Check-Exit 'menu' $p) { Dump-Crash $since 'LETHALDREAMS'; Copy-HdLog; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { Log "NO WINDOW"; exit 1 }
[void][X]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
Log "window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F"
Shot $main "$ShotDir\$($Tag)_menu.png"

# путь новой игры: глиф «Вообразить» -> скип ролика -> глиф персонажа -> Enter
[void][X]::SetForegroundWindow($main.H)
$a = To-Abs $main $NewX $NewY
Click $a[0] $a[1]
if (Check-Exit 'newglyph' $p) { Dump-Crash $since 'LETHALDREAMS'; Copy-HdLog; exit 1 }
Shot $main "$ShotDir\$($Tag)_list.png"

for ($i = 1; $i -le $SkipClicks; $i++) {
  $c = To-Abs $main $SkipX $SkipY
  Click $c[0] $c[1]
}
if (Check-Exit 'skip' $p) { Dump-Crash $since 'LETHALDREAMS'; Copy-HdLog; exit 1 }
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_skip.png"

$b = To-Abs $main $CharX $CharY
Click $b[0] $b[1]
if (Check-Exit 'charsel' $p) { Dump-Crash $since 'LETHALDREAMS'; Copy-HdLog; exit 1 }
$main = Find-Main $p.Id
Shot $main "$ShotDir\$($Tag)_sel.png"

for ($i = 1; $i -le $EnterCount; $i++) { Enter }
Log "ENTER x$EnterCount sent; polling LoadWait=$LoadWait"
# опрос: как только процесс завершился — фиксируем код; окно снимаем и при жизни
$exited = $false
$deadline = (Get-Date).AddSeconds($LoadWait)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 1000
  if (Check-Exit 'load' $p) { $exited = $true; break }
}
if (-not $exited) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  if ($main) { Shot $main "$ShotDir\$($Tag)_overland.png" } else { Log "no window after load" }
}

# WER-событие пишется с задержкой; ждём WerWait секунд и печатаем всё найденное.
$deadline2 = (Get-Date).AddSeconds($WerWait)
while ((Get-Date) -lt $deadline2) {
  $evs = Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$since} `
        -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'LETHALDREAMS' }
  if (@($evs).Count -gt 0) { break }
  if (-not $exited) { break }
  Start-Sleep -Milliseconds 1500
}
Dump-Crash $since 'LETHALDREAMS'
Copy-HdLog
if (-not $exited -and -not $NoKill) {
  Stop-Process -Id $p.Id -Force
  Log "game closed pid=$($p.Id)"
} elseif ($exited) {
  Log "process already exited (code above), nothing to kill"
}
