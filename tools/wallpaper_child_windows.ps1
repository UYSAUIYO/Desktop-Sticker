param(
    [string]$ClassName = 'DesktopSticker.WallPaper.Window'
)

# 列出指定类名窗口及其子树（可见性/矩形/扩展样式）。
# 壁纸窗口是 WorkerW 的子窗口 —— 不是顶层窗口，因此必须在每个顶层窗口的
# 全部后代里找，EnumWindows 是找不到它的。
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;

public class ChildWin {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int m);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int m);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);

    public struct RECT { public int L, T, R, B; }

    public static string Cls(IntPtr h) {
        var cn = new StringBuilder(256);
        GetClassName(h, cn, 256);
        return cn.ToString();
    }

    public static string Info(IntPtr h, string indent) {
        var tx = new StringBuilder(256);
        GetWindowText(h, tx, 256);
        RECT r;
        GetWindowRect(h, out r);
        long ex = GetWindowLong(h, -20) & 0xffffffffL;
        long st = GetWindowLong(h, -16) & 0xffffffffL;
        return indent + Cls(h) + " vis=" + IsWindowVisible(h)
             + " style=0x" + st.ToString("X8") + " ex=0x" + ex.ToString("X8")
             + " rect=" + r.L + "," + r.T + "-" + r.R + "," + r.B
             + " text=\"" + tx.ToString() + "\"";
    }

    public static List<string> Find(string cls) {
        var outp = new List<string>();
        EnumWindows((h, l) => {
            IntPtr found = IntPtr.Zero;
            if (Cls(h) == cls) found = h;
            if (found == IntPtr.Zero) {
                EnumChildWindows(h, (c, l2) => {
                    if (Cls(c) == cls) { found = c; return false; }
                    return true;
                }, IntPtr.Zero);
            }
            if (found != IntPtr.Zero) {
                outp.Add("FOUND hwnd=" + found + " parent=" + Cls(h));
                outp.Add(Info(found, ""));
                EnumChildWindows(found, (c, l2) => {
                    outp.Add(Info(c, "  "));
                    EnumChildWindows(c, (g, l3) => { outp.Add(Info(g, "    ")); return true; }, IntPtr.Zero);
                    return true;
                }, IntPtr.Zero);
            }
            return true;
        }, IntPtr.Zero);
        return outp;
    }
}
"@

$lines = [ChildWin]::Find($ClassName)
if ($lines.Count -eq 0) {
    Write-Output "no window of class $ClassName found"
} else {
    $lines | ForEach-Object { Write-Output $_ }
}
