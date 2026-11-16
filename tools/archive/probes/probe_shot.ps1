# Captures the full virtual screen (no process launch).
param([string]$Tag = "shot", [string]$ScratchRoot = $env:LD_SCRATCH, [string]$GameRoot = $env:LD_GAME, [string]$RepoRoot = $env:LD_REPO)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
$full = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp = New-Object System.Drawing.Bitmap $full.Width, $full.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($full.X, $full.Y, 0, 0, (New-Object System.Drawing.Size $full.Width, $full.Height))
$name = "$ScratchRoot\{0}.png" -f $Tag
$bmp.Save($name)
"saved " + $name + " size " + $full.Width + "x" + $full.Height
