Add-Type @'
using System;
using System.Runtime.InteropServices;
public class M2 {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
}
'@
[M2]::SetProcessDPIAware() | Out-Null
# 应用分区卡片中央 (40,60)-(380,300) -> (210,180)
[M2]::SetCursorPos(210, 180) | Out-Null
Start-Sleep -Milliseconds 200
# 向下滚 5 格 (负 delta = 向下; -120 的 uint32 表示)
$down = [uint32]4294967176
for ($i = 0; $i -lt 5; $i++) {
    [M2]::mouse_event(0x0800, 0, 0, $down, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
}
Write-Host 'wheel down x5 injected at (210,180)'
Start-Sleep -Seconds 2
