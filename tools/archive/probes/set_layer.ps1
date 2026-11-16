param([string]$Flags, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
$key = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers'
$name = "$GameRoot\LETHALDREAMS.EXE"
if ($Flags -eq '') { Remove-ItemProperty -Path $key -Name $name -EA SilentlyContinue; "removed" }
else { Set-ItemProperty -Path $key -Name $name -Value $Flags; (Get-ItemProperty -Path $key -Name $name).$name }
