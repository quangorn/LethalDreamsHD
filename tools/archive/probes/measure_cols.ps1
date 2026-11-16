param([string]$Png,[int]$X0,[int]$Y0,[int]$W,[int]$H,[int]$T)
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$cols=@()
for($x=0;$x -lt $W;$x++){
  $c=0
  for($y=0;$y -lt $H;$y++){
    $p=$b.GetPixel($X0+$x,$Y0+$y).ToArgb()
    $r=($p -shr 16) -band 255; $g=($p -shr 8) -band 255; $bl=$p -band 255
    if(($r+$g+$bl)/3 -ge $T){$c++}
  }
  $cols+=$c
}
$first=-1; $last=-1
for($x=0;$x -lt $W;$x++){ if($cols[$x] -gt 0){ if($first -lt 0){$first=$x}; $last=$x } }
"text span x=$first..$last width=$($last-$first+1)"
