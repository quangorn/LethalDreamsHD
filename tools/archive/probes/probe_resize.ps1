# Probe: does the Lethal Dreams GL window follow an external resize?
param([string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class W {
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
 [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int idx);
 [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int idx, int v);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][W]::SetProcessDPIAware()

function Find-MainWindow([int]$targetId) {
  $found = New-Object System.Collections.ArrayList
  $cb = [W+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][W]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $targetId) {
      $sb = New-Object System.Text.StringBuilder 256; [void][W]::GetWindowText($h,$sb,256)
      $cn = New-Object System.Text.StringBuilder 256; [void][W]::GetClassName($h,$cn,256)
      $rec = New-Object W+RECT; [void][W]::GetWindowRect($h,[ref]$rec)
      [void]$found.Add([pscustomobject]@{ H=$h; Class=$cn.Text; Title=$sb.Text; Visible=[W]::IsWindowVisible($h); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
    return $true
  }
  [W]::EnumWindows($cb,[IntPtr]::Zero)
  return $found
}

function Shot($name, [int]$targetId) {
  $wins = Find-MainWindow $targetId
  foreach ($w in $wins) {
    if ($w.Visible -and $w.H2 -gt 200) {
      $bmp = New-Object System.Drawing.Bitmap $w.W, $w.H2
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $g.CopyFromScreen($w.L, $w.T, [System.Drawing.Point]::Empty, (New-Object System.Drawing.Size ($w.W, $w.H2)))
      $bmp.Save("$ScratchRoot\" + $name)
      "window {0}x{1} at ({2},{3}) class={4} title=[{5}] -> saved {6}" -f $w.W, $w.H2, $w.L, $w.T, $w.Class, $w.Title, $name
      return
    }
  }
  "no visible window for pid=$targetId"
}

$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds 12
Shot "r0_before.png" $p.Id
$wins = Find-MainWindow $p.Id
$main = $wins | Where-Object { $_.Visible -and $_.H2 -gt 300 } | Select-Object -First 1
if (-not $main) { "main window not found"; exit }
"main: class={0} title=[{1}] size={2}x{3}" -f $main.Class, $main.Title, $main.W, $main.H2
# strip border/caption and resize to 1920x1080 at (0,0)
$style = [W]::GetWindowLong($main.H, -16)
[void][W]::SetWindowLong($main.H, -16, ($style -band 0x7FFFFFFF -band (-bnot 0x00C00000)))
[void][W]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 1920, 1080, 0x0040)
Start-Sleep -Seconds 3
Shot "r1_afterresize.png" $p.Id
Start-Sleep -Seconds 5
Shot "r2_after5s.png" $p.Id
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode } else { "still running; leaving window open for user" }
