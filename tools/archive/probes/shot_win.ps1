# Активировать окно игры и снять ТОЛЬКО окно по GetWindowRect (любой размер).
param([int]$Pid_ = 0, [string]$Tag = "actwin", [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class A {
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint f);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][A]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))
$found = New-Object System.Collections.ArrayList
$cb = [A+EnumWindowsProc]{ param($h,$p2)
  $procId = 0; [void][A]::GetWindowThreadProcessId($h,[ref]$procId)
  if ($procId -eq $Pid_) {
    $rec = New-Object A+RECT; [void][A]::GetWindowRect($h,[ref]$rec)
    if ([A]::IsWindowVisible($h)) { [void]$found.Add([pscustomobject]@{H=$h;L=$rec.L;T=$rec.T;W=($rec.R-$rec.L);H2=($rec.B-$rec.T)}) }
  }
  return $true
}
[A]::EnumWindows($cb,[IntPtr]::Zero)
$main = $found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1
if (-not $main) { "no window"; exit 1 }
[void][A]::ShowWindow($main.H, 9)
[void][A]::SetWindowPos($main.H, [IntPtr]::new(-1), $main.L, $main.T, $main.W, $main.H2, 3)
[void][A]::SetForegroundWindow($main.H)
Start-Sleep -Milliseconds 600
$bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
$bmp.Save("$ScratchRoot\ld\$($Tag)_win.png")
"saved $Tag ($($main.W)x$($main.H2)) at $($main.L),$($main.T)"
