# 用 fork 出来的 Godot 编辑器 headless 打包一个**加密** PCK。
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/pack_encrypted.ps1 `
#       -Source <要打包的目录> -Out <输出的 .pck 路径>
# 注：路径一律用正斜杠，PowerShell 与 Windows API 都能接受，也免得写出  这类转义陷阱。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Out
)
$ErrorActionPreference = 'Stop'

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$Editor   = Join-Path $RepoRoot 'tools/godot-src/bin/godot.windows.editor.x86_64.console.exe'
$ToolGd   = Join-Path $RepoRoot 'tools/wallpaper-studio/pack_encrypted.gd'
$KeyFile  = Join-Path $PSScriptRoot 'signing_key.txt'

if (-not (Test-Path $Editor)) {
    throw "editor binary missing: $Editor (run: tools/build_godot.ps1 -Target Editor)"
}
if (-not (Test-Path $KeyFile)) { throw "missing $KeyFile" }
$Key = (Get-Content -Raw $KeyFile).Trim()

# --script 只接受 res:// 路径，所以把工具脚本放进一个一次性的最小工程里跑。
$Scratch = Join-Path $env:TEMP ('dstk_pack_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Scratch | Out-Null
try {
    Set-Content -Path (Join-Path $Scratch 'project.godot') -Encoding utf8 -Value @"
config_version=5

[application]

config/name="dstk_pack"
"@
    Copy-Item $ToolGd (Join-Path $Scratch 'pack_encrypted.gd')

    # 传绝对路径：脚本里用文件系统 API 读源目录、写输出文件，绕开 res:// 的限制。
    $SourceAbs = (Resolve-Path $Source).Path
    $OutAbs    = [System.IO.Path]::GetFullPath($Out)
    $OutDir    = Split-Path -Parent $OutAbs
    if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

    Write-Host "[pack] $SourceAbs -> $OutAbs"
    & $Editor --headless --path $Scratch --script res://pack_encrypted.gd -- --source $SourceAbs --out $OutAbs --key $Key
    if ($LASTEXITCODE -ne 0) { throw "pack failed (exit $LASTEXITCODE)" }
} finally {
    Remove-Item -Recurse -Force $Scratch -ErrorAction SilentlyContinue
}
Write-Host "[done] $Out"
