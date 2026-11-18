# perf_tid.ps1 — find the busiest thread of a process (max CPU delta over N sec)
param([int]$Pid_ = 0, [int]$Seconds = 4)
if ($Pid_ -le 0) { $Pid_ = (Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1).Id }
if (-not $Pid_) { Write-Host 'LETHALDREAMS process not found'; exit 1 }
$p = Get-Process -Id $Pid_
$h1 = @{}
foreach ($t in $p.Threads) { $h1[[int]$t.Id] = $t.TotalProcessorTime.TotalMilliseconds }
Start-Sleep -Seconds $Seconds
$p.Refresh()
$best = 0; $bestMs = -1
foreach ($t in $p.Threads) {
  $d = $t.TotalProcessorTime.TotalMilliseconds - $h1[[int]$t.Id]
  if ($d -gt $bestMs) { $bestMs = $d; $best = [int]$t.Id }
}
Write-Host ("busiest tid={0}  delta={1:N0} ms over {2}s" -f $best, $bestMs, $Seconds)