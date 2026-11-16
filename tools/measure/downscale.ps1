param([Parameter(Mandatory=$true)][string]$In, [Parameter(Mandatory=$true)][string]$Out, [int]$MaxW = 1200)
# Уменьшить снимок до MaxW по ширине (сохраняя пропорции) - для просмотра целиком
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile($In)
$w = $bmp.Width; $h = $bmp.Height
if ($w -le $MaxW) { $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); "saved $Out ${w}x${h}"; exit 0 }
$nw = $MaxW; $nh = [int]($h * $MaxW / $w)
$nb = New-Object System.Drawing.Bitmap $nw, $nh
$g = [System.Drawing.Graphics]::FromImage($nb)
$g.DrawImage($bmp, 0, 0, $nw, $nh)
$nb.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"saved $Out ${nw}x${nh} (from ${w}x${h})"
$bmp.Dispose(); $nb.Dispose()
