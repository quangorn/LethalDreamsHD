# probe_dpi.ps1 - запускает игру и печатает: DPI-awareness процесса, rect окна, режим
param([int]$MenuSec = 8, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class DPIQ {
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 [DllImport("shcore.dll")] public static extern int GetProcessDpiAwareness(IntPtr p, out int a);
 [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(int acc, bool inh, int pid);
 [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
 public delegate bool EWP(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EWP cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][DPIQ]::SetProcessDpiAwareness(2)

Get-Process LETHALDREAMS -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec
$h = [DPIQ]::OpenProcess(0x1000, $false, $p.Id)
$aw = -1
if ($h -ne [IntPtr]::Zero) { [void][DPIQ]::GetProcessDpiAwareness($h, [ref]$aw); [void][DPIQ]::CloseHandle($h) }
"dpi_awareness=$aw   (0=unaware 1=system 2=per-monitor)"
$cb = [DPIQ+EWP]{ param($w,$q)
  $pp=0; [void][DPIQ]::GetWindowThreadProcessId($w,[ref]$pp)
  if ($pp -eq $p.Id) {
    $r = New-Object DPIQ+RECT; [void][DPIQ]::GetWindowRect($w,[ref]$r)
    $cn = New-Object System.Text.StringBuilder 128; [void][DPIQ]::GetClassNameW($w,$cn,128)
    if ($cn.ToString() -eq 'LethalDreamsWndClass') { "window $($r.L),$($r.T) $($r.R-$r.L)x$($r.B-$r.T) visible=$([DPIQ]::IsWindowVisible($w))" }
  }
  return $true }
[void][DPIQ]::EnumWindows($cb,[IntPtr]::Zero)
Get-Process LETHALDREAMS -EA SilentlyContinue | Stop-Process -Force
