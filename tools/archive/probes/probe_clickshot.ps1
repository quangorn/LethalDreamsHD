# probe_clickshot.ps1 - для запущенного окна игры: навести OS-курсор (ax,ay), кликнуть ЛКМ, снять окно.
# Съёмка DPI-aware, только окно; курсор перед съёмкой уводится в угол.
param([int]$Pid_ = 0, [int]$Ax = 2378, [int]$Ay = 1845, [string]$Tag = "click", [string]$Sub = "ld", [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class C {
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
[void][C]::SetProcessDpiAwareness(2)

$target = $Pid_
if (-not $target) {
  $p = Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { "нет процесса LETHALDREAMS"; exit 1 }
  $target = $p.Id
}

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [C+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][C]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object C+RECT; [void][C]::GetWindowRect($h,[ref]$rec)
      if ([C]::IsWindowVisible($h)) {
        [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [C]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  "saved $name ($($main.W)x$($main.H2))"
}

$main = Find-Main $target
if (-not $main) { "окно не найдено (pid=$target)"; exit 1 }
[void][C]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300

[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($Ax, $Ay)
Start-Sleep -Milliseconds 900
Shot $main "$ShotDir\$($Tag)_pre.png"

[C]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 80
[C]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 1500

$main = Find-Main $target
Shot $main "$ShotDir\$($Tag)_dlg.png"

[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
Start-Sleep -Milliseconds 600
$main = Find-Main $target
Shot $main "$ShotDir\$($Tag)_dlg_nocursor.png"
"clicked ($Ax,$Ay) pid=$target"
