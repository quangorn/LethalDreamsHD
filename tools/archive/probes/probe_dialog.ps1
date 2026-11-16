# probe_dialog.ps1 - читает текст messagebox процесса игры (если он есть)
param([int]$MenuSec = 9, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;using System.Collections.Generic;
public class DLG {
 public delegate bool EWP(IntPtr h, IntPtr p);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumWindows(EWP cb, IntPtr p);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool EnumChildWindows(IntPtr parent, EWP cb, IntPtr p);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 public static List<string> Dump(uint pid) {
   List<string> res = new List<string>();
   EnumWindows(delegate(IntPtr h, IntPtr p) {
     uint pp; GetWindowThreadProcessId(h, out pp);
     if (pp != pid) return true;
     StringBuilder cn = new StringBuilder(128); GetClassNameW(h, cn, 128);
     StringBuilder tx = new StringBuilder(512); GetWindowTextW(h, tx, 512);
     if (cn.ToString() == "#32770") {
       res.Add("DIALOG class=" + cn + " text=" + tx);
       EnumChildWindows(h, delegate(IntPtr c, IntPtr q) {
         StringBuilder cc = new StringBuilder(128); GetClassNameW(c, cc, 128);
         StringBuilder ct = new StringBuilder(1024); GetWindowTextW(c, ct, 1024);
         if (ct.Length > 0) res.Add("   CHILD " + cc + ": " + ct);
         return true;
       }, IntPtr.Zero);
     }
     return true;
   }, IntPtr.Zero);
   return res;
 }
}
"@
Get-Process LETHALDREAMS -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec
[DLG]::Dump([uint32]$p.Id)
Get-Process LETHALDREAMS -EA SilentlyContinue | Stop-Process -Force
