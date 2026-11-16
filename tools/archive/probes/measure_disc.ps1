param([Parameter(Mandatory=$true)][string]$Png, [int]$X0 = 0, [int]$Y0 = 0, [int]$W = 0, [int]$H = 0)
# Замер bbox яркого синего кольца (domain plate / портал) в зоне снимка.
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($Png)
if ($W -eq 0) { $W = $bmp.Width - $X0 }
if ($H -eq 0) { $H = $bmp.Height - $Y0 }
$minx = 1e9; $miny = 1e9; $maxx = -1; $maxy = -1; $cnt = 0
for ($y = $Y0; $y -lt $Y0 + $H; $y++) {
  for ($x = $X0; $x -lt $X0 + $W; $x++) {
    $c = $bmp.GetPixel($x, $y)
    $r = $c.R; $g = $c.G; $b = $c.B
    if ($b -gt 120 -and ($b - $g) -gt 60 -and ($r + $g + $b) -gt 300) {
      $cnt++
      if ($x -lt $minx) { $minx = $x }
      if ($x -gt $maxx) { $maxx = $x }
      if ($y -lt $miny) { $miny = $y }
      if ($y -gt $maxy) { $maxy = $y }
    }
  }
}
if ($cnt -eq 0) { "no ring pixels"; exit 0 }
$bw = $maxx - $minx + 1; $bh = $maxy - $miny + 1
"bbox x $($minx)..$($maxx) y $($miny)..$($maxy) w=$bw h=$bh ratio=$([math]::Round($bw/$bh,4)) px=$cnt center=($([math]::Round(($minx+$maxx)/2,1)), $([math]::Round(($miny+$maxy)/2,1)))"
$bmp.Dispose()
