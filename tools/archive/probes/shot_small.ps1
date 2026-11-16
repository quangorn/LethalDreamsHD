param([string]$Png, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile($Png)
$w=$b.Width; $h=$b.Height
$sw=960; $sh=[int](960*$h/$w)
$scaled = New-Object System.Drawing.Bitmap($sw,$sh)
$g=[System.Drawing.Graphics]::FromImage($scaled)
$g.DrawImage($b,0,0,$sw,$sh)
$g.Dispose()
$out = "$ScratchRoot/ld/small_" + (Split-Path $Png -Leaf)
$scaled.Save($out)
"saved $out"
