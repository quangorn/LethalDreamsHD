# probe_flicker.ps1 - ищет вспышку при alt-tab: снимает ВЕСЬ экран в 96x54 и считает
# среднюю яркость; печатает провалы относительно базовой яркости.
param([int]$MenuSec = 7, [int]$Frames = 80, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class FL2 {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EWP(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EWP cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][FL2]::SetProcessDpiAwareness(2)

$log = "$ScratchRoot\ld\flicker.txt"
"--- $(Get-Date -Format 'HH:mm:ss') menu=$MenuSec frames=$Frames" | Set-Content -Path $log -Encoding ASCII

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [FL2+EWP]{ param($h,$p2)
    $procId = 0; [void][FL2]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_ -and [FL2]::IsWindowVisible($h)) {
      $rec = New-Object FL2+RECT; [void][FL2]::GetWindowRect($h,[ref]$rec)
      [void]$found.Add([pscustomobject]@{ H=$h; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
    return $true
  }
  [FL2]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW" | Add-Content $log; exit 1 }

$bmp = New-Object System.Drawing.Bitmap 96, 54
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
function Mean-Brightness {
  $g.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 3840, 2160))
  $s = 0.0; $n = 0
  for ($y = 0; $y -lt 54; $y += 2) {
    for ($x = 0; $x -lt 96; $x += 2) {
      $c = $bmp.GetPixel($x, $y)
      $s += (0.299*$c.R + 0.587*$c.G + 0.114*$c.B); $n++
    }
  }
  return [math]::Round($s / $n, 1)
}

$base = @()
1..12 | ForEach-Object { $base += Mean-Brightness }
$bmean = [math]::Round((($base | Measure-Object -Average).Average), 1)
"baseline mean=$bmean min=$(($base | Measure-Object -Minimum).Minimum) max=$(($base | Measure-Object -Maximum).Maximum)" | Add-Content $log

function Sample($label, $count) {
  $vals = @()
  for ($i = 0; $i -lt $count; $i++) { $vals += Mean-Brightness }
  $mn = ($vals | Measure-Object -Minimum).Minimum
  $mx = ($vals | Measure-Object -Maximum).Maximum
  $avg = [math]::Round((($vals | Measure-Object -Average).Average), 1)
  $dips = ($vals | Where-Object { $_ -lt 0.75 * $bmean }).Count
  "$label n=$count min=$mn max=$mx avg=$avg dips_below_75pct=$dips" | Add-Content $log
}

$np = Start-Process notepad -PassThru
Start-Sleep -Milliseconds 900
$np.Refresh()
[void][FL2]::SetForegroundWindow($np.MainWindowHandle)
Sample "switch_to_notepad" $Frames
[void][FL2]::SetForegroundWindow($main.H)
Sample "switch_back" $Frames
Start-Sleep -Milliseconds 400
Stop-Process -Id $np.Id -Force
Start-Sleep -Milliseconds 400
[void][FL2]::SetForegroundWindow($main.H)
Sample "idle" 25
Get-Content $log
