# 按固定版本 + SHA-256 校验获取 Godot 源码到 tools/godot-src/，并应用 fork 补丁集。
# 说明：tools/godot-src/ 已 vendoring 进本仓库（git 跟踪）；本脚本用于重建/恢复
#       一份干净副本（灾难恢复、与上游比对），或首次克隆后补齐被裁掉的副本。
# 用途：场景壁纸的 Godot 编辑器 / 运行库 fork 源码（见 .zcode/plans/ 的场景壁纸计划）。
# 二进制与源码均不入版本库（见 .gitignore），仅本脚本可再生成。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_godot_src.ps1 [-Force]
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# 固定版本（改动必须同步计划文档里的引擎版本）
$Version  = '4.7.2-stable'
$ZipName  = "godot-$Version.zip"
$Url      = "https://github.com/godotengine/godot/archive/refs/tags/$Version.zip"
$Expected = 'f0c95e750cb5f2a75c9ba54103722b241b185c4ea92c15d914575da95a1b3cf0'   # 2026-09-27 实测

$ToolsDir  = $PSScriptRoot
$TargetDir = Join-Path $ToolsDir 'godot-src'
$PatchDir  = Join-Path $ToolsDir 'godot-fork\patches'

# 应用 tools/godot-fork/patches/*.patch（幂等：已打过的补丁跳过，冲突则报错）
function Invoke-ForkPatches {
    param([string]$Dir)
    if (-not (Test-Path $PatchDir)) { return }
    $patches = Get-ChildItem -Path $PatchDir -Filter '*.patch' | Sort-Object Name
    if (-not $patches) { return }
    Push-Location $Dir
    try {
        foreach ($p in $patches) {
            git apply --check $p.FullName 2>$null
            if ($LASTEXITCODE -eq 0) {
                git apply $p.FullName
                if ($LASTEXITCODE -ne 0) { throw "failed to apply patch: $($p.Name)" }
                Write-Host "[patch] applied $($p.Name)"
            } else {
                git apply --reverse --check $p.FullName 2>$null
                if ($LASTEXITCODE -eq 0) {
                    Write-Host "[patch] already applied: $($p.Name)"
                } else {
                    throw "patch does not apply cleanly (source modified?): $($p.Name)"
                }
            }
        }
    } finally {
        Pop-Location
    }
}

if ((Test-Path (Join-Path $TargetDir 'SConstruct')) -and -not $Force) {
    Invoke-ForkPatches -Dir $TargetDir
    Write-Host "[skip] godot source already present (use -Force to refetch)"
    exit 0
}

$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("dstk-godot-src-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Work -Force | Out-Null

try {
    $zipPath = Join-Path $Work $ZipName
    Write-Host "[fetch] $Url"
    Invoke-WebRequest -Uri $Url -OutFile $zipPath -UseBasicParsing

    # SHA-256 校验（与 ffmpeg / godot 运行时负载同一套纪律）
    $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected) { throw "SHA-256 mismatch: expected $Expected, got $actual" }
    Write-Host "[ok] sha256 verified: $actual"

    Write-Host "[unpack] -> $TargetDir"
    if (Test-Path $TargetDir) { Remove-Item $TargetDir -Recurse -Force }
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    # tar.exe 对 1.4 万个文件比 Expand-Archive 快一个量级；Win10 1803+ 自带
    tar -xf $zipPath -C $TargetDir --strip-components=1
    if (-not (Test-Path (Join-Path $TargetDir 'SConstruct'))) { throw "source check failed after unpack" }

    Invoke-ForkPatches -Dir $TargetDir
    Write-Host "[done] godot source ready: $TargetDir"
} finally {
    if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
}
