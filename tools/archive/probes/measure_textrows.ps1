# measure_textrows.ps1 - построчный профиль ярких (текстовых) пикселей в прямоугольнике снимка.
# Позволяет измерить высоту текстовых полос в физических пикселях.
param([string]$Png, [int]$X0 = 0, [int]$Y0 = 0, [int]$W = 0, [int]$H = 0, [int]$T = 150, [int]$Step = 1, [int]$StepX = 3)

Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$pw = $src.Width; $ph = $src.Height
if (-not $W) { $W = $pw }
if (-not $H) { $H = $ph }
if (($X0 + $W) -gt $pw) { $W = $pw - $X0 }
if (($Y0 + $H) -gt $ph) { $H = $ph - $Y0 }
"image ${pw}x${ph} region ($X0,$Y0)+$W`x$H threshold $T"

$bands = New-Object System.Collections.ArrayList
$curTop = -1
for ($row = 0; $row -lt $H; $row += $Step) {
  $cnt = 0
  $yy = $Y0 + $row
  for ($col = 0; $col -lt $W; $col += $StepX) {
    $xx = $X0 + $col
    $c = $src.GetPixel($xx, $yy)
    $lum = ($c.R + $c.G + $c.B) / 3
    if ($lum -ge $T) { $cnt = $cnt + 1 }
  }
  if ($cnt -gt 0) {
    if ($curTop -lt 0) { $curTop = $row }
    $curBottom = $row
    "{0,4} {1,4}" -f ($Y0 + $row), $cnt
  } elseif ($curTop -ge 0) {
    [void]$bands.Add([pscustomobject]@{ Top=($Y0 + $curTop); Bottom=($Y0 + $curBottom); Height=($curBottom - $curTop + 1) })
    $curTop = -1
  }
}
if ($curTop -ge 0) { [void]$bands.Add([pscustomobject]@{ Top=($Y0 + $curTop); Bottom=($Y0 + $curBottom); Height=($curBottom - $curTop + 1) }) }
"BANDS:"
foreach ($b in $bands) { " top={0} bottom={1} height={2}" -f $b.Top, $b.Bottom, $b.Height }
$src.Dispose()
