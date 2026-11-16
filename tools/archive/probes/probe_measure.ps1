# Measures the Lethal Dreams window size (DPI aware), captures window and full screen,
# reports desktop resolution before launch and after the run.
param([int]$WaitSec = 5, [string]$Tag = "run", [int]$ClickAt = 0, [string]$Sub = "ld", [int]$Shots = 1, [switch]$WinOnly, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class W {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, IntPtr dwExtra);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][W]::SetProcessDpiAwareness(2)
[void][W]::SetProcessDPIAware()

function Get-DesktopRes {
  $p = Get-CimInstance -ClassName Win32_VideoController -ErrorAction SilentlyContinue | Where-Object { $_.CurrentVerticalResolution -gt 0 } | Select-Object -First 1
  if ($p) { "desktop " + $p.CurrentHorizontalResolution + "x" + $p.CurrentVerticalResolution + " @ " + $p.CurrentRefreshRate + " (mode: " + $p.CurrentVideoMode + ")" }
  else { "desktop unknown" }
}

function Find-Windows([int]$targetId) {
  $found = New-Object System.Collections.ArrayList
  $cb = [W+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][W]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $targetId) {
      $sb = New-Object System.Text.StringBuilder 256; [void][W]::GetWindowText($h,$sb,256)
      $cn = New-Object System.Text.StringBuilder 256; [void][W]::GetClassName($h,$cn,256)
      $rec = New-Object W+RECT; [void][W]::GetWindowRect($h,[ref]$rec)
      [void]$found.Add([pscustomobject]@{ H=$h; Class=$cn.Text; Title=$sb.Text; Visible=[W]::IsWindowVisible($h); L=$rec.L; T=$rec.T; Width=($rec.R-$rec.L); Height=($rec.B-$rec.T) })
    }
    return $true
  }
  [W]::EnumWindows($cb,[IntPtr]::Zero)
  return $found
}

"BEFORE: " + (Get-DesktopRes)
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
for ($i=1; $i -le $Shots; $i++) {
  Start-Sleep -Seconds $WaitSec
  $p.Refresh()
  if ($p.HasExited) { "t={0}s EXITED code=0x{1:X}" -f ($i*$WaitSec), $p.ExitCode; break }
  $wins = Find-Windows $p.Id | Where-Object { $_.Visible }
  foreach ($w in $wins) {
    "t={0}s window [{1}] class={2} size={3}x{4} at ({5},{6})" -f ($i*$WaitSec), $w.Title, $w.Class, $w.Width, $w.Height, $w.L, $w.T
  }
  if ($ClickAt -gt 0 -and ($i*$WaitSec) -eq $ClickAt) {
    [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point([int]($full.Width/2), [int]($full.Height/2))
    [W]::mouse_event(2,0,0,0,[IntPtr]::Zero); [W]::mouse_event(4,0,0,0,[IntPtr]::Zero)
    "click at center"
  }
  "t={0}s " + (Get-DesktopRes)
  $full = [System.Windows.Forms.SystemInformation]::VirtualScreen
  # главный window игры = самое большое видимое окно процесса
  $main = $wins | Sort-Object { $_.Width * $_.Height } | Select-Object -Last 1
  if ($main) {
    # поднять на передний план без перемещения/ресайза (SWP_NOMOVE=2 | SWP_NOSIZE=1);
    # WM_SIZE не генерируется -> игра не минимизируется
    [void][W]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
    Start-Sleep -Milliseconds 300
  }
  if (-not $WinOnly) {
    $fbmp = New-Object System.Drawing.Bitmap $full.Width, $full.Height
    $fg = [System.Drawing.Graphics]::FromImage($fbmp)
    $fg.CopyFromScreen($full.X, $full.Y, 0, 0, (New-Object System.Drawing.Size $full.Width, $full.Height))
    $fname = "$ShotDir\{0}_{1}s_full.png" -f $Tag, ($i*$WaitSec)
    $fbmp.Save($fname)
    "saved " + $fname
  }
  if ($main) {
    $name = "$ShotDir\{0}_{1}s_win.png" -f $Tag, ($i*$WaitSec)
    $bmp = New-Object System.Drawing.Bitmap $main.Width, $main.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size ($main.Width, $main.Height)))
    $bmp.Save($name)
    "saved " + $name
  }
}
"AFTER: " + (Get-DesktopRes)
