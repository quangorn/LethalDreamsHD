param([string]$Png,[int]$X0,[int]$Y0,[int]$W,[int]$H,[int]$T=170)
Add-Type -AssemblyName System.Drawing
$b=[System.Drawing.Bitmap]::FromFile((Resolve-Path $Png))
$bands=@()
$cur=-1; $bot=-1
for($y=0;$y -lt $H;$y++){
  $c=0
  for($x=0;$x -lt $W;$x+=2){
    $p=$b.GetPixel($X0+$x,$Y0+$y).ToArgb()
    if((($p -shr 16) -band 255)+(($p -shr 8) -band 255)+($p -band 255) -ge 3*$T){$c++}
  }
  if($c -gt 0){ if($cur -lt 0){$cur=$y}; $bot=$y }
  elseif($cur -ge 0){ $bands += ,@($cur,$bot); $cur=-1 }
}
if($cur -ge 0){ $bands += ,@($cur,$bot) }
foreach($bb in $bands){
  $t=[int]$bb[0]; $bt=[int]$bb[1]
  $minx=999999; $maxx=-1
  for($y=$t;$y -le $bt;$y++){
    for($x=0;$x -lt $W;$x++){
      $p=$b.GetPixel($X0+$x,$Y0+$y).ToArgb()
      if((($p -shr 16) -band 255)+(($p -shr 8) -band 255)+($p -band 255) -ge 3*$T){
        if($X0+$x -lt $minx){$minx=$X0+$x}; if($X0+$x -gt $maxx){$maxx=$X0+$x}
      }
    }
  }
  "BAND y=$($Y0+$t)..$($Y0+$bt) h=$($bt-$t+1) x=$minx..$maxx w=$($maxx-$minx+1)"
}
$b.Dispose()
