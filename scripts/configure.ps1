# scripts/configure.ps1
# 配置 RainDeskPlus 的 CMake 构建。预设见 CMakePresets.json。
# 用法：
#   .\scripts\configure.ps1                      # 默认 vs2026-x64
#   .\scripts\configure.ps1 -Preset ninja-debug
#   .\scripts\configure.ps1 -Preset vs2022-x64-dock-ui
param(
    [ValidateSet('ninja-debug','ninja-release','vs2026-x64','vs2026-x64-dock-ui','vs2022-x64','vs2022-x64-dock-ui')]
    [string]$Preset = 'vs2026-x64'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot  # 仓库根 = scripts 的上级
if (-not $root) { $root = (Get-Location).Path }

Write-Host "[configure] preset=$Preset" -ForegroundColor Cyan
& cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake 配置失败 (exit $LASTEXITCODE)。确认已装 VS2022+CMake+Windows SDK。见 docs/BUILD.md。"
    exit $LASTEXITCODE
}
Write-Host "[configure] 完成。" -ForegroundColor Green
