# Съёмка пути «Вспомнить -> первый сейв -> Enter» (загрузка в оверленд) в любом режиме.
# Пишет hd.cfg, запускает игру, кликает по КАНВАСНЫМ точкам (пересчёт канвас->экран
# из hd_scale: экран_x = sy*CanvasX + DX*(w/1024), экран_y = sy*CanvasY, sy = h/768),
# снимает ТОЛЬКО окно игры, DPI-aware. Игра остаётся запущенной (pid печатается).
#
#   probe_loadshot.ps1 <W> <H> <F> <Tag> [-Sub ld] [-MenuSec 6] [-LoadX 350 -LoadY 107]
#       [-SlotX 300 -SlotY 215] [-NoEnter]
param([int]$W = 1280, [int]$H = 960, [int]$F = 0, [string]$Tag = "load",
      [string]$Sub = "ld", [int]$MenuSec = 6, [double]$LoadX = 350, [double]$LoadY = 107,
      [double]$SlotX = 280, [double]$SlotY = 161, [int]$LoadWait = 10,
      [string]$Mode = 'load', [double]$NewX = 200, [double]$NewY = 400,
      [double]$CharX = 0, [double]$CharY = 0,
      [int]$BattleWait = 0, [int]$BattleSettle = 4, [int]$EnterCount = 1,
      [int]$Tabs = 0, [int]$TabDelay = 2,
      [int]$SkipClicks = 0, [double]$SkipX = 512, [double]$SkipY = 384,
      [switch]$NoClick, [switch]$NoEnter, [switch]$NoKill, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
if (-not $RepoRoot) { $RepoRoot = (Resolve-Path "$PSScriptRoot\..\..\..").Path }

#Mode: 'load' = путь через «Вспомнить» (сейв, оверленд); 'new' = путь пользователя
#в БОЙ: глиф «Вообразить» (центр канвас (200,400), COORDS_MM_NewGame) -> SkipClicks
#кликов скипа ролика -> глиф персонажа (CharX,CharY) -> EnterCount Enter -> LoadWait сек.
if ($Mode -eq 'new') { $LoadX = $NewX; $LoadY = $NewY; $SlotX = $CharX; $SlotY = $CharY }

$ShotDir = "$ScratchRoot\$Sub"
if (-not (Test-Path $ShotDir)) { New-Item -ItemType Directory -Path $ShotDir | Out-Null }

Add-Type -AssemblyName System.Windows.Forms,System.Drawing

Add-Type @"
using System;using System.Runtime.InteropServices;
public class L {
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr ins, int x, int y, int cx, int cy, uint flags);
 [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int a);
 public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder buf, int max);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, IntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
[void][L]::SetProcessDpiAwareness(2)

$line = "{0} {1} 32 {2}" -f $W, $H, $F
Set-Content -Path "$RepoRoot\hd.cfg" -Value $line -Encoding ASCII
Set-Content -Path "$GameRoot\hd.cfg" -Value $line -Encoding ASCII
"hd.cfg -> $line"

function Find-Main([int]$pid_) {
  $found = New-Object System.Collections.ArrayList
  $cb = [L+EnumWindowsProc]{ param($h,$p2)
    $procId = 0; [void][L]::GetWindowThreadProcessId($h,[ref]$procId)
    if ($procId -eq $pid_) {
      $rec = New-Object L+RECT; [void][L]::GetWindowRect($h,[ref]$rec)
      if ([L]::IsWindowVisible($h)) {
        $sb = New-Object System.Text.StringBuilder 260
        [void][L]::GetWindowText($h, $sb, 260)
        [void]$found.Add([pscustomobject]@{ H=$h; Title=$sb.ToString(); L=$rec.L; T=$rec.T; W=($rec.R-$rec.L); H2=($rec.B-$rec.T) })
      }
    }
    return $true
  }
  [L]::EnumWindows($cb,[IntPtr]::Zero)
  # лог всех видимых окон процесса: съёмка/клики должны быть привязаны к окну ИГРЫ
  foreach ($w in $found) { "WIN title='$($w.Title)' rect $($w.L),$($w.T) $($w.W)x$($w.H2)" | Add-Content -Path $hdlog -Encoding ASCII }
  # окно игры = окно с заголовком (максимальная площадь); беззаголовочные служебные не берём
  $named = @($found | Where-Object { $_.Title -ne '' })
  if ($named.Count -gt 0) { return ($named | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1) }
  return ($found | Sort-Object { $_.W * $_.H2 } | Select-Object -Last 1)
}

function Shot($main, $name) {
  $bmp = New-Object System.Drawing.Bitmap $main.W, $main.H2
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($main.L, $main.T, 0, 0, (New-Object System.Drawing.Size $main.W, $main.H2))
  $bmp.Save($name)
  "saved $name ($($main.W)x$($main.H2))"
}

function To-Abs($main, $cx, $cy) {
  $k = $main.H2 * 1024.0 / ($main.W * 768.0)
  $dx = 512.0 - 512.0 * $k
  $sy = $main.H2 / 768.0
  return @([int]($main.L + $sy * $cx + $dx * ($main.W / 1024.0)), [int]($main.T + $sy * $cy))
}

