# perf_activate.ps1 — force the game window to foreground and send a key (space = pause/unpause)
param([int]$Pid_ = 0, [string]$Key = ' ')
if ($Pid_ -le 0) { $Pid_ = (Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1).Id }
if (-not $Pid_) { Write-Host 'LETHALDREAMS process not found'; exit 1 }
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class A {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
$p = Get-Process -Id $Pid_
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Host 'NO WINDOW'; exit 1 }
if ([A]::IsIconic($h)) { [void][A]::ShowWindow($h, 9); Start-Sleep -Milliseconds 500 }
$fg = [A]::GetForegroundWindow()
$fgT = 0; [void][A]::GetWindowThreadProcessId($fg, [ref]$fgT)
$tt = 0; [void][A]::GetWindowThreadProcessId($h, [ref]$tt)
[void][A]::AttachThreadInput($fgT, $tt, $true)
[void][A]::SetForegroundWindow($h)
[void][A]::AttachThreadInput($fgT, $tt, $false)
Start-Sleep -Milliseconds 700
$fg2 = [A]::GetForegroundWindow()
Write-Host ("activated: {0} (was {1})" -f ($fg2 -eq $h), ($fg -eq $h))
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait($Key)
Write-Host ("sent [{0}]" -f $Key)
Start-Sleep -Milliseconds 800
$c1 = $p.CPU
Start-Sleep -Seconds 3
$p.Refresh()
Write-Host ("CPU after: {0:N2}s over 3s" -f ($p.CPU - $c1))