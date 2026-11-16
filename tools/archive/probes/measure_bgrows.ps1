param([string]$Path)
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $Path
$W = $bmp.Width; $H = $bmp.Height
$rect = New-Object System.Drawing.Rectangle 0,0,$W,$H
$data = [System.Drawing.Imaging.BitmapFile]
$bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$stride = $bd.Stride
$bytes = New-Object byte[] ($stride * $H)
[System.Runtime.InteropServices.Marshal]::Copy($bd.Scan0, $bytes, 0, $bytes.Length)
$bmp.UnlockBits($bd)
$avg = New-Object double[] $H
for ($y = 0; $y -lt $H; $y++) {
  $s = 0
  for ($x = 0; $x -lt $W; $x += 32) {
    $o = $y * $stride + $x * 3
    $s += $bytes[$o] + $bytes[$o+1] + $bytes[$o+2]
  }
  $avg[$y] = $s / ($W / 32)
}
$first = -1; $last = -1
for ($y = 0; $y -lt $H; $y++) { if ($avg[$y] -gt 20) { if ($first -lt 0) { $first = $y }; $last = $y } }
"$Path size=$W x $H first-nonblack-row=$first last-nonblack-row=$last top-black=$first bottom-black=$($H-1-$last)"
# profile every 64 rows
for ($y = 0; $y -lt $H; $y += 64) { "row {0} avg={1:N1}" -f $y, $avg[$y] }
