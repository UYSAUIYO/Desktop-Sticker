Add-Type @'
using System;
using System.Runtime.InteropServices;
public class M {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    public struct POINT { public int x, y; }
}
'@
[M]::SetProcessDPIAware() | Out-Null
# 省赛证书磁贴:文件夹分区 (1180,340) + 图标区中心 (144,134) = 物理 (1324, 474)
[M]::SetCursorPos(1324, 474) | Out-Null
$p = New-Object M+POINT
[M]::GetCursorPos([ref]$p) | Out-Null
Write-Host ("cursor at physical ({0},{1})" -f $p.x, $p.y)
Start-Sleep -Milliseconds 200
[M]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
[M]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 120
[M]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
[M]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
Write-Host 'double-click injected'
Start-Sleep -Seconds 3
