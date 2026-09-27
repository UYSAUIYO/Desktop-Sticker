# 编译 M1 宿主测试程序（cl.exe，自动进入 VS2022 x64 开发环境）。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/godot-host-test/build.ps1
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$Dir = $PSScriptRoot
$Src = Join-Path $Dir 'main.cpp'
$Out = Join-Path $Dir 'dstk-godot-host.exe'

# 定位 vcvars64.bat：vswhere 优先，其次常见安装路径
$vcvars = $null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) { $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat' }
}
if (-not $vcvars -or -not (Test-Path $vcvars)) {
    foreach ($cand in @(
            'D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
            'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
            'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
            'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat')) {
        if (Test-Path $cand) { $vcvars = $cand; break }
    }
}
if (-not $vcvars) { throw "vcvars64.bat not found; install VS2022 with the C++ workload." }

Write-Host "[build] cl.exe main.cpp -> dstk-godot-host.exe"
Push-Location $Dir
try {
    cmd /c "call `"$vcvars`" >nul && cl /nologo /utf-8 /std:c++20 /EHsc /O2 /W4 /Fe:dstk-godot-host.exe main.cpp /link /SUBSYSTEM:CONSOLE user32.lib advapi32.lib"
    if ($LASTEXITCODE -ne 0) { throw "host test build failed (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

Get-ChildItem -Path (Join-Path $Dir '*') -Include '*.obj', '*.ilk', '*.pdb' -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
Write-Host "[done] $Out"
