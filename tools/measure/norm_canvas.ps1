# norm_canvas.ps1 - переснимает снимок в КАНВАСНЫЕ координаты (канвас 1024x768,
# в 16:9 видимый канвас x от -170 до 1196 = ширина 1366).
#   4:3 : px = 1.875*canvasX, py = 1.875*canvasY  (k=1, dx=0)
#   16:9: px = 2.8125*canvasX + 480, py = 2.8125*canvasY (k=0.75, dx=128*3.75)
# Выход: PNG в канвасном масштабе 1 px = 1 канвас, x=-170..1196 -> пиксель 0..1366.
param([string]$In, [string]$Out, [string]$Mode = "4k")
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($In)
$W = $b.Width; $H = $b.Height
if ($Mode -eq "43") { $sx = $W / 1024.0; $sy = $H / 768.0 }
else { $sy = $H / 768.0; $offx = 480.0 }
$CW = 1366; $CH = 768
$c = New-Object System.Drawing.Bitmap $CW, $CH
$g = [System.Drawing.Graphics]::FromImage($c)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
if ($Mode -eq "43") {
  # канвас x 0..1024 -> пиксель 170..1194 (в общей системе -170..1196)
  $src = New-Object System.Drawing.Rectangle 0, 0, $W, $H
  $dst = New-Object System.Drawing.RectangleF 170.0, 0.0, 1024.0, 768.0
  $g.DrawImage($b, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
}
else {
  # канвас x = (px - 480)/sy -> пиксель canvasX+170
  $x0 = ($offx / $sy) + 170.0
  $src = New-Object System.Drawing.Rectangle 0, 0, $W, $H
  $dst = New-Object System.Drawing.RectangleF $x0, 0.0, ($W / $sy), $CH
  $g.DrawImage($b, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
}
$c.Save($Out)
$g.Dispose(); $b.Dispose(); $c.Dispose()
"normalized $In -> $Out ($Mode, src ${W}x${H}, sy=$sy)"
