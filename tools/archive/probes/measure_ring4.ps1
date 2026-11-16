param(
    [Parameter(Mandatory=$true)][string]$File,
    [double]$CX,
    [double]$CY,
    [int]$Radius = 240
)
# Точный центр тора на ЧЁРНОМ фоне: bbox пикселей тора (яркий белый>235, насыщенный
# красный, чёрная дуга=тёмные внутри радиуса) - центр bbox = центр кольца;
# rx/ry - полуразмеры (круглость).
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($File)
$W = $bmp.Width; $H = $bmp.Height
$x0 = [math]::Max(0, [int]($CX-$Radius)); $x1 = [math]::Min($W, [int]($CX+$Radius))
$y0 = [math]::Max(0, [int]($CY-$Radius)); $y1 = [math]::Min($H, [int]($CY+$Radius))
$minx=$W; $maxx=0; $miny=$H; $maxy=0; $n=0
for ($y = $y0; $y -lt $y1; $y += 1) {
    for ($x = $x0; $x -lt $x1; $x += 1) {
        $dx = $x - $CX; $dy = $y - $CY
        if ($dx*$dx + $dy*$dy -gt $Radius*$Radius) { continue }
        $c = $bmp.GetPixel($x, $y)
        $r = $c.R; $g = $c.G; $b = $c.B
        if (($r -gt 235 -and $g -gt 235 -and $b -gt 235) -or ($r -gt 120 -and $g -lt 60 -and $b -lt 60) -or ($r -lt 30 -and $g -lt 30 -and $b -lt 30)) {
            $n += 1
            if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x }
            if ($y -lt $miny) { $miny = $y }; if ($y -gt $maxy) { $maxy = $y }
        }
    }
}
$bcx = ($minx+$maxx)/2; $bcy = ($miny+$maxy)/2
Write-Output ("{0} tor bbox x={1}..{2} y={3}..{4} center=({5},{6}) rx={7} ry={8} ratio={9} offset=({10},{11}) n={12}" -f `
    (Split-Path $File -Leaf), $minx, $maxx, $miny, $maxy, [math]::Round($bcx,1), [math]::Round($bcy,1), `
    [math]::Round(($maxx-$minx)/2,1), [math]::Round(($maxy-$miny)/2,1), [math]::Round(($maxx-$minx)/($maxy-$miny),3), `
    [math]::Round($bcx-$CX,1), [math]::Round($bcy-$CY,1), $n)
$bmp.Dispose()
