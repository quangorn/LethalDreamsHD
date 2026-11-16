# stack_cmp.ps1 - складывает два канвас-нормализованных снимка (4:3 сверху, 16:9 снизу)
# в один PNG с подписью; обе строки в ОДНОЙ канвасной системе координат.
param([string]$Top, [string]$Bottom, [string]$Out, [string]$Label = "")
Add-Type -AssemblyName System.Drawing
$a = [System.Drawing.Bitmap]::FromFile($Top)
$b = [System.Drawing.Bitmap]::FromFile($Bottom)
$sep = 16
$c = New-Object System.Drawing.Bitmap ($a.Width), ($a.Height + $b.Height + $sep)
$g = [System.Drawing.Graphics]::FromImage($c)
$g.Clear([System.Drawing.Color]::Magenta)
$g.DrawImageUnscaled($a, 0, 0)
$g.DrawImageUnscaled($b, 0, ($a.Height + $sep))
if ($Label) {
  $f = New-Object System.Drawing.Font "Consolas", 20
  $g.DrawString($Label, $f, [System.Drawing.Brushes]::Yellow, 10, ($a.Height + $sep - 14))
}
$c.Save($Out)
$g.Dispose(); $a.Dispose(); $b.Dispose(); $c.Dispose()
"stacked $Out"
