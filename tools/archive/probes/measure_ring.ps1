param(
    [Parameter(Mandatory=$true)][string[]]$Files,
    [int]$XRange = 260
)

# Измеряет центр бело-чёрно-красного кольца-маркера:
#   y_top    = верхний ряд яркого белого (R,G,B>240) в полосе x W/2+-XRange
#   y_bot    = нижний ряд насыщенного красного (R>110, G<55, B<55) там же
#   центр Y  = (y_top + y_bot)/2, радиус Y = (y_bot - y_top)/2
#   центр X  = среднее x красных пикселей в нижних 30% арки
Add-Type -AssemblyName System.Drawing
foreach ($f in $Files) {
    if (-not (Test-Path $f)) { Write-Output "$f : NO FILE"; continue }
    $bmp = [System.Drawing.Bitmap]::FromFile($f)
    $W = $bmp.Width; $H = $bmp.Height
    $x0 = [int](($W - 2*$XRange)/2); $x1 = [int](($W + 2*$XRange)/2)
    if ($x0 -lt 0) { $x0 = 0 }; if ($x1 -gt $W) { $x1 = $W }
    $top = -1; $bot = -1
    for ($y=0; $y -lt $H; $y+=1) {
        $hit = 0
        for ($x=$x0; $x -lt $x1; $x+=2) {
            $c = $bmp.GetPixel($x,$y)
            if ($c.R -gt 240 -and $c.G -gt 240 -and $c.B -gt 240) { $hit += 1 }
        }
        if ($hit -ge 30) { $top = $y; break }
    }
    for ($y=$H-1; $y -ge 0; $y-=1) {
        $hit = 0
        for ($x=$x0; $x -lt $x1; $x+=2) {
            $c = $bmp.GetPixel($x,$y)
            if ($c.R -gt 110 -and $c.G -lt 55 -and $c.B -lt 55) { $hit += 1 }
        }
        if ($hit -ge 30) { $bot = $y; break }
    }
    # центр X: красные пиксели арки (нижние 40% дуги)
    $sum = 0.0; $n = 0
    $yl0 = $bot - [int](($bot - $top)*0.4)
    for ($y=$yl0; $y -le $bot; $y+=1) {
        for ($x=$x0; $x -lt $x1; $x+=1) {
            $c = $bmp.GetPixel($x,$y)
            if ($c.R -gt 110 -and $c.G -lt 55 -and $c.B -lt 55) { $sum += $x; $n += 1 }
        }
    }
    $cx = if ($n -gt 0) { [math]::Round($sum/$n,1) } else { -1 }
    $cy = if ($top -ge 0 -and $bot -ge 0) { [math]::Round(($top+$bot)/2,1) } else { -1 }
    $ry = if ($top -ge 0 -and $bot -ge 0) { [math]::Round(($bot-$top)/2,1) } else { -1 }
    Write-Output ("{0} size={1}x{2} center=({3},{4}) radiusY={5} top={6} bot={7} target=({8},{9})" -f `
        (Split-Path $f -Leaf), $W, $H, $cx, $cy, $ry, $top, $bot, ($W/2), ($H/2))
    $bmp.Dispose()
}
