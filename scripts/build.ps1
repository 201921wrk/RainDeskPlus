# scripts/build.ps1
# 编译 RainDeskPlus（先配置后构建）。预设见 CMakePresets.json。
# 用法：
#   .\scripts\build.ps1                                  # 默认 vs2026-release
#   .\scripts\build.ps1 -Preset ninja-debug
#   .\scripts\build.ps1 -Preset ninja-release -ConfigureOnly
param(
    [ValidateSet('ninja-debug','ninja-release','vs2026-debug','vs2026-release','vs2022-debug','vs2022-release')]
    [string]$Preset = 'vs2026-release',
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'

$configurePreset = switch ($Preset) {
    'ninja-debug'    { 'ninja-debug' }
    'ninja-release'  { 'ninja-release' }
    'vs2026-debug'   { 'vs2026-x64' }
    'vs2026-release' { 'vs2026-x64' }
    'vs2022-debug'   { 'vs2022-x64' }
    'vs2022-release' { 'vs2022-x64' }
}

Write-Host "[build] configure preset=$configurePreset" -ForegroundColor Cyan
& cmake --preset $configurePreset
if ($LASTEXITCODE -ne 0) { Write-Error "配置失败"; exit $LASTEXITCODE }

if ($ConfigureOnly) { Write-Host "[build] 仅配置，跳过编译。"; exit 0 }

Write-Host "[build] build preset=$Preset" -ForegroundColor Cyan
& cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { Write-Error "编译失败 (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

Write-Host "[build] 完成。" -ForegroundColor Green
