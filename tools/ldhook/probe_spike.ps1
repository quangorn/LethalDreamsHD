# Фаза 0-бис (PLAN_proxy_dll.md, §2A): запуск игры из scratch-дерева спайка,
# снимок ОКНА (только окно, DPI-aware) и гашение процесса.
# Гейт спайка: <GameRoot>\ldhook_marker.txt существует (initldhook执行) и окно
# игры отрисовалось (снимок не чёрный).
# Порядок важен: SetProcessDpiAwareness ДО загрузки System.Drawing (иначе WinForms
# кэширует логические координаты и снимок промахивается мимо окна).
# Окно: если свёрнуто — разворачиваем (SW_RESTORE); поверх всех (topmost);
# захват — пересечение прямоугольника окна с экраном (GetSystemMetrics, физические).
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/ldhook/probe_spike.ps1 -GameRoot "<scratch-tree>"
param([string]$GameRoot = $env:LD_SPIKE_ROOT, [int]$WaitSec = 10,
      [string]$Tag = "spike0b", [switch]$NoKill)
if (-not $GameRoot) { throw "укажите -GameRoot <scratch-дерево> (или LD_SPIKE_ROOT)" }
if (-not (Test-Path (Join-Path $GameRoot "LETHALDREAMS.EXE"))) { throw "нет LETHALDREAMS.EXE: $GameRoot" }

$ShotDir = "$GameRoot\shots"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }
$LogFile = "$ShotDir\$Tag.log"
"PROBE $Tag start $(Get-Date -Format 'HH:mm:ss') GameRoot=$GameRoot" | Set-Content -Path $LogFile -Encoding ASCII

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class H {
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder buf, int max);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][H]::SetProcessDpiAwareness(2)
Add-Type -AssemblyName System.Drawing

function Log($m) { "$m" | Add-Content -Path $LogFile -Encoding ASCII; Write-Host $m }

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
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  # экран в физических пикселях (процесс DPI-aware, awareness до System.Drawing)
  $sw = [H]::GetSystemMetrics(0); $sh = [H]::GetSystemMetrics(1)
  $l = [Math]::Max($main.L, 0); $t = [Math]::Max($main.T, 0)
  $r = [Math]::Min($main.L + $main.W, $sw); $b = [Math]::Min($main.T + $main.H2, $sh)
  $w = $r - $l; $h = $b - $t
  if ($w -le 0 -or $h -le 0) { Log "shot skipped: window off-screen (screen ${sw}x${sh})"; return }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($l, $t, 0, 0, (New-Object System.Drawing.Size $w, $h))
  $bmp.Save($name)
  $g.Dispose(); $bmp.Dispose()
  Log "saved $name ($w x $h from screen $l,$t; screen ${sw}x${sh})"
}

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

$p = Start-Process (Join-Path $GameRoot "LETHALDREAMS.EXE") -WorkingDirectory $GameRoot -PassThru
Log "launched pid=$($p.Id)"
Start-Sleep -Seconds $WaitSec
$p.Refresh()
if ($p.HasExited) { Log ("EXITED code=0x{0:X}" -f $p.ExitCode); exit 1 }

$main = Find-Main $p.Id
if (-not $main) { Log "NO WINDOW"; Stop-Process -Id $p.Id -Force; exit 1 }
Log "window found: $($main.W)x$($main.H2) at ($($main.L),$($main.T)) title='$($main.Title)'"

# развернуть, если свёрнуто; поднять наверх; дать кадру отрисоваться
if ([H]::IsIconic($main.H)) { [void][H]::ShowWindow($main.H, 9); Log "restored from minimized"; Start-Sleep -Seconds 2 }
[void][H]::SetWindowPos($main.H, [IntPtr]::new(-1), 0, 0, 0, 0, 0x01 -bor 0x02)  # HWND_TOP, NOSIZE|NOMOVE
[void][H]::SetForegroundWindow($main.H)
Start-Sleep -Seconds 2
$p.Refresh()
if ($p.HasExited) { Log ("EXITED code=0x{0:X}" -f $p.ExitCode); exit 1 }
$main = Find-Main $p.Id
if (-not $main) { Log "NO WINDOW after restore"; Stop-Process -Id $p.Id -Force; exit 1 }
Log "window final: $($main.W)x$($main.H2) at ($($main.L),$($main.T))"

Shot $main "$ShotDir\$($Tag)_menu.png"

$marker = Join-Path $GameRoot "ldhook_marker.txt"
$exists = Test-Path $marker
Log "MARKER exists=$exists path=$marker"
if ($exists) {
  $content = (Get-Content $marker -Raw)
  Log "MARKER content='$($content.Trim())'"
}

if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  Log "game closed pid=$($p.Id)"
} else {
  Log "game left running pid=$($p.Id)"
}
if ($exists) { exit 0 } else { exit 2 }