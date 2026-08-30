# Check Desktop Sticker process: list top-level windows and their visibility
Add-Type -Namespace Win32 -Name Vis -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lp);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);
'@

$p = Get-Process Desktop_Sticker -ErrorAction SilentlyContinue
if (-not $p) { Write-Host 'NOT RUNNING'; exit 1 }
$target = $p.Id
$script:found = 0
$script:visible = 0
$cb = [Win32.Vis+EnumWindowsProc]{
    param($h, $l)
    [uint32]$wpid = 0
    [Win32.Vis]::GetWindowThreadProcessId($h, [ref]$wpid) | Out-Null
    if ($wpid -eq $script:target) {
        $sb = New-Object System.Text.StringBuilder 256
        [Win32.Vis]::GetWindowText($h, $sb, 256) | Out-Null
        $vis = [Win32.Vis]::IsWindowVisible($h)
        $script:found++
        if ($vis) { $script:visible++ }
        Write-Host ("hwnd={0} visible={1} title='{2}'" -f $h, $vis, $sb.ToString())
    }
    return $true
}
[Win32.Vis]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
Write-Host ("top-level windows: {0}, visible: {1}" -f $script:found, $script:visible)
