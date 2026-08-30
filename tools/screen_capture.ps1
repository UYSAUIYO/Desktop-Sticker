# Capture the screen (physical pixels) and crop the right-side zone card area for inspection
param([string]$OutPath = "$env:TEMP\ds_screen.png")
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Win32 -Name Cap -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
[DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
'@
[Win32.Cap]::SetProcessDPIAware() | Out-Null
$w = [Win32.Cap]::GetSystemMetrics(0)
$h = [Win32.Cap]::GetSystemMetrics(1)
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$g.Dispose()
# 右侧列卡片大约在 x = W-560..W，纵向取中段
$cropW = 560
$cropH = [int]($h * 0.62)
$crop = $bmp.Clone([System.Drawing.Rectangle]::new($w - $cropW, [int]($h * 0.18), $cropW, $cropH), $bmp.PixelFormat)
$crop.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose(); $crop.Dispose()
Write-Host "saved $OutPath ($cropW x $cropH, screen ${w}x${h})"