function Click($x, $y) {
  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
  Start-Sleep -Milliseconds 400
  [L]::mouse_event(0x2,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [L]::mouse_event(0x4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 900
}

function Enter {
  [L]::keybd_event(0x0D,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [L]::keybd_event(0x0D,0,2,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

function TabKey {
  [L]::keybd_event(0x09,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
  [L]::keybd_event(0x09,0,2,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}

$hdlog = "$GameRoot\hdlog.txt"
"LOADSHOT $Tag start $(Get-Date -Format 'HH:mm:ss')" | Set-Content -Path $hdlog -Encoding ASCII

Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$dir = "$GameRoot"
$p = Start-Process (Join-Path $dir "LETHALDREAMS.EXE") -WorkingDirectory $dir -PassThru

Start-Sleep -Seconds $MenuSec
$p.Refresh()
if ($p.HasExited) { "EXITED code=0x{0:X}" -f $p.ExitCode; exit 1 }
$main = Find-Main $p.Id
if (-not $main) { "NO WINDOW"; exit 1 }
[void][L]::SetWindowPos($main.H, [IntPtr]::Zero, 0, 0, 0, 0, 3)
Start-Sleep -Milliseconds 300
$main = Find-Main $p.Id
"window $($main.W)x$($main.H2) at ($($main.L),$($main.T)) F=$F" | Add-Content -Path $hdlog -Encoding ASCII

Shot $main "$ShotDir\$($Tag)_menu.png"

if (-not $NoClick) {
  $a = To-Abs $main $LoadX $LoadY
  "glyph canvas ($LoadX,$LoadY) -> abs ($($a[0]),$($a[1]))" | Add-Content -Path $hdlog -Encoding ASCII
  Click $a[0] $a[1]
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_list.png"

  # [-SkipClicks N] - N кликов по канвасу (по умолчанию в центр): скип ролика при
  # загрузке сейва (путь пользователя: глиф -> 2 клика скипа -> глиф персонажа -> Enter Enter)
  if ($SkipClicks -gt 0) {
    for ($i = 1; $i -le $SkipClicks; $i++) {
      $c = To-Abs $main $SkipX $SkipY
      Click $c[0] $c[1]
    }
    $main = Find-Main $p.Id
    Shot $main "$ShotDir\$($Tag)_skip.png"
  }

  if ($SlotX -gt 0 -and $SlotY -gt 0) {
    $b = To-Abs $main $SlotX $SlotY
    "slot canvas ($SlotX,$SlotY) -> abs ($($b[0]),$($b[1]))" | Add-Content -Path $hdlog -Encoding ASCII
    Click $b[0] $b[1]
  }
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_sel.png"

  if (-not $NoEnter) {
    for ($i = 1; $i -le $EnterCount; $i++) { Enter }
    "ENTER x$EnterCount sent" | Add-Content -Path $hdlog -Encoding ASCII
    Start-Sleep -Seconds $LoadWait
  }

  [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point(4000, 20)
  Start-Sleep -Milliseconds 600
  $main = Find-Main $p.Id
  Shot $main "$ShotDir\$($Tag)_overland.png"

  # [-Tabs N] - N нажатий Tab в БОЮ (журнал боя выезжает/уезжает), снимок после каждого.
  # Перед этим нужен EnterCount 6..10 - пропуск первых обучающих уроков, иначе Tab перехватывает урок.
  if ($Tabs -gt 0) {
    for ($i = 1; $i -le $Tabs; $i++) {
      TabKey
      Start-Sleep -Seconds $TabDelay
      $main = Find-Main $p.Id
      Shot $main "$ShotDir\$($Tag)_tab$i.png"
    }
  }

  # [-BattleWait N] ждёт N секунд, пока пользователь руками входит в бой;
  # появление боя определяем по строке "SCENE battle" в hdlog.txt (hd_scale логирует
  # смену имени сцены на событиях мыши). После обнаружения ждём BattleSettle сек
  # (чтобы HUD боя полностью отрисовался) и снимаем.
  if ($BattleWait -gt 0) {
    $deadline = (Get-Date).AddSeconds($BattleWait)
    $seen = $false
    while ((Get-Date) -lt $deadline) {
      $t = Get-Content $hdlog -ErrorAction SilentlyContinue
      if ($t -and ($t | Select-String -Pattern 'SCENE battle' -Quiet)) { $seen = $true; break }
      Start-Sleep -Milliseconds 700
    }
    "battle seen=$seen (waited)" | Add-Content -Path $hdlog -Encoding ASCII
    if ($seen) {
      Start-Sleep -Seconds $BattleSettle
      $main = Find-Main $p.Id
      Shot $main "$ShotDir\$($Tag)_battle.png"
    }
  }
}
# правила съёмки: игра закрывается сразу после последнего снимка (-NoKill оставляет её)
if (-not $NoKill) {
  Stop-Process -Id $p.Id -Force
  "game closed pid=$($p.Id)"
} else {
  "game left running pid=$($p.Id)"
}
