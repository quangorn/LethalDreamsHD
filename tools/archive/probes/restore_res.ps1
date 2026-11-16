# Возвращает разрешение desktop к целевому (по умолчанию 3840x2160 @120) через
# EnumDisplaySettings/ChangeDisplaySettings. Нужен после fullscreen-прогона игры,
# когда dxwrapper ResetScreenRes не восстановил desktop.
param([int]$W = 3840, [int]$H = 2160, [int]$Hz = 120)

Add-Type @"
using System;using System.Runtime.InteropServices;
public class R {
  [StructLayout(LayoutKind.Sequential)]
  public struct DEVMODE {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string sDevName;
    public int dSpec; public int dASize; public int dW; public int dH;
    public int wPPI; public int wPPV; public int dFlags; public int dFreq;
    public int dBpp; public int dXV; public int dYV;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst = 32)] public byte[] dCv;
    public int dRA;
  }
  [DllImport("user32.dll")] public static extern bool EnumDisplaySettings(string name, int mode, ref DEVMODE dm);
  [DllImport("user32.dll")] public static extern int ChangeDisplaySettings(ref DEVMODE dm, int flags);
  [DllImport("user32.dll")] public static extern int ChangeDisplaySettingsEx(string name, ref DEVMODE dm, IntPtr hwnd, int flags, IntPtr p);
}
"@

$dm = New-Object R+DEVMODE
$found = $false
for ($i = 0; $i -lt 200; $i++) {
  $ok = [R]::EnumDisplaySettings($null, $i, [ref]$dm)
  if (-not $ok) { break }
  if ($dm.dW -eq $W -and $dm.dH -eq $H) { $found = $true; break }
}
if (-not $found) { "режим $W`x$H не найден в списке" ; exit 1 }
$dm.dW = $W; $dm.dH = $H
if ($Hz -gt 0) { $dm.dFreq = $Hz; $dm.dFlags = $dm.dFlags -bor 0x4 }
$dm.dASize = [System.Runtime.InteropServices.Marshal]::SizeOf($dm)
$r = [R]::ChangeDisplaySettings([ref]$dm, 0)
"ChangeDisplaySettings -> $r (0 = успех), целевое $W`x$H @ $Hz"
