# probe_shotwin.ps1 - снять ТОЛЬКО окно игры для запущенного процесса (поднять topmost перед съёмкой).
# Съёмка DPI-aware. Param: pid (если не задан - самый большой window процесса LETHALDREAMS).
param([int]$Pid_ = 0, [string]$Tag = "now", [string]$Sub = "ld", [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class S {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][S]::SetProcessDpiAwareness(2)

$target = $Pid_
if (-not $target) {
  $p = Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { "нет процесса LETHALDREAMS"; exit 1 }
  $target = $p.Id
}

$found = New-Object System.Collections.ArrayList
$cb = [S+EnumWindowsProc]{ param($h,$p2)
  $procId = 0; [void][S]::GetWindowThreadProcessId($h,[ref]$procId)
  if ($procId -eq $target) {
    $rec = New-Object S+RECT; [void][S]::GetWindowRect($h,[ref]$rec)
    if ([S]::IsWindowVisible($h)) {
      [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
  }
  return $true
}
[S]::EnumWindows($cb,[IntPtr]::Zero)
$main = $found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1
if (-not $main) { "окно не найдено (pid=$target)"; exit 1 }

# HWND_TOPMOST = 0, SWP_NOMOVE=2 | SWP_NOSIZE=1 (WM_SIZE не генерируется)
[void][S]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 400
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 300
$name = "$ShotDir\$($Tag)_win.png"
$bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
$bmp.Save($name)
"saved $name ($($main.W)x$($main.H2)) at ($($main.L),$($main.T)) pid=$target"
