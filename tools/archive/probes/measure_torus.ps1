param(
    [Parameter(Mandatory=$true)][string]$File,
    [int]$Radius = 380,
    [double]$CX = -1,
    [double]$CY = -1
)
# Точный центр кольца: берём пиксели кольца (яркий белый / насыщенный красный /
# чёрный рядом с ярким) внутри радиуса Radius от точки (CX,CY) (по умолчанию - центр
# изображения). Спираль в основном вне этого радиуса. Центр = центроид маски,
# rx/ry - полуразмеры (roundness).
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($File)
$W = $bmp.Width; $H = $bmp.Height
if ($CX -lt 0) { $CX = $W/2 }
if ($CY -lt 0) { $CY = $H/2 }
$x0 = [math]::Max(0, [int]($CX - $Radius)); $x1 = [math]::Min($W, [int]($CX + $Radius))
$y0 = [math]::Max(0, [int]($CY - $Radius)); $y1 = [math]::Min($H, [int]($CY + $Radius))
$sx = 0.0; $sy = 0.0; $n = 0; $minx = $W; $maxx = 0; $miny = $H; $maxy = 0
for ($y = $y0; $y -lt $y1; $y += 1) {
    for ($x = $x0; $x -lt $x1; $x += 1) {
        $dx = $x - $CX; $dy = $y - $CY
        if ($dx*$dx + $dy*$dy -gt $Radius*$Radius) { continue }
        $c = $bmp.GetPixel($x, $y)
        $r = $c.R; $g = $c.G; $b = $c.B
        $m = ($r -gt 235 -and $g -gt 235 -and $b -gt 235) -or ($r -gt 150 -and $g -lt 70 -and $b -lt 70) -or ($r -lt 30 -and $g -lt 30 -and $b -lt 30)
        if ($m) {
            $sx += $x; $sy += $y; $n += 1
            if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x }
            if ($y -lt $miny) { $miny = $y }; if ($y -gt $maxy) { $maxy = $y }
        }
    }
}
$cxm = $sx/$n; $cym = $sy/$n
Write-Output ("{0} centroid=({1},{2}) bbox x={3}..{4} y={5}..{6} bboxcenter=({7},{8}) rx={9} ry={10} ratio={11} n={12} offset=({13},{14})" -f `
    (Split-Path $File -Leaf), [math]::Round($cxm,1), [math]::Round($cym,1), $minx, $maxx, $miny, $maxy, `
    [math]::Round(($minx+$maxx)/2,1), [math]::Round(($miny+$maxy)/2,1), `
    [math]::Round(($maxx-$minx)/2,1), [math]::Round(($maxy-$miny)/2,1), [math]::Round(($maxx-$minx)/($maxy-$miny),3), $n, `
    [math]::Round($cxm - $CX,1), [math]::Round($cym - $CY,1))
$bmp.Dispose()
