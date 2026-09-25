# 构建 GLSL -> SPIR-V 编译器（glslangValidator.exe）到 tools/shaderc/
# 产物不入版本库（见 .gitignore）；源码用 glslang 自己的 known_good.json 钉版本。
#
# 为什么是源码构建而不是预编译包：本机上下载 GitHub release 附件会超时（git 协议正常），
# 而且源码构建能把版本钉在 known_good.json 上、可复现。构建是离线的（依赖已在源码目录下）。
#
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare_shaderc.ps1 [-GlslangSrc D:\project\glslang] [-Force]
[CmdletBinding()]
param(
    [string]$GlslangSrc = 'D:\project\glslang',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ToolsDir  = $PSScriptRoot
$OutDir    = Join-Path $ToolsDir 'shaderc'          # 产物：glslangValidator.exe
$BuildDir  = Join-Path $OutDir 'build'              # 构建目录（out-of-source，不往源码目录里写）
$Exe       = Join-Path $OutDir 'glslangValidator.exe'

# 期望的第三方位置（由 glslang 的 update_glslang_sources.py 按 known_good.json 拉取）
$SpirvTools   = Join-Path $GlslangSrc 'External\spirv-tools'
$SpirvHeaders = Join-Path $SpirvTools 'external\spirv-headers'

if ((Test-Path $Exe) -and -not $Force) {
    Write-Host "[skip] $Exe already built (use -Force to rebuild)"
    exit 0
}

if (-not (Test-Path (Join-Path $GlslangSrc 'StandAlone\CMakeLists.txt'))) {
    throw "not a glslang source tree: $GlslangSrc (missing StandAlone\CMakeLists.txt)"
}

# SPIRV-Headers 是必需的（glslang 生成 SPIR-V 枚举要用）；SPIRV-Tools 只在 ENABLE_OPT 时需要
if (-not (Test-Path (Join-Path $SpirvHeaders 'include\spirv\unified1\spirv.core.grammar.json'))) {
    Write-Host "[deps] SPIRV-Headers missing -> running update_glslang_sources.py"
    $fetch = Join-Path $GlslangSrc 'update_glslang_sources.py'
    if (-not (Test-Path $fetch)) { throw "missing $fetch; fetch glslang deps manually" }
    Push-Location $GlslangSrc
    try { & python $fetch } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "update_glslang_sources.py failed" }
}
if (-not (Test-Path (Join-Path $SpirvHeaders 'include\spirv\unified1\spirv.core.grammar.json'))) {
    throw "SPIRV-Headers still incomplete under $SpirvHeaders"
}

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue)
if (-not $cmake) { throw "cmake not found on PATH" }

if ($Force -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

# 静态链接（BUILD_SHARED_LIBS=OFF）=> 只产出一个 exe，运行时不用带任何 DLL
# ENABLE_OPT=ON 用上已拉下来的 SPIRV-Tools：能对产物做校验与 -Os 优化
# GLSLANG_TESTS=OFF 不构建 googletest
Write-Host "[cfg ] glslang @ $GlslangSrc"
$cfgArgs = @('-S', $GlslangSrc, '-B', $BuildDir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
             '-DENABLE_OPT=ON', '-DGLSLANG_TESTS=OFF', '-DBUILD_SHARED_LIBS=OFF',
             '-DCMAKE_BUILD_TYPE=Release')
& cmake @cfgArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "[build] glslang-standalone (Release) ..."
& cmake --build $BuildDir --config Release --target glslang-standalone
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

# MSBuild 下的产物位置带配置子目录
$candidates = @(
    (Join-Path $BuildDir 'StandAlone\Release\glslangValidator.exe'),
    (Join-Path $BuildDir 'Release\glslangValidator.exe'),
    (Join-Path $BuildDir 'StandAlone\glslangValidator.exe')
)
$found = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $found) { throw "glslangValidator.exe not found under $BuildDir" }

Copy-Item -Force $found $Exe
Write-Host "[done] $Exe"
& $Exe --version
