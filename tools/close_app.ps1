# Gracefully close Desktop Sticker:
# 1) WM_CLOSE to its top-level windows (destroys XAML window, removes tray icon)
# 2) WM_QUIT to the window's thread (ends the XAML message loop -> App unwinds -> FeatureModule::Shutdown -> desktop icons restored)
Add-Type -Namespace Win32 -Name Msg -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool PostThreadMessage(uint threadId, uint Msg, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lp);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
'@

$p = Get-Process Desktop_Sticker -ErrorAction SilentlyContinue
if (-not $p) { Write-Host 'not running'; exit 0 }
$target = $p.Id
$threads = New-Object System.Collections.Generic.List[uint32]
$cb = [Win32.Msg+EnumWindowsProc]{
    param($h, $l)
    [uint32]$wpid = 0
    [Win32.Msg]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
    if ($wpid -eq $script:target) {
        $threads.Add([Win32.Msg]::GetWindowThreadProcessId($h, [ref]$wpid)) | Out-Null
        [Win32.Msg]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_CLOSE
    }
    return $true
}
[Win32.Msg]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Start-Sleep -Seconds 2
if (Get-Process Desktop_Sticker -ErrorAction SilentlyContinue) {
    foreach ($t in $threads) { [Win32.Msg]::PostThreadMessage($t, 0x0012, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }  # WM_QUIT
    Start-Sleep -Seconds 3
}
if (Get-Process Desktop_Sticker -ErrorAction SilentlyContinue) { Write-Host 'STILL RUNNING' } else { Write-Host 'EXITED' }
