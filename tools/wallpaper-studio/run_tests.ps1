# Studio 无头测试。两阶段：先跑可同进程的用例，再单跑必须独占进程的用例。
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_tests.ps1 -Only <用例名>
#
# 必须用 dstk-studio.exe：模板 console exe 那次构建没开 disable_path_overrides=no，
# 喂 --path 会直接报 "compiled without support for path overrides"。
#
# 注意：不要写 `$code = Invoke-StudioTests`。PowerShell 的函数输出会被赋给变量，
# 那样连引擎的 stdout 一起吞掉，跑测试时终端一片空白、看起来像脚本没执行。
[CmdletBinding()]
param([string]$Only = '')
$ErrorActionPreference = 'Stop'

$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$OutDir    = Join-Path $RepoRoot 'Desktop Sticker/x64/Release/Desktop Sticker'
$StudioExe = Join-Path $OutDir 'dstk-studio.exe'
$Project   = Join-Path $OutDir 'studio'

if (-not (Test-Path $StudioExe)) { throw "missing $StudioExe - run build.bat first" }
if (-not (Test-Path (Join-Path $Project 'project.godot'))) { throw "missing studio project at $Project" }

function Invoke-StudioTests([string]$only) {
    $engineArgs = @('--headless', '--script', 'res://tests/run_tests.gd')
    if ($only -ne '') { $engineArgs += @('--', '--only=' + $only) }
    & $StudioExe @engineArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[studio] tests FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

if ($Only -ne '') {
    Invoke-StudioTests $Only
    exit 0
}

Invoke-StudioTests ''

# 独占用例：ProjectSettings.load_resource_pack 挂载后不可卸载，引擎又是进程级单例，
# 所以每个这样的用例都要单独一个进程。
foreach ($case in @('exporter_produces_loadable_pack')) {
    Invoke-StudioTests $case
}
exit 0
