# perf_cpuload.ps1 — current CPU load of the game process (auto-detect PID)
param([int]$Pid_ = 0)
if ($Pid_ -le 0) { $Pid_ = (Get-Process LETHALDREAMS -ErrorAction SilentlyContinue | Select-Object -First 1).Id }
if (-not $Pid_) { Write-Host 'LETHALDREAMS process not found'; exit 1 }
$p = Get-Process -Id $Pid_
$c1 = $p.CPU
Start-Sleep -Seconds 3
$p.Refresh()
$c2 = $p.CPU
$d = $c2 - $c1
Write-Host ("PID {0}: CPU {1:N2}s over 3s ({2:P0} of one core)" -f $Pid_, $d, ($d/3))