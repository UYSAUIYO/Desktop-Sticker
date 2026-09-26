# 差量编译（Release x64）+ 同步 DLL + 跑单测。日常迭代用它，别用全量那份。
#
# 和 rebuild_and_test.ps1 的唯一区别是不传 `-t:Rebuild`：MSBuild 按时间戳只重编真正
# 有改动的项目，其余只做 up-to-date 检查（六个工程一共几秒）。改动涉及头文件、vcxproj、
# 或怀疑 obj 脏了（LNK 怪错、测试段错误但代码看着没错）时，回到 rebuild_and_test.ps1。
#
# 为什么不另写一套编译/同步/测试逻辑：DLL 同步那一段是踩过坑的 —— 单个工程构建时
# 产物落在工程自己的 OutDir，而测试从解决方案 bin 目录加载，只编一个工程就会出现
# "代码是新的、测试用的是旧的"。所以这里只做参数转发，真活在 rebuild_and_test.ps1 里。
#
# 用法（一行）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1 -Filter WallPaper
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev_build.ps1 -NoTest
[CmdletBinding()]
param(
    [string]$Filter = '',   # 只跑名字含该子串的用例
    [switch]$List,          # 只列用例名
    [switch]$NoTest,        # 只编译 + 同步 DLL，不跑测试
    [switch]$SkipClose      # 不关正在运行的实例
)

$ErrorActionPreference = 'Stop'

$full = Join-Path $PSScriptRoot 'rebuild_and_test.ps1'
if (-not (Test-Path $full)) {
    Write-Host "[ERROR] 找不到 $full" -ForegroundColor Red
    exit 2
}

# 只转参数，不复制逻辑；hashtable splat 在这里是安全的（PowerShell 层传参，
# 不会像传给 MSBuild 那样把开关拆成单字符）
$forward = @{ Incremental = $true; SkipClose = $SkipClose }
if ($List)      { $forward['List'] = $true }
if ($NoTest)    { $forward['NoTest'] = $true }
if ($Filter -ne '') { $forward['Filter'] = $Filter }

& $full @forward
exit $LASTEXITCODE
