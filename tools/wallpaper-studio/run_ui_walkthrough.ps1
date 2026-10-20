# 真实窗口走查：把 studio 工程同步到 <exeDir>\studio，然后**带窗口**跑一遍界面交互，
# 每一步截图。跟 run_tests.ps1 的区别：那边是 --headless，控件没有真实布局、也收不到输入。
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/run_ui_walkthrough.ps1
#   ... -Shots D:\some\dir        # 指定截图目录
#
# 退出码：0 全部断言通过（截图见目录），1 有断言失败。
[CmdletBinding()]
param([string]$Shots = '')

$ErrorActionPreference = 'Stop'

$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$OutDir    = Join-Path $RepoRoot 'Desktop Sticker/x64/Release/Desktop Sticker'
$StudioExe = Join-Path $OutDir 'dstk-studio.exe'
$Project   = Join-Path $OutDir 'studio'
$Source    = Join-Path $RepoRoot 'tools/wallpaper-studio/project'

if (-not (Test-Path $StudioExe)) { throw "missing $StudioExe - run build.bat first" }

# 和 run_tests.ps1 一样：.gd 改动不触发 MSBuild 重编，不重新同步就会跑上一次的副本。
Write-Host "[walk] sync $Source -> $Project"
robocopy $Source $Project /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "failed to sync studio project to $Project" }
Copy-Item (Join-Path $RepoRoot 'tools/wallpaper-studio/signing_key.txt') `
    (Join-Path $Project 'signing_key.txt') -Force

if ($Shots -eq '') { $Shots = Join-Path $RepoRoot 'build_tmp/ui-walkthrough' }
New-Item -ItemType Directory -Force $Shots | Out-Null

Push-Location $OutDir
try {
    & $StudioExe --script res://tests/ui_walkthrough.gd -- --shots $Shots
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

Write-Host "[walk] shots: $Shots"
exit $code
