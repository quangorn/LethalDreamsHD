param(
    [Parameter(Mandatory=$true)][string]$File,
    [double]$StartX,
    [double]$StartY,
    [int]$Radius = 300
)
# Центр кольца на чёрном фоне: центроид яркого (сумма RGB>200) в радиусе Radius от
# стартовой точки, 2 итерации (центроид -> новый радиус вокруг него).
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($File)
$W = $bmp.Width; $H = $bmp.Height
$cx = $StartX; $cy = $StartY
$n = 0
for ($pass = 0; $pass -lt 2; $pass++) {
    $sx = 0.0; $sy = 0.0; $n = 0
    $x0 = [math]::Max(0, [int]($cx-$Radius)); $x1 = [math]::Min($W, [int]($cx+$Radius))
    $y0 = [math]::Max(0, [int]($cy-$Radius)); $y1 = [math]::Min($H, [int]($cy+$Radius))
    for ($y = $y0; $y -lt $y1; $y += 2) {
        for ($x = $x0; $x -lt $x1; $x += 2) {
            $dx = $x - $cx; $dy = $y - $cy
            if ($dx*$dx + $dy*$dy -gt $Radius*$Radius) { continue }
            $c = $bmp.GetPixel($x, $y)
            if ($c.R + $c.G + $c.B -gt 200) { $sx += $x; $sy += $y; $n += 1 }
        }
    }
    $cx = $sx / $n; $cy = $sy / $n
}
Write-Output ("{0} ring_center=({1},{2}) n={3}" -f (Split-Path $File -Leaf), [math]::Round($cx,1), [math]::Round($cy,1), $n)
$bmp.Dispose()
