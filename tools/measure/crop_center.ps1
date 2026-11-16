param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out,
    [int]$Side = 640
)
# Кроп квадрата Side x Side вокруг центра изображения (центр экрана)
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($In)
$cx = [int]($bmp.Width/2); $cy = [int]($bmp.Height/2)
$x0 = $cx - [int]($Side/2); $y0 = $cy - [int]($Side/2)
if ($x0 -lt 0) { $x0 = 0 }; if ($y0 -lt 0) { $y0 = 0 }
$w = $Side; $h = $Side
if ($x0 + $w -gt $bmp.Width) { $w = $bmp.Width - $x0 }
if ($y0 + $h -gt $bmp.Height) { $h = $bmp.Height - $y0 }
$rect = New-Object System.Drawing.Rectangle($x0,$y0,$w,$h)
$crop = $bmp.Clone($rect, $bmp.PixelFormat)
$crop.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output ("crop {0} {1}x{2} at ({3},{4}) center=({5},{6})" -f (Split-Path $In -Leaf), $w, $h, $x0, $y0, $cx, $cy)
$bmp.Dispose(); $crop.Dispose()
