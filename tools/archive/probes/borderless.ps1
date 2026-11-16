# borderless.ps1 - превращает окно Lethal Dreams в borderless fullscreen:
# снимает рамку (WS_CAPTION/WS_SYSMENU/WS_THICKFRAME/min-max) и ставит окно
# topmost на (0,0) размером весь desktop. Движок остаётся в оконном режиме
# (true windowed), modeset не трогается, мигания при переключении окон нет.
# Usage: powershell -File borderless.ps1  (после запуска игры; окно уже создано)
param([int]$W = 3840, [int]$H = 2160, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

Add-Type @"
using System;using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int i, int v);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after,
      int x, int y, int w, int hgt, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l,t,r,b; }
  public static IntPtr HWND_TOPMOST = (IntPtr)(-1);
  public const int GWL_STYLE = -16;
  public const uint SWP_FRAMECHANGED = 0x20, SWP_SHOWWINDOW = 0x40,
      SWP_NOACTIVATE = 0x10;
  public const int WS_CAPTION = 0x00C00000, WS_SYSMENU = 0x00080000,
      WS_THICKFRAME = 0x04000000, WS_MINIMIZEBOX = 0x00020000,
      WS_MAXIMIZEBOX = 0x00010000, WS_MINIMIZE = 0x20000000;
}
"@

$deadline = (Get-Date).AddSeconds(30)
$h = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
  $p = Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0
  if ($p) { $h = $p[0].MainWindowHandle; break }
  Start-Sleep -Milliseconds 500
}
if ($h -eq [IntPtr]::Zero) { 'no window found' | Out-File -Append "$GameRoot/hdlog.txt"; exit 1 }

$s = [W]::GetWindowLong($h, [W]::GWL_STYLE)
$s2 = $s -band (-not ([W]::WS_CAPTION -bor [W]::WS_SYSMENU -bor [W]::WS_THICKFRAME -bor [W]::WS_MINIMIZEBOX -bor [W]::WS_MAXIMIZEBOX -bor [W]::WS_MINIMIZE))
[W]::SetWindowLong($h, [W]::GWL_STYLE, $s2) | Out-Null
([W]::SetWindowPos($h, [W]::HWND_TOPMOST, 0, 0, $W, $H, [W]::SWP_FRAMECHANGED -bor [W]::SWP_SHOWWINDOW -bor [W]::SWP_NOACTIVATE)) | Out-Null
$r = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
"borderless: hwnd=$h style $s -> $s2 rect=$($r.l),$($r.t),$($r.r-$r.l)x$($r.b-$r.t)" | Out-File -Append "$GameRoot/hdlog.txt"
