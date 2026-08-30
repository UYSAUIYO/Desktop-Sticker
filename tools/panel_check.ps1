Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public class V {
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern int GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public struct POINT { public int x, y; }
    public struct RECT { public int L, T, R, B; }
}
'@
$pt = New-Object V+POINT
$pt.x = 960; $pt.y = 431
$hit = [V]::WindowFromPoint($pt)
$sb = New-Object System.Text.StringBuilder 256
[void][V]::GetClassName($hit, $sb, 256)
$pid2 = [uint32]0
[void][V]::GetWindowThreadProcessId($hit, [ref]$pid2)
$fg = [V]::GetForegroundWindow()
Write-Host ("WindowFromPoint(960,431): class={0} hwnd={1} pid={2} visible={3}" -f $sb.ToString(), $hit, $pid2, [V]::IsWindowVisible($hit))
$launcher = [IntPtr]0x480DC4
$sb2 = New-Object System.Text.StringBuilder 256
[void][V]::GetClassName($launcher, $sb2, 256)
$r = New-Object V+RECT
[void][V]::GetWindowRect($launcher, [ref]$r)
Write-Host ("launcher hwnd: class={0} visible={1} rect=({2},{3})-({4},{5})" -f $sb2.ToString(), [V]::IsWindowVisible($launcher), $r.L, $r.T, $r.R, $r.B)
Write-Host ("foreground hwnd: {0} (launcher: {1})" -f $fg, ($fg -eq $launcher))
