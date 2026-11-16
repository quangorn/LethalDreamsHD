# probe_f1.ps1 - проверка borderless fullscreen (hd.cfg F=1):
#  - режим дисплея ДО запуска и ВО ВРЕМЯ работы (не должен меняться)
#  - реальный rect окна игры, стиль, topmost
#  - снимок области окна (без всего рабочего стола)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File probe_f1.ps1 -Tag f1a
param([int]$MenuSec = 6, [string]$Tag = "f1", [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$ShotDir = "$ScratchRoot\ld"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;using System.Text;
public class F1 {
 [DllImport("user32.dll", EntryPoint="EnumDisplaySettingsA")] public static extern bool EnumDisplaySettingsA(IntPtr dev, int mode, ref DEVMODE dm);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)] public struct DEVMODE {
   [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
   public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
   public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
   public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
   [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
   public short dmLogPixels;
   public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
   public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2, dmPanningWidth, dmPanningHeight;
 }
}
"@
[void][F1]::SetProcessDpiAwareness(2)

$hdlog = "$GameRoot\hdlog.txt"

function Get-Mode([string]$when) {
  $dm = New-Object F1+DEVMODE
  $dm.dmSize = [System.Runtime.InteropServices.Marshal]::SizeOf($dm)
  if ([F1]::EnumDisplaySettingsA([IntPtr]::Zero, -1, [ref]$dm)) {
    "MODE $when = $($dm.dmPelsWidth)x$($dm.dmPelsHeight) bpp=$($dm.dmBitsPerPel) hz=$($dm.dmDisplayFrequency)" | Add-Content -Path $hdlog -Encoding ASCII
  } else {
    "MODE $when = <EnumDisplaySettings failed>" | Add-Content -Path $hdlog -Encoding ASCII
  }
}

"RUN $Tag start $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $hdlog -Encoding ASCII
Get-Mode "before"

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec

$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode | Add-Content -Path $hdlog -Encoding ASCII; exit 1 }
Get-Mode "during"

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [F1+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][F1]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_ -and [F1]::IsWindowVisible($h)) {
      $rec = New-Object F1+RECT; [void][F1]::GetWindowRect($h,[ref]$rec)
      $cn = New-Object System.Text.StringBuilder 256
      [void][F1]::GetClassName($h,$cn,256)
      [void]$found.Add([pscustomobject]@{ H=$h; L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T); Cls=$cn.ToString() })
    }
    return $true
  }
  [F1]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW" | Add-Content -Path $hdlog -Encoding ASCII; exit 1 }
$style   = [F1]::GetWindowLong($main.H, -16)
$exstyle = [F1]::GetWindowLong($main.H, -20)
$fg = [F1]::GetForegroundWindow()
"WINDOW class=$($main.Cls) rect=$($main.W)x$($main.H2) at ($($main.L),$($main.T)) style=0x{0:X8} exstyle=0x{1:X8} foreground={2}" -f $style, $exstyle, ($fg -eq $main.H) | Add-Content -Path $hdlog -Encoding ASCII

function Focus-Wait([IntPtr]$h, [string]$what) {
  [void]([F1]::SetForegroundWindow($h))
  $sw = [System.Diagnostics.Stopwatch]::StartNew()
  $ok = $false
  while ($sw.ElapsedMilliseconds -lt 4000) {
    if ([F1]::GetForegroundWindow() -eq $h) { $ok = $true; break }
    Start-Sleep -Milliseconds 5
  }
  $sw.Stop()
  "ALT-TAB $what ok=$ok ms=$($sw.ElapsedMilliseconds)" | Add-Content -Path $hdlog -Encoding ASCII
}

# alt-tab: нотепад -> игра; в эксклюзивном fullscreen это занимает ~1-3 с и мигает
$np = Start-Process notepad -PassThru
Start-Sleep -Milliseconds 700
$np.Refresh()
Focus-Wait $np.MainWindowHandle "to_notepad"
Get-Mode "after_switch_out"
Start-Sleep -Milliseconds 500
Focus-Wait $main.H "back_to_game"
Get-Mode "after_switch_back"
Stop-Process -Id $np.Id -Force
Start-Sleep -Milliseconds 500

# снимок области окна (при настоящем borderless = весь кадр)
$fname = "$ShotDir\{0}_win.png" -f $Tag
$bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
$bmp.Save($fname)
"saved $fname" | Add-Content -Path $hdlog -Encoding ASCII

# снимок всего экрана: видно, есть ли рабочий стол вокруг окна
$fname2 = "$ShotDir\{0}_desktop.png" -f $Tag
$b2 = New-Object System.Drawing.Bitmap 3840, 2160
$g2 = [System.Drawing.Graphics]::FromImage($b2)
$g2.CopyFromScreen(0, 0, 0, 0, (New-Object System.Drawing.Size 3840, 2160))
$b2.Save($fname2)
"saved $fname2" | Add-Content -Path $hdlog -Encoding ASCII
