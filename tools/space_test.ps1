Add-Type @'
using System;
using System.Runtime.InteropServices;
public class K {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern int GetForegroundWindow();
}
'@
# 桌面双击空格:两次按下-释放,间隔 120ms
[K]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero)       # down
[K]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero)       # up (KEYEVENTF_KEYUP)
Start-Sleep -Milliseconds 120
[K]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero)
[K]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero)
Start-Sleep -Seconds 2
Write-Host 'double-space injected on desktop'
