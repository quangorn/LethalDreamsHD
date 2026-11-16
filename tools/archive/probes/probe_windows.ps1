# probe_windows.ps1 - перечисляет ВСЕ окна процесса игры (включая невидимые):
# класс, rect, style, exstyle, visible
param([int]$MenuSec = 7, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;using System.Collections.Generic;
public class W3 {
 public delegate bool EWP(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EWP cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EWP cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 public static List<string> Dump(uint pid) {
   List<string> res = new List<string>();
   EnumWindows(delegate(IntPtr h, IntPtr p) {
     uint pp; GetWindowThreadProcessId(h, out pp);
     if (pp == pid) {
       RECT r; GetWindowRect(h, out r);
       StringBuilder cn = new StringBuilder(128); GetClassName(h, cn, 128);
       res.Add(string.Format("TOP  {0} class={1} rect={2},{3} {4}x{5} style=0x{6:X8} ex=0x{7:X8} visible={8}",
         h, cn, r.L, r.T, r.R-r.L, r.B-r.T, GetWindowLong(h,-16), GetWindowLong(h,-20), IsWindowVisible(h)));
     }
     return true;
   }, IntPtr.Zero);
   return res;
 }
}
"@

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec
[W3]::Dump([uint32]$p.Id)
Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
