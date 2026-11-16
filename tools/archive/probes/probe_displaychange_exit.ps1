# probe_displaychange_exit.ps1 - ловит WM_DISPLAYCHANGE при запуске и при выходе из игры
param([int]$MenuSec = 7, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)

$log = "$ScratchRoot\ld\displaychange_exit.txt"
"--- $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $log -Encoding ASCII

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;

public class DisplayWatch2 {
  const int WM_DISPLAYCHANGE = 0x7E;
  static string LogPath;
  static string ClassName = "DX" + Guid.NewGuid().ToString("N");
  delegate IntPtr WndProc(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Ansi)]
  struct WNDCLASSA { public uint style; public WndProc lpfnWndProc; public int cbClsExtra; public int cbWndExtra;
    public IntPtr hInstance; public IntPtr hIcon; public IntPtr hCursor; public IntPtr hbrBackground;
    public string lpszMenuName; public string lpszClassName; }
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] static extern ushort RegisterClassA(ref WNDCLASSA c);
  [DllImport("user32.dll", CharSet=CharSet.Ansi)] static extern IntPtr CreateWindowExA(int ex, string cls, string name, int style, int x, int y, int w, int h, IntPtr p, IntPtr m, IntPtr i, IntPtr pp);
  [DllImport("user32.dll")] static extern IntPtr DefWindowProc(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] static extern int GetMessage(out MSG m, IntPtr h, uint a, uint b);
  [DllImport("user32.dll")] static extern IntPtr DispatchMessageA(ref MSG m);
  [DllImport("user32.dll")] static extern bool TranslateMessage(ref MSG m);
  [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam; public IntPtr lParam; public uint time; public int x; public int y; }
  static IntPtr Proc(IntPtr h, uint m, IntPtr w, IntPtr l) {
    if (m == WM_DISPLAYCHANGE) { File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " WM_DISPLAYCHANGE w=" + w.ToInt64() + " h=" + l.ToInt64() + Environment.NewLine); return IntPtr.Zero; }
    return DefWindowProc(h, m, w, l);
  }
  public static void Start(string path) {
    LogPath = path;
    Thread t = new Thread(delegate() {
      WNDCLASSA wc = new WNDCLASSA();
      wc.lpfnWndProc = Proc; wc.lpszClassName = ClassName;
      RegisterClassA(ref wc);
      CreateWindowExA(0, ClassName, "dx", 0, 0, 0, 0, 0, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
      File.AppendAllText(LogPath, DateTime.Now.ToString("HH:mm:ss.fff") + " watcher ready" + Environment.NewLine);
      MSG msg;
      while (GetMessage(out msg, IntPtr.Zero, 0, 0) > 0) { TranslateMessage(ref msg); DispatchMessageA(ref msg); }
    });
    t.IsBackground = true; t.SetApartmentState(ApartmentState.STA); t.Start();
  }
}
"@

[DisplayWatch2]::Start($log)
Start-Sleep -Milliseconds 500

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
"--- launch" | Add-Content $log
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru
Start-Sleep -Seconds $MenuSec
"--- graceful close (WM_CLOSE)" | Add-Content $log
$p.Refresh()
[void]$p.CloseMainWindow()
Start-Sleep -Seconds 4
$p.Refresh()
"--- exited=$($p.HasExited)" | Add-Content $log
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Seconds 1
Get-Content $log
