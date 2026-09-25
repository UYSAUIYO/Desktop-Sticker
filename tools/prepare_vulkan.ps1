# 按固定版本获取 Vulkan 编译期头文件到 tools/vulkan-sdk/
# 头文件不入版本库（见 .gitignore），仅本脚本可再生成。
#
# 只要头文件：运行时是动态加载系统 vulkan-1.dll（见规格 §8.1），
# 所以不装 Vulkan SDK、不链接导入库、不注册任何东西。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_vulkan.ps1 [-Force]
[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

# 固定版本（同时写在 third_party/Vulkan-NOTICE.txt，改动必须同步）
# 选 1.4.321 是贴着本机 loader 版本（vulkaninfo 报 1.4.321），避免头文件跑到运行时前面
$HeadersTag    = 'vulkan-sdk-1.4.321.0'
$HeadersCommit = '2cd90f9d20df57eac214c148f3aed885372ddcfe'
$HppTag        = 'v1.4.321'
$HppCommit     = '78243585183d42c93eeb2c57f9a194c7cac40bcc'

$ToolsDir  = $PSScriptRoot
$SdkDir    = Join-Path $ToolsDir 'vulkan-sdk'
$HeadersDir = Join-Path $SdkDir 'Vulkan-Headers'
$HppDir     = Join-Path $SdkDir 'Vulkan-Hpp'

function Test-Headers {
    return (Test-Path (Join-Path $HeadersDir 'include\vulkan\vulkan.h')) -and
           (Test-Path (Join-Path $HeadersDir 'include\vulkan\vulkan_core.h')) -and
           (Test-Path (Join-Path $HppDir 'vulkan\vulkan.hpp')) -and
           (Test-Path (Join-Path $HppDir 'vulkan\vulkan_raii.hpp'))
}

# 按固定提交取一份仓库；已存在则只校验提交对不对（不对就重取）
function Get-Pinned {
    param([string]$Url, [string]$Tag, [string]$Commit, [string]$Dir)
    if (Test-Path $Dir) {
        $head = (& git -C $Dir rev-parse HEAD 2>$null)
        if ($LASTEXITCODE -eq 0 -and $head -and $head.StartsWith($Commit.Substring(0, 7))) {
            Write-Host "[skip] $Dir already at $($Commit.Substring(0,7))"
            return
        }
        Write-Host "[warn] $Dir is at '$head', expected $Commit -> refetching"
        Remove-Item -Recurse -Force $Dir
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $Dir) -Force | Out-Null
    Write-Host "[get ] $Url @ $Tag"
    & git clone --quiet --branch $Tag --depth 1 $Url $Dir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed: $Url" }
    # depth 1 也能 checkout 指定提交（只要它在被 clone 的那条历史里）；取不到就报错而不是凑合用
    & git -C $Dir checkout --quiet $Commit
    if ($LASTEXITCODE -ne 0) { throw "pinned commit not reachable: $Commit in $Dir" }
}

if ((Test-Headers) -and -not $Force) {
    Write-Host "[skip] vulkan headers already present (use -Force to refetch)"
    exit 0
}
if ($Force -and (Test-Path $SdkDir)) {
    Remove-Item -Recurse -Force $SdkDir
}

Get-Pinned -Url 'https://github.com/KhronosGroup/Vulkan-Headers.git' -Tag $HeadersTag -Commit $HeadersCommit -Dir $HeadersDir
Get-Pinned -Url 'https://github.com/KhronosGroup/Vulkan-Hpp.git' -Tag $HppTag -Commit $HppCommit -Dir $HppDir

if (-not (Test-Headers)) { throw "payload incomplete under $SdkDir" }

Write-Host "[done] $SdkDir"
Write-Host "  include: $HeadersDir\include ; $HppDir"
