param(
    [Parameter(Mandatory=$true)][string]$File,
    [int]$Half = 320
)

# Кольцо-маркер: y_top - верх яркого белого, y_bot - низ насыщенного красного;
# x_left/x_right - горизонтальныйExtent (белый+красный) на средней строке.
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($File)
$W = $bmp.Width; $H = $bmp.Height
$x0 = [int](($W - 2*$Half)/2); $x1 = [int](($W + 2*$Half)/2)
if ($x0 -lt 0) { $x0 = 0 }; if ($x1 -gt $W) { $x1 = $W }
function IsTor($c) {
    return ($c.R -gt 240 -and $c.G -gt 240 -and $c.B -gt 240) -or ($c.R -gt 140 -and $c.G -lt 45 -and $c.B -lt 45)
}
$top = -1; $bot = -1
for ($y=0; $y -lt $H; $y+=1) {
    $hit = 0
    for ($x=$x0; $x -lt $x1; $x+=2) { if (IsTor($bmp.GetPixel($x,$y))) { $hit += 1 } }
    if ($hit -ge 40) { $top = $y; break }
}
for ($y=$H-1; $y -ge 0; $y-=1) {
    $hit = 0
    for ($x=$x0; $x -lt $x1; $x+=2) { if (IsTor($bmp.GetPixel($x,$y))) { $hit += 1 } }
    if ($hit -ge 40) { $bot = $y; break }
}
$cy = [int](($top + $bot)/2)
$L = -1; $R = -1
for ($x=$x0; $x -lt $x1; $x+=1) {
    $c = $bmp.GetPixel($x,$cy)
    if (IsTor($c)) { if ($L -lt 0) { $L = $x }; $R = $x }
}
$cx = [math]::Round(($L + $R)/2, 1)
$rx = [math]::Round(($R - $L)/2, 1)
$ry = [math]::Round(($bot - $top)/2, 1)
Write-Output ("{0} center=({1},{2}) rx={3} ry={4} ratio_rx_ry={5} target=({6},{7})" -f `
    (Split-Path $File -Leaf), $cx, $cy, $rx, $ry, [math]::Round($rx/$ry,3), ($W/2), ($H/2))
$bmp.Dispose()
