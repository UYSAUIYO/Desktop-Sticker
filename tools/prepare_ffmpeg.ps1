# 按固定版本 + SHA-256 校验获取 FFmpeg 负载到 tools/ffmpeg/
# 二进制不入版本库（见 .gitignore），仅本脚本可再生成。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_ffmpeg.ps1 [-Force]
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# 固定版本（同时写在 third_party/FFmpeg-NOTICE.txt，改动必须同步）
$ZipName  = 'ffmpeg-n8.1.2-53-g1005b294ff-win64-lgpl-shared-8.1.zip'
$BaseUrl  = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-09-16-19-44'
$Expected = 'a654407793b1caef118550de3b99e46299dcabc6649ccf9a3a325f41ff4ea414'

$ToolsDir   = $PSScriptRoot
$TargetDir  = Join-Path $ToolsDir 'ffmpeg'        # 运行期负载：ffmpeg.exe + av*.dll + 许可
$SdkDir     = Join-Path $ToolsDir 'ffmpeg-sdk'    # 构建期头文件与导入库（include + lib）
$RepoRoot   = Split-Path -Parent $ToolsDir
$ThirdParty = Join-Path $RepoRoot 'third_party'

function Test-Payload {
    param([string]$Dir)
    if (-not (Test-Path (Join-Path $Dir 'ffmpeg.exe'))) { return $false }
    $dlls = @(Get-ChildItem -Path $Dir -Filter 'av*.dll' -ErrorAction SilentlyContinue)
    return ($dlls.Count -ge 4)
}

function Test-Sdk {
    param([string]$Dir)
    return (Test-Path (Join-Path $Dir 'include\libavformat\avformat.h')) -and
           (Test-Path (Join-Path $Dir 'include\libavcodec\avcodec.h'))
}

if ((Test-Payload $TargetDir) -and (Test-Sdk $SdkDir) -and -not $Force) {
    Write-Host "[skip] payload and sdk already present (use -Force to refetch)"
    exit 0
}

# 许可正文必须先存在于仓库内，否则拒绝生成负载（合规要求随负载保留声明）
foreach ($required in @('LICENSE-FFmpeg.txt', 'OpenH264-LICENSE.txt')) {
    $p = Join-Path $ThirdParty $required
    if (-not (Test-Path $p)) { throw "license text missing: $p" }
}

$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("dstk-ffmpeg-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Work -Force | Out-Null

try {
    $zipPath = Join-Path $Work $ZipName
    $url = "$BaseUrl/$ZipName"
    Write-Host "[1/4] downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing

    Write-Host "[2/4] verifying sha256"
    $actual = (Get-FileHash -Path $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $ZipName`n  expected=$Expected`n  actual  =$actual"
    }

    Write-Host "[3/4] extracting"
    Expand-Archive -Path $zipPath -DestinationPath $Work -Force

    $packageRoot = Get-ChildItem -Path $Work -Directory |
                   Where-Object { Test-Path (Join-Path $_.FullName 'bin\ffmpeg.exe') } |
                   Select-Object -First 1
    if (-not $packageRoot) { throw 'ffmpeg.exe not found inside archive' }
    $binDir = Join-Path $packageRoot.FullName 'bin'

    # 运行期负载
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    Copy-Item -Path (Join-Path $binDir 'ffmpeg.exe') -Destination $TargetDir -Force
    Copy-Item -Path (Join-Path $binDir 'av*.dll')   -Destination $TargetDir -Force
    Copy-Item -Path (Join-Path $binDir 'sw*.dll')   -Destination $TargetDir -Force

    # 构建期 SDK：动态加载 av*.dll 仍需真实头文件来取函数声明（不链接导入库）
    $sdkInclude = Join-Path $packageRoot.FullName 'include'
    if (-not (Test-Path $sdkInclude)) { throw 'archive does not contain include\ (need a -shared build)' }
    New-Item -ItemType Directory -Path $SdkDir -Force | Out-Null
    Copy-Item -Path $sdkInclude -Destination $SdkDir -Recurse -Force

    Write-Host "[4/4] copying license texts"
    Copy-Item -Path (Join-Path $ThirdParty 'LICENSE-FFmpeg.txt')   -Destination (Join-Path $TargetDir 'LICENSE-FFmpeg.txt')   -Force
    Copy-Item -Path (Join-Path $ThirdParty 'OpenH264-LICENSE.txt') -Destination (Join-Path $TargetDir 'LICENSE-OpenH264.txt') -Force
}
finally {
    Remove-Item -Path $Work -Recurse -Force -ErrorAction SilentlyContinue
}

$files = @(Get-ChildItem -Path $TargetDir)
Write-Host "[done] $($files.Count) files in $TargetDir"
$files | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host "[done] build-time headers in $SdkDir\include"
