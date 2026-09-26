# 全量重编（Release x64）+ 同步 DLL + 跑单测。以后这个操作固定用它。
#
#   [1/4] 关闭正在运行的实例
#   [2/4] 全量编译（-t:Rebuild，输出实时滚动）
#   [3/4] 把新 DLL 同步到 EXE 目录与测试目录
#   [4/4] 跑单元测试
#
# 为什么不逐个工程编：单个工程构建时 DLL/.lib 落在该工程自己的 OutDir，而测试是从
# 解决方案 bin 目录链接并加载的 —— 于是"代码是新的、测试用的是旧的"，轻则断言错乱，
# 重则因结构体布局不一致直接段错误（这坑踩过不止一次）。
#
# 不跑 NuGet restore：包已还原，restore 只会空转（日志里是"所有项目均是最新的"）；
# 只有 packages.config 变了或全新 checkout 才需要，那时跑一次 build.bat 即可。
#
# 用法（一行）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/rebuild_and_test.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/rebuild_and_test.ps1 -Filter WallPaperStore_
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/rebuild_and_test.ps1 -List
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/rebuild_and_test.ps1 -Incremental
[CmdletBinding()]
param(
    [switch]$Incremental,   # 增量编译（不做 Rebuild）
    [string]$Filter = '',   # 只跑名字含该子串的用例
    [switch]$List,          # 只列用例名（按注册顺序）
    [switch]$NoTest,        # 只编译 + 同步 DLL，不跑测试
    [switch]$SkipClose      # 不关正在运行的实例
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$MSBuild  = 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
$Sln      = Join-Path $RepoRoot 'Desktop Sticker\Desktop Sticker.sln'
$OutDir   = Join-Path $RepoRoot 'Desktop Sticker\bin\x64\Release'
$ExeDir   = Join-Path $RepoRoot 'Desktop Sticker\x64\Release\Desktop Sticker'
$TestsDir = Join-Path $OutDir 'Tests'
$TestExe  = Join-Path $TestsDir 'DesktopSticker.Tests.exe'
$Log      = Join-Path $env:TEMP 'dstk_build.log'

function Say($msg, $color = 'Gray') { Write-Host $msg -ForegroundColor $color }
function Stamp { return (Get-Date).ToString('HH:mm:ss') }

if (-not (Test-Path $MSBuild)) { Say "[ERROR] 找不到 MSBuild: $MSBuild" 'Red'; exit 2 }
if (-not (Test-Path $Sln))     { Say "[ERROR] 找不到解决方案: $Sln" 'Red'; exit 2 }

# ---- [1/4] 关掉正在运行的实例，否则 EXE/DLL 被占用会链接失败 ----
if (-not $SkipClose) {
    Say "[$(Stamp)] [1/4] 关闭正在运行的实例 ..."
    $closer = Join-Path $PSScriptRoot 'close_app.ps1'
    if (Test-Path $closer) { & powershell -NoProfile -ExecutionPolicy Bypass -File $closer | Out-Null }
    Start-Sleep -Milliseconds 800
    if (Get-Process -Name Desktop_Sticker -ErrorAction SilentlyContinue) {
        # 渲染线程卡住时 WM_CLOSE/WM_QUIT 都进不去，只能强杀；
        # 强杀会跳过 Shutdown 的桌面图标还原，所以这里必须提醒。
        Say '           [WARN] 优雅关闭超时，改为强制结束（桌面图标可能停在分区里，下次正常退出会还原）' 'Yellow'
        Stop-Process -Name Desktop_Sticker -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 500
    }
} else {
    Say "[$(Stamp)] [1/4] 跳过关闭实例"
}

# ---- [2/4] 全量编译 ----
$mode = if ($Incremental) { '增量' } else { '全量' }
# 开关一律写成字面量，**不要用数组 splat**：PowerShell 会把 splat 进来的
# "-t:Rebuild" 拆成 "- t : R e b u i l d" 交给 MSBuild，报 "MSB1001 未知开关"。
$commonArgs = "-p:Configuration=Release -p:Platform=x64 -m:1 -v:m -nologo"
if ($Incremental) { $buildArgs = "$commonArgs" } else { $buildArgs = "-t:Rebuild $commonArgs" }

Say "[$(Stamp)] [2/4] $mode 编译 Release x64 ..."
Say "           完整日志：$Log"
# 输出走 Tee-Object：既实时打印（卡住时能看出卡在哪个工程），又留一份日志
if ($Incremental) {
    & $MSBuild $Sln -p:Configuration=Release -p:Platform=x64 -m:1 -v:m -nologo 2>&1 |
        Tee-Object -FilePath $Log
} else {
    & $MSBuild $Sln -t:Rebuild -p:Configuration=Release -p:Platform=x64 -m:1 -v:m -nologo 2>&1 |
        Tee-Object -FilePath $Log
}
$buildCode = $LASTEXITCODE
Say "[$(Stamp)] 编译结束，退出码 $buildCode"

if ($buildCode -ne 0) {
    Say "[FAILED] 编译失败，错误如下（完整日志：$Log）：" 'Red'
    # 不要强制 -Encoding：Tee-Object 写出来的是 UTF-16LE（开头 FF FE），指定 Default
    # 会把中文诊断读成乱码（实测过）。不指定时 Select-String 自己按 BOM 判编码。
    Select-String -Path $Log -Pattern 'error ' |
        Select-Object -First 25 | ForEach-Object { Say ("  " + $_.Line.Trim()) 'Red' }
    Say '         可手动复现（复制这一行到 cmd）：' 'Yellow'
    Say "         `"$MSBuild`" `"$Sln`" $buildArgs" 'Yellow'
    exit 1
}
Say "[$(Stamp)] [2/4] [OK] 编译通过" 'Green'

# ---- [3/4] 同步 DLL 到 EXE 目录与测试目录 ----
Say "[$(Stamp)] [3/4] 同步 DLL ..."
$dlls = @('DesktopSticker.Features.dll', 'DesktopSticker.WallPaper.dll',
          'DesktopSticker.ResMon.dll', 'WebView2Loader.dll')
$copied = 0
foreach ($dir in @($ExeDir, $TestsDir)) {
    if (-not (Test-Path $dir)) { continue }
    foreach ($name in $dlls) {
        $src = Join-Path $OutDir $name
        if (Test-Path $src) { Copy-Item -Force $src (Join-Path $dir $name); $copied++ }
    }
}
Say "           [OK] 复制了 $copied 个文件" 'Green'

# ---- [4/4] 跑单测 ----
if ($NoTest) {
    Say "[$(Stamp)] [4/4] 跳过单测（-NoTest）"
    exit 0
}
if (-not (Test-Path $TestExe)) { Say "[ERROR] 找不到测试 exe: $TestExe" 'Red'; exit 2 }
Say "[$(Stamp)] [4/4] 运行单元测试 ..."
# 测试输出无缓冲，所以即使某个用例崩溃也能看到跑到了哪一条。
# 同样不用 splat（理由见上面编译那段），分支写全。
if ($List) {
    & $TestExe --list
} elseif ($Filter -ne '') {
    & $TestExe --filter $Filter
} else {
    & $TestExe
}
$code = $LASTEXITCODE

if ($List) { Say '[DONE] 已列出用例' 'Green'; exit 0 }

Say ''
if ($code -eq 0) {
    Say "[$(Stamp)] [DONE] 全部通过" 'Green'
} else {
    Say "[$(Stamp)] [FAILED] 退出码 $code（139 = 段错误，看最后一个 [PASS] 的下一条）" 'Red'
}
exit $code
