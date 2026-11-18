# perf_sendkey.ps1 — activate the game window and send a key (default: space)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File perf_sendkey.ps1 [-Pid <pid>] [-Key " "] [-Activate]
param([int]$Pid_ = 0, [string]$Key = " ", [switch]$Activate)
if ($Pid_ -le 0) { $Pid_ = (Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1).Id }
if (-not $Pid_) { Write-Host 'LETHALDREAMS process not found'; exit 1 }
Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class K {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, StringBuilder buf, int max);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
}
"@
$found = New-Object System.Collections.ArrayList
$cb = [K+EnumWindowsProc]{ param($h,$p2)
  $procId = 0; [void][K]::GetWindowThreadProcessId($h,[ref]$procId)
  if ($procId -eq $Pid_) {
    $rec = New-Object K+RECT; [void][K]::GetWindowRect($h,[ref]$rec)
    if ([K]::IsWindowVisible($h)) {
      $sb = New-Object System.Text.StringBuilder 260
      [void][K]::GetWindowText($h, $sb, 260)
      [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
  }
  return $true
}
[K]::EnumWindows($cb,[IntPtr]::Zero)
$named = @($found | Where-Object { $_.Title -ne '' })
if ($named.Count -gt 0) { $main = $named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1 }
else { $main = $found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1 }
if (-not $main) { Write-Host 'NO WINDOW'; exit 1 }
if ($Activate) {
  [void][K]::ShowWindow($main.H, 9)   # SW_RESTORE
  Start-Sleep -Milliseconds 300
  [void][K]::SetForegroundWindow($main.H)
  Start-Sleep -Milliseconds 500
  Write-Host ("activated '{0}'" -f $main.Title)
}
if ($Key -ne '') {
  Add-Type -AssemblyName System.Windows.Forms
  [System.Windows.Forms.SendKeys]::SendWait($Key)
  Write-Host ("sent key: [{0}]" -f $Key)
}