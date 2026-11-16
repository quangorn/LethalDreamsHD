# measure_inkbox.ps1 - bbox ярких (текстовых) пикселей в прямоугольнике снимка + ширина/высота.
# Тот же протокол, что measure_textrows (порог яркости T), но меряет и горизонталь:
# возвращает ink bbox (ширина/высота в физических px) и долю ink в заданном rect.
param([string]$Png, [int]$X0 = 0, [int]$Y0 = 0, [int]$W = 0, [int]$H = 0, [int]$T = 170, [int]$StepX = 1, [int]$StepY = 1)

Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$pw = $src.Width; $ph = $src.Height
if (-not $W) { $W = $pw }
if (-not $H) { $H = $ph }
if (($X0 + $W) -gt $pw) { $W = $pw - $X0 }
if (($Y0 + $H) -gt $ph) { $H = $ph - $Y0 }
"image ${pw}x${ph} region ($X0,$Y0)+$W`x$H threshold $T"

$minx = 999999; $maxx = -1; $miny = 999999; $maxy = -1; $cnt = 0
for ($yy = $Y0; $yy -lt ($Y0 + $H); $yy += $StepY) {
  for ($xx = $X0; $xx -lt ($X0 + $W); $xx += $StepX) {
    $c = $src.GetPixel($xx, $yy)
    $lum = ($c.R + $c.G + $c.B) / 3
    if ($lum -ge $T) {
      $cnt = $cnt + 1
      if ($xx -lt $minx) { $minx = $xx }
      if ($xx -gt $maxx) { $maxx = $xx }
      if ($yy -lt $miny) { $miny = $yy }
      if ($yy -gt $maxy) { $maxy = $yy }
    }
  }
}
if ($maxx -lt 0) { "NO INK"; $src.Dispose(); exit 0 }
$iw = $maxx - $minx + 1; $ih = $maxy - $miny + 1
"INK BBOX x=$minx..$maxy  y=$miny..$maxy   width=$iw  height=$ih  pixels=$cnt"
"SHARE width/rect = {0:N3}   height/rect = {1:N3}   aspect = {2:N2}" -f ($iw / $W), ($ih / $H), ($iw / $ih)
$src.Dispose()
