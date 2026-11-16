Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.Screen]::AllScreens | ForEach-Object { "screen bounds=$($_.Bounds) primary=$($_.Primary)" }
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public class M2 {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)] public struct DEVMODE {
   [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
   public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
   public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
   public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
   [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
   public short dmLogPixels;
   public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
 }
 [DllImport("user32.dll", CharSet=CharSet.Ansi)] public static extern bool EnumDisplaySettingsA(IntPtr d, int m, ref DEVMODE dm);
}
"@
$dm = New-Object M2+DEVMODE; $dm.dmSize = [System.Runtime.InteropServices.Marshal]::SizeOf($dm)
[M2]::EnumDisplaySettingsA([IntPtr]::Zero, -1, [ref]$dm) | Out-Null
"MODE $($dm.dmPelsWidth)x$($dm.dmPelsHeight) bpp=$($dm.dmBitsPerPel) hz=$($dm.dmDisplayFrequency) dpi=$($dm.dmLogPixels)"
