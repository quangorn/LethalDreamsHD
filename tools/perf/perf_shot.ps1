# perf_shot.ps1 — screenshot ONLY the game window (DPI-aware), no interaction
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File perf_shot.ps1 -Out <png> [-Pid <pid>]
# Out defaults to $env:LD_SCRATCH\perf\shot.png (the project scratch dir).
param([string]$Out, [int]$Pid_ = 0)
if (-not $Out) { $Out = (Join-Path $env:LD_SCRATCH 'perf\shot.png') }
$ErrorActionPreference = 'Stop'
if ($Pid_ -le 0) { $Pid_ = (Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1).Id }
if (-not $Pid_) { Write-Host 'LETHALDREAMS process not found'; exit 1 }

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class W {
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder buf, int max);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][W]::SetProcessDpiAwareness(2)
$found = New-Object System.Collections.ArrayList
$cb = [W+EnumWindowsProc]{ param($h,$p2)
  $procId = 0; [void][W]::GetWindowThreadProcessId($h,[ref]$procId)
  if ($procId -eq $Pid_) {
    $rec = New-Object W+RECT; [void][W]::GetWindowRect($h,[ref]$rec)
    if ([W]::IsWindowVisible($h)) {
      $sb = New-Object System.Text.StringBuilder 260
      [void][W]::GetWindowText($h, $sb, 260)
      [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
  }
  return $true
}
[W]::EnumWindows($cb,[IntPtr]::Zero)
$named = @($found | Where-Object { $_.Title -ne '' })
if ($named.Count -gt 0) { $main = $named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1 }
else { $main = $found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1 }
if (-not $main) { Write-Host 'NO WINDOW'; exit 1 }
Write-Host ("window '{0}' {1}x{2} at ({3},{4})" -f $main.Title, $main.W, $main.H2, $main.L, $main.T)
$bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
$bmp.Save($Out)
Write-Host "saved $Out"