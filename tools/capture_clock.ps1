# Capture the clock widget region (top-center of screen)
param([string]$OutPath = "D:/project/Desktop Sticker/tools/ds_clock.png")
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace W -Name C -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
'@
[W.C]::SetProcessDPIAware() | Out-Null
$w = [W.C]::GetSystemMetrics(0)
$h = [W.C]::GetSystemMetrics(1)
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$g.Dispose()
$x = [int](($w - 540) / 2) - 20
$crop = $bmp.Clone([System.Drawing.Rectangle]::new($x, 10, 580, 330), $bmp.PixelFormat)
$crop.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose(); $crop.Dispose()
Write-Host "captured $OutPath"
