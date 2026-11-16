param([string]$In,[string]$Out,[int]$X,[int]$Y,[int]$W,[int]$H)
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile($In)
$c=New-Object System.Drawing.Bitmap $W,$H
$g=[System.Drawing.Graphics]::FromImage($c)
$g.DrawImage($b,(New-Object System.Drawing.Rectangle 0,0,$W,$H),(New-Object System.Drawing.Rectangle $X,$Y,$W,$H),[System.Drawing.GraphicsUnit]::Pixel)
$c.Save($Out)
$g.Dispose(); $b.Dispose(); $c.Dispose()
"saved $Out"
