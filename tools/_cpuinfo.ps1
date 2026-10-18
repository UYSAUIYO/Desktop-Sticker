# 查询本机 CPU 可用指令集（调用 kernel32!IsProcessorFeaturePresent，无需外部工具）。
$sig = @'
using System.Runtime.InteropServices;
public static class CpuFeat {
    [DllImport("kernel32.dll")] private static extern bool IsProcessorFeaturePresent(uint f);
    public static bool Has(uint f) { return IsProcessorFeaturePresent(f); }
}
'@
Add-Type -TypeDefinition $sig

$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
Write-Output ("CPU        : {0}" -f $cpu.Name)
Write-Output ("Arch       : {0}  ({1} cores / {2} logical)" -f $env:PROCESSOR_ARCHITECTURE, $cpu.NumberOfCores, $cpu.NumberOfLogicalProcessors)
Write-Output ("MaxClockMHz: {0}" -f $cpu.MaxClockSpeed)
Write-Output ""

# Windows 的 PF_* 常量（winnt.h）
$feats = [ordered]@{
    'MMX'      = 3
    'SSE'      = 6
    'SSE2'     = 10
    'SSE3'     = 13
    'AVX'      = 39
    'AVX2'     = 40
    'AVX512F'  = 41
}
foreach ($k in $feats.Keys) {
    $ok = [CpuFeat]::Has([uint32]$feats[$k])
    Write-Output ("{0,-9}: {1}" -f $k, $(if ($ok) { 'YES' } else { 'no' }))
}
