# measure_span.ps1 - горизонтальный размах ярких (текстовых) пикселей в прямоугольнике.
param([string]$Png, [int]$X0 = 0, [int]$Y0 = 0, [int]$W = 0, [int]$H = 0, [int]$T = 170, [int]$StepY = 1)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$pw = $src.Width; $ph = $src.Height
if (-not $W) { $W = $pw }
if (-not $H) { $H = $ph }
$minx = [int]::MaxValue; $maxx = -1; $count = 0
$rowfirst = -1; $rowlast = -1
for ($row = 0; $row -lt $H; $row += $StepY) {
  $yy = $Y0 + $row
  $any = $false
  for ($col = 0; $col -lt $W; $col += 1) {
    $c = $src.GetPixel($X0 + $col, $yy)
    $lum = ($c.R + $c.G + $c.B) / 3
    if ($lum -ge $T) {
      $xx = $X0 + $col
      if ($xx -lt $minx) { $minx = $xx }
      if ($xx -gt $maxx) { $maxx = $xx }
      $count++
      $any = $true
    }
  }
  if ($any) { if ($rowfirst -lt 0) { $rowfirst = $row }; $rowlast = $row }
}
"image ${pw}x${ph} region ($X0,$Y0)+$W`x$H T=$T"
if ($maxx -lt 0) { "no ink" } else {
  "span x = $minx .. $maxx  width = $($maxx - $minx + 1)  pixels = $count"
  "rows y = $($Y0 + $rowfirst) .. $($Y0 + $rowlast)  h = $($rowlast - $rowfirst + 1)"
}
$src.Dispose()
