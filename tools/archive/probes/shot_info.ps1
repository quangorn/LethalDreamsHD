param([string]$Png, [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile($Png)
$w=$b.Width; $h=$b.Height
$sum=0; $nz=0
for($y=0;$y -lt $h;$y+=3){
  for($x=0;$x -lt $w;$x+=3){
    $p=$b.GetPixel($x,$y).ToArgb()
    $r=($p -shr 16) -band 255; $g=($p -shr 8) -band 255; $bl=$p -band 255
    $s=$r+$g+$bl; $sum+=$s
    if($s -gt 200){$nz++}
  }
}
"NZ samples=$nz total samples=$((([int]($h/3))*([int]($w/3)))) mean=$((($sum / ([int]($h/3)*[int]($w/3)))))"
$scaled = New-Object System.Drawing.Bitmap 960 ([int](960*$h/$w))
$g=[System.Drawing.Graphics]::FromImage($scaled)
$g.DrawImage($b,0,0,$scaled.Width,$scaled.Height)
$g.Dispose()
$out = "$ScratchRoot/ld/small_" + (Split-Path $Png -Leaf)
$scaled.Save($out)
"saved $out"
