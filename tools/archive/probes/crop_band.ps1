param([string]$Png, [int]$X0, [int]$Y0, [int]$W, [int]$H, [string]$Out)
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($Png)
$rect = New-Object System.Drawing.Rectangle $X0, $Y0, $W, $H
$c = $b.Clone($rect, $b.PixelFormat)
$c.Save($Out)
Write-Host "saved $Out ($($c.Width)x$($c.Height))"
