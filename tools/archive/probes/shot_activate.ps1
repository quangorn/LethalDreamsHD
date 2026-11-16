# Активировать окно игры, отправить клавиши, снять экран 0,0..3840x2160.
param([int]$Pid_ = 0, [string]$Tag = "act", [string]$Keys = "", [switch]$NoCursor, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
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
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
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
[void][A]::SetWindowPos($main.H, [IntPtr]::new(-1), 0,0,0,0, 3)
[void][A]::SetForegroundWindow($main.H)
Start-Sleep -Milliseconds 400
if ($Keys) { foreach ($vk in $Keys.Split(',')) { $v=[byte]$vk; [A]::keybd_event($v,[byte][A]::MapVirtualKey($v,0),0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60; [A]::keybd_event($v,[byte][A]::MapVirtualKey($v,0),2,[IntPtr]::Zero); Start-Sleep -Milliseconds 150 } }
if (-not $NoCursor) { [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(1200, 900); Start-Sleep -Milliseconds 200; [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(1204, 904) }
Start-Sleep -Milliseconds 600
$bmp = New-Object System.Drawing.Bitmap 3840, 2160
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 3840, 2160))
$bmp.Save("$ScratchRoot\ld\$($Tag)_scr.png")
"saved $Tag ($($main.W)x$($main.H2))"
