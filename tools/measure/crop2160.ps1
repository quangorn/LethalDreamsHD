param(
  [Parameter(Mandatory=$true)][string]$In,
  [Parameter(Mandatory=$true)][string]$Out,
  [int]$W = 3840,
  [int]$H = 2160
)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($In)
$c = New-Object System.Drawing.Bitmap $W, $H
$g = [System.Drawing.Graphics]::FromImage($c)
$g.DrawImage($b, 0, 0, $W, $H)
$c.Save($Out)
Write-Host "saved: $Out"
