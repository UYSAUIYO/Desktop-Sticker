# 校验 signing_key.txt：必须是 64 位十六进制且不能是全 0。
# 全 0 在引擎里表示"未启用加密"（core/script_encryption_key.gen.cpp 的占位内容），
# 一旦以全 0 构建，导出的包就是明文的，而且是静默的 —— 所以要在这里挡住。
# 用法：powershell -NoProfile -ExecutionPolicy Bypass -File tools/wallpaper-studio/verify_key.ps1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'

$keyFile = Join-Path $PSScriptRoot 'signing_key.txt'
if (-not (Test-Path $keyFile)) { throw "missing $keyFile" }

$key = (Get-Content -Raw $keyFile).Trim()
if ($key.Length -ne 64) { throw "signing_key.txt must be 64 hex chars, got $($key.Length)" }
if ($key -notmatch '^[0-9a-fA-F]{64}$') { throw 'signing_key.txt must be hexadecimal' }
if ($key -match '^0{64}$') { throw 'signing_key.txt must not be all zeros (the engine treats that as "no encryption")' }

Write-Host "[ok] signing_key.txt is a valid 256-bit key"
