param(
    [Parameter(Mandatory=$true)][string]$File,
    [int]$Radius = 420,
    [double]$CX = -1,
    [double]$CY = -1
)
# Тор-кольцо: белая дуга (R,G,B>240), чёрная дуга (R,G,B<45 рядом с ярким пикселем),
# красная дуга (R>140, G<70, B<70). Берём только пиксели внутри Radius от (CX,CY).
# Центр тора = центроид всех трёх дуг; rx/ry - полуразмеры bbox дуг (roundness).
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($File)
$W = $bmp.Width; $H = $bmp.Height
if ($CX -lt 0) { $CX = $W/2 }
if ($CY -lt 0) { $CY = $H/2 }
$x0 = [math]::Max(0, [int]($CX - $Radius)); $x1 = [math]::Min($W, [int]($CX + $Radius))
$y0 = [math]::Max(0, [int]($CY - $Radius)); $y1 = [math]::Min($H, [int]($CY + $Radius))
# пре-массив яркости
$bright = New-Object bool[] (($x1-$x0)*($y1-$y0))
for ($y = $y0; $y -lt $y1; $y += 1) {
    for ($x = $x0; $x -lt $x1; $x += 1) {
        $c = $bmp.GetPixel($x, $y)
        $bright[($y-$y0)*($x1-$x0) + ($x-$x0)] = ($c.R -gt 240 -and $c.G -gt 240 -and $c.B -gt 240)
    }
}
$sx = 0.0; $sy = 0.0; $n = 0; $minx = $W; $maxx = 0; $miny = $H; $maxy = 0
$wb = 0; $bl = 0; $rd = 0
for ($y = $y0; $y -lt $y1; $y += 1) {
    for ($x = $x0; $x -lt $x1; $x += 1) {
        $dx = $x - $CX; $dy = $y - $CY
        if ($dx*$dx + $dy*$dy -gt $Radius*$Radius) { continue }
        $c = $bmp.GetPixel($x, $y)
        $r = $c.R; $g = $c.G; $b = $c.B
        $isw = ($r -gt 240 -and $g -gt 240 -and $b -gt 240)
        $isr = ($r -gt 140 -and $r - ($g + $b)/2 -gt 80 -and $g -lt 70 -and $b -lt 70)
        $isb = $false
        if ($r -lt 45 -and $g -lt 45 -and $b -lt 45) {
            # рядом (в 25 px) яркий пиксель тора
            $found = $false
            for ($dy2 = -25; $dy2 -le 25; $dy2 += 5) {
                for ($dx2 = -25; $dx2 -le 25; $dx2 += 5) {
                    $xx = $x + $dx2; $yy = $y + $dy2
                    if ($xx -lt $x0 -or $xx -ge $x1 -or $yy -lt $y0 -or $yy -ge $y1) { continue }
                    if ($bright[($yy-$y0)*($x1-$x0) + ($xx-$x0)]) { $found = $true; break }
                }
                if ($found) { break }
            }
            $isb = $found
        }
        if ($isw -or $isr -or $isb) {
            $sx += $x; $sy += $y; $n += 1
            if ($isw) { $wb += 1 }; if ($isb) { $bl += 1 }; if ($isr) { $rd += 1 }
            if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x }
            if ($y -lt $miny) { $miny = $y }; if ($y -gt $maxy) { $maxy = $y }
        }
    }
}
Write-Output ("{0} tor n={1} (white={2} black={3} red={4}) centroid=({5},{6}) bboxcenter=({7},{8}) rx={9} ry={10} ratio={11} offset_centroid=({12},{13}) offset_bbox=({14},{15})" -f `
    (Split-Path $File -Leaf), $n, $wb, $bl, $rd, `
    [math]::Round($sx/$n,1), [math]::Round($sy/$n,1), `
    [math]::Round(($minx+$maxx)/2,1), [math]::Round(($miny+$maxy)/2,1), `
    [math]::Round(($maxx-$minx)/2,1), [math]::Round(($maxy-$miny)/2,1), `
    [math]::Round(($maxx-$minx)/($maxy-$miny),3), `
    [math]::Round($sx/$n - $CX,1), [math]::Round($sy/$n - $CY,1), `
    [math]::Round(($minx+$maxx)/2 - $CX,1), [math]::Round(($miny+$maxy)/2 - $CY,1))
$bmp.Dispose()
