param([Parameter(Mandatory=$true)][string]$In1, [Parameter(Mandatory=$true)][string]$In2, [Parameter(Mandatory=$true)][string]$Out,
      [int]$X0 = 1300, [int]$Y0 = 826, [int]$W = 400, [int]$H = 24, [int]$Scale = 3)
# Сложить два кропа (одинаковый регион из двух снимков) вертикально: сверху base, снизу new.
Add-Type -AssemblyName System.Drawing
$b1 = [System.Drawing.Bitmap]::FromFile($In1)
$b2 = [System.Drawing.Bitmap]::FromFile($In2)
$c1 = $b1.Clone((New-Object System.Drawing.Rectangle($X0,$Y0,$W,$H)), $b1.PixelFormat)
$c2 = $b2.Clone((New-Object System.Drawing.Rectangle($X0,$Y0,$W,$H)), $b2.PixelFormat)
$nw = $W*$Scale; $nh = $H*$Scale
$nb = New-Object System.Drawing.Bitmap $nw, ($nh*2 + $Scale)
$g = [System.Drawing.Graphics]::FromImage($nb)
$g.Clear([System.Drawing.Color]::FromArgb(90,90,90))
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.DrawImage($c1, 0, 0, $nw, $nh)
$g.DrawImage($c2, 0, $nh + $Scale, $nw, $nh)
$nb.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"stacked $Out ($nw x $($nh*2+$Scale)) region ($X0,$Y0)+$W`x$H scale=$Scale  top=In1 bottom=In2"
$b1.Dispose(); $b2.Dispose(); $c1.Dispose(); $c2.Dispose(); $nb.Dispose()
