# probe_displaychange.ps1 - ловит WM_DISPLAYCHANGE / WM_SETTINGCHANGE при alt-tab.
# Печатает каждое событие с временем и текущим режимом (EnumDisplaySettings).
param([int]$MenuSec = 7, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$log = "$ScratchRoot\ld\displaychange.txt"
"--- $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $log -Encoding ASCII

Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public class DisplayWatch {
  const int WM_DISPLAYCHANGE = 0x7E;
  const int WM_SETTINGCHANGE = 0x1A;
  static string LogPath;
  static string ClassName = "DW" + Guid.NewGuid().ToString("N");
  delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
  struct WNDCLASSA { public uint style; public WndProc lpfnWndProc; public int cbClsExtra; public int cbWndExtra;
    public IntPtr hInstance; public IntPtr hIcon; public IntPtr hCursor; public IntPtr hbrBackground;
    public string lpszMenuName; public string lpszClassName; }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
  struct DEVMODEA {
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmDeviceName;
    public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
    public int dmFields, dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
    public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string dmFormName;
    public short dmLogPixels;
    public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
  }
  [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Ansi)] static extern ushort RegisterClassA(ref WNDCLASSA c);
  [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Ansi)] static extern IntPtr CreateWindowExA(int ex, string cls, string name, int style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr p);
  [DllImport("user32.dll")] static extern IntPtr DefWindowProc(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern int GetMessage(out MSG m, IntPtr h, uint a, uint b);
  [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG m);
  [DllImport("user32.dll")] static extern IntPtr DispatchMessageA(ref MSG m);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] static extern bool EnumDisplaySettingsA(IntPtr dev, int mode, ref DEVMODEA dm);
  [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int x; public int y; }

  static string Mode() {
    DEVMODEA dm = new DEVMODEA(); dm.dmSize = (short)Marshal.SizeOf(typeof(DEVMODEA));
    if (EnumDisplaySettingsA(IntPtr.Zero, -1, ref dm))
      return dm.dmPelsWidth + "x" + dm.dmPelsHeight + "@" + dm.dmDisplayFrequency + " bpp=" + dm.dmBitsPerPel;
    return "?";
  }
  static IntPtr Proc(IntPtr h, uint m, IntPtr w, IntPtr l) {
    if (m == WM_DISPLAYCHANGE) { File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " WM_DISPLAYCHANGE -> " + Mode() + " lParam=" + l.ToInt64() + Environment.NewLine); return IntPtr.Zero; }
    if (m == WM_SETTINGCHANGE) { File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " WM_SETTINGCHANGE -> " + Mode() + Environment.NewLine); return IntPtr.Zero; }
    return DefWindowProc(h, m, w, l);
  }
  public static void Start(string path) {
    LogPath = path;
    Thread t = new Thread(delegate() {
      WNDCLASSA wc = new WNDCLASSA();
      wc.lpfnWndProc = Proc; wc.lpszClassName = ClassName;
      RegisterClassA(ref wc);
      CreateWindowExA(0, ClassName, "dw", 0, 0, 0, 0, 0, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
      File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " watcher ready, mode=" + Mode() + Environment.NewLine);
      MSG msg;
      while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0) { TranslateMessage(ref msg); DispatchMessageA(ref msg); }
    });
    t.IsBackground = true; t.SetApartmentState(ApartmentState.STA); t.Start();
  }
}
"@

[DisplayWatch]::Start($log)
Start-Sleep -Milliseconds 700

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class DW2 {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EWP(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EWP cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][DW2]::SetProcessDpiAwareness(2)

$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [DW2+EWP]{ param($h,$p2)
    $procId = 0; [void][DW2]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_ -and [DW2]::IsWindowVisible($h)) {
      $rec = New-Object DW2+RECT; [void][DW2]::GetWindowRect($h,[ref]$rec)
      [void]$found.Add([pscustomobject]@{ H=$h; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
    }
    return $true
  }
  [DW2]::EnumWindows($cb,[IntPtr]::Zero)
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}
$main = Find-Main $p.Id
"game hwnd=$($main.H)" | Add-Content $log

$np = Start-Process notepad -PassThru
Start-Sleep -Milliseconds 900
$np.Refresh()
"--- switching to notepad" | Add-Content $log
[void][DW2]::SetForegroundWindow($np.MainWindowHandle)
Start-Sleep -Seconds 3
"--- switching back" | Add-Content $log
[void][DW2]::SetForegroundWindow($main.H)
Start-Sleep -Seconds 3
Stop-Process -Id $np.Id -Force
Start-Sleep -Seconds 2
Get-Content $log
