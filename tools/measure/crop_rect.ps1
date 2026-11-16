param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$X = 0, [int]$Y = 0, [int]$W = 640, [int]$H = 640
)
# Кроп произвольногоrect'а снимка (экранные пиксели)
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($In)
if ($X -lt 0) { $X = 0 }; if ($Y -lt 0) { $Y = 0 }
if ($X + $W -gt $bmp.Width) { $W = $bmp.Width - $X }
if ($Y + $H -gt $bmp.Height) { $H = $bmp.Height - $Y }
$rect = New-Object System.Drawing.Rectangle($X,$Y,$W,$H)
$crop = $bmp.Clone($rect, $bmp.PixelFormat)
$crop.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output ("crop {0} {1}x{2} at ({3},{4})" -f (Split-Path $In -Leaf), $W, $H, $X, $Y)
$bmp.Dispose(); $crop.Dispose()
