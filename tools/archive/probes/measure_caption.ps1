param([string]$Png, [int]$X0 = 700, [int]$Y0 = 300, [int]$W = 2450, [int]$H = 300, [int]$T = 170, [int]$StepX = 2)
# measure_caption.ps1 - найти текстовые полосы в области заголовка и для каждой
# мерить ширину/высоту ink-бокса. Протокол тот же, что measure_textrows (яркость >= T).
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
if (($X0 + $W) -gt $src.Width) { $W = $src.Width - $X0 }
if (($Y0 + $H) -gt $src.Height) { $H = $src.Height - $Y0 }
"image $($src.Width)x$($src.Height) region ($X0,$Y0)+$W`x$H threshold $T"

$bands = New-Object System.Collections.ArrayList
$curTop = -1
for ($row = 0; $row -lt $H; $row++) {
  $yy = $Y0 + $row
  $cnt = 0
  for ($col = 0; $col -lt $W; $col += $StepX) {
    $c = $src.GetPixel($X0 + $col, $yy)
    if (($c.R + $c.G + $c.B) / 3 -ge $T) { $cnt = $cnt + 1 }
  }
  if ($cnt -gt 0) {
    if ($curTop -lt 0) { $curTop = $row }
    $curBottom = $row
  } elseif ($curTop -ge 0) {
    [void]$bands.Add([pscustomobject]@{ Top=$curTop; Bottom=$curBottom })
    $curTop = -1
  }
}
if ($curTop -ge 0) { [void]$bands.Add([pscustomobject]@{ Top=$curTop; Bottom=$curBottom }) }

foreach ($b in $bands) {
  $minx = 999999; $maxx = -1; $cnt = 0
  for ($yy = $Y0 + $b.Top; $yy -le $Y0 + $b.Bottom; $yy++) {
    for ($col = 0; $col -lt $W; $col += $StepX) {
      $xx = $X0 + $col
      $c = $src.GetPixel($xx, $yy)
      if (($c.R + $c.G + $c.B) / 3 -ge $T) {
        $cnt = $cnt + 1
        if ($xx -lt $minx) { $minx = $xx }
        if ($xx -gt $maxx) { $maxx = $xx }
      }
    }
  }
  if ($maxx -lt 0) { continue }
  $h = $b.Bottom - $b.Top + 1
  $w = $maxx - $minx + 1
  "BAND y=$($Y0+$b.Top)..$($Y0+$b.Bottom) height=$h  ink x=$minx..$maxx width=$w  aspect={0:N2}  center_x={1:N1}  pixels=$cnt" -f ($w / $h), (($minx + $maxx) / 2.0)
}
$src.Dispose()
