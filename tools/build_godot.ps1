# 编译 Desktop Sticker 的 Godot fork（壁纸运行库 / 编辑器）。
# 前置：
#   1) 源码已随仓库提供（tools/godot-src/，vendored）；
#   2) py -3 -m pip install scons；
#   3) 首次编译 D3D12/ANGLE/AccessKit 需先跑官方依赖脚本（缺依赖会明确报错并给出命令）：
#        py -3 misc\scripts\install_d3d12_sdk_windows.py
#        py -3 misc\scripts\install_angle.py
#        py -3 misc\scripts\install_accesskit.py
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1               # 默认：壁纸运行库（shared_library）
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1 -Target Editor
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_godot.ps1 -Target Both -Clean
[CmdletBinding()]
param(
    [ValidateSet('RuntimeLib', 'Editor', 'Both')]
    [string]$Target = 'RuntimeLib',
    [switch]$Clean,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

$ToolsDir = $PSScriptRoot
$SrcDir   = Join-Path $ToolsDir 'godot-src'

if (-not (Test-Path (Join-Path $SrcDir 'SConstruct'))) {
    throw "Godot source missing: run tools/prepare_godot_src.ps1 first."
}

# 解析可用的 Python（SCons 需已安装）
$pyExe = $null
$pyPre = @()
foreach ($cand in @(@('py', '-3'), @('python'))) {
    $exe = Get-Command $cand[0] -ErrorAction SilentlyContinue
    if (-not $exe) { continue }
    $pre = @()
    if ($cand.Count -gt 1) { $pre = $cand[1..($cand.Count - 1)] }
    & $exe.Source @pre -c "import SCons" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $pyExe = $exe.Source
        $pyPre = $pre
        break
    }
}
if (-not $pyExe) {
    throw "SCons not found. Install it first: py -3 -m pip install scons"
}

if ($Jobs -le 0) {
    $Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
}

if ($Clean) {
    Write-Host "[clean] removing bin/ and sconsign (full rebuild on next build)"
    Remove-Item -Path (Join-Path $SrcDir 'bin') -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path (Join-Path $SrcDir '.sconsign.dblite') -Force -ErrorAction SilentlyContinue
}

$common = @('platform=windows', 'arch=x86_64', "-j$Jobs")
$variants = @()
if ($Target -in @('RuntimeLib', 'Both')) {
    $variants += , @('target=template_release', 'library_type=shared_library')
}
if ($Target -in @('Editor', 'Both')) {
    $variants += , @('target=editor')
}

foreach ($v in $variants) {
    Write-Host "[build] scons $($v -join ' ') $($common -join ' ')"
    Push-Location $SrcDir
    try {
        & $pyExe @pyPre -m SCons @common @v
        if ($LASTEXITCODE -ne 0) {
            throw "Godot build failed ($($v -join ' ')). Missing build deps? See the header of this script."
        }
    } finally {
        Pop-Location
    }
}

$binDir = Join-Path $SrcDir 'bin'
Write-Host "[done] artifacts in $binDir :"
Get-ChildItem -Path $binDir -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like 'godot*' } |
    ForEach-Object { Write-Host ("  {0}  ({1:N1} MB)" -f $_.Name, ($_.Length / 1MB)) }
