# scripts/fetch_rainmeter.ps1
# 克隆 Rainmeter 上游源码到 rainmeter-upstream/（已被 .gitignore 忽略）。
# 用法：
#   .\scripts\fetch_rainmeter.ps1
#   .\scripts\fetch_rainmeter.ps1 -SkipTlsVerify   # 仅排错用，不推荐长期
param(
    [switch]$SkipTlsVerify
)

$ErrorActionPreference = 'Stop'
$root   = Split-Path -Parent $PSScriptRoot
$target = Join-Path $root 'rainmeter-upstream'

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Error "未检测到 git。先安装 git for Windows 并配置 user.name/user.email。见 docs/BUILD.md。"
    exit 1
}

if (Test-Path $target) {
    Write-Host "[fetch] $target 已存在，执行 pull..." -ForegroundColor Yellow
    Push-Location $target
    & git pull
    Pop-Location
    exit $LASTEXITCODE
}

Write-Host "[fetch] 克隆 Rainmeter 到 $target ..." -ForegroundColor Cyan
if ($SkipTlsVerify) {
    Write-Warning "已临时关闭 TLS 校验（仅排错）。完成后请恢复。"
    & git -c http.sslVerify=false clone https://github.com/rainmeter/rainmeter.git $target
} else {
    & git clone https://github.com/rainmeter/rainmeter.git $target
}
if ($LASTEXITCODE -ne 0) {
    Write-Warning @"
克隆失败（exit $LASTEXITCODE）。
本机 hosts 把 github.com 映射到 127.0.0.1（GitHub 加速器 MITM），TLS 可能失败：
  1) 首选：把加速器根 CA 导入受信任根证书库；
  2) 临时：注释 hosts 相关行直连；
  3) 仅排错：重试带 -SkipTlsVerify。
详见 docs/BUILD.md §5。
"@
    exit $LASTEXITCODE
}

Write-Host "[fetch] 完成。下一步按 docs/MODULE_EXTRACTION.md 提取到 Library/。" -ForegroundColor Green
