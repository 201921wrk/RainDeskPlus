# scripts/package.ps1
# 打包 RainDeskPlus 发布包（x64）：从构建产物目录收集 exe / 皮肤 / 许可与文档，压成 zip。
# 用法：
#   .\scripts\package.ps1                                   # 默认取 ON 线 RelWithDebInfo 产物
#   .\scripts\package.ps1 -PresetDir vs2026 -Configuration Debug
# 产物：dist\RainDeskPlus-<版本>-win64.zip（dist/ 已入 .gitignore）
param(
    [string]$PresetDir     = 'vs2026-dock-ui',
    [string]$Configuration = 'RelWithDebInfo',
    [string]$OutputDir     = 'dist'
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

# 版本号以 CMakeLists.txt 的 project(... VERSION x.y.z) 为唯一来源，避免两处漂移
$cmakeLists = Join-Path $repoRoot 'CMakeLists.txt'
if (-not (Test-Path $cmakeLists)) { Write-Error "未找到 $cmakeLists"; exit 1 }
# project() 的 VERSION 与工程名跨行书写，故在整份文本上匹配（Select-String 逐行匹配会漏）
$cmakeText    = [System.IO.File]::ReadAllText($cmakeLists, [System.Text.Encoding]::UTF8)
$versionMatch = [regex]::Match($cmakeText, 'project\(RainDeskPlus\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $versionMatch.Success) { Write-Error '无法从 CMakeLists.txt 解析版本号'; exit 1 }
$version = $versionMatch.Groups[1].Value

# 发布包内的顶层目录名即解包后的目录名
$pkgName  = "RainDeskPlus-$version-win64"
$buildDir = Join-Path $repoRoot "build\$PresetDir\$Configuration"
$exePath  = Join-Path $buildDir 'RainDeskPlus.exe'

if (-not (Test-Path $exePath)) {
    Write-Error "未找到主程序：$exePath`n请先构建对应预设（例：cmake --build --preset vs2026-dock-ui-release）"
    exit 1
}

$outRoot = Join-Path $repoRoot $OutputDir
$stage   = Join-Path $outRoot $pkgName
$zipPath = Join-Path $outRoot "$pkgName.zip"

# 重建暂存目录：全部落在项目内，不写 C 盘
if (Test-Path $stage)   { Remove-Item $stage -Recurse -Force }
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Write-Host "[package] 版本=$version 构建线=$PresetDir/$Configuration" -ForegroundColor Cyan

# 1) 主程序（只取 exe，不带 pdb/lib 与构建目录里的 Smoke 临时夹具）
Copy-Item $exePath -Destination $stage -Force

# 2) 皮肤目录（含 Themes 与离线快照 json）
Copy-Item (Join-Path $repoRoot 'Skins') -Destination $stage -Recurse -Force

# 3) 许可与面向使用者的文档
foreach ($doc in @('LICENSE', 'README.md', 'CHANGELOG.md')) {
    $src = Join-Path $repoRoot $doc
    if (Test-Path $src) { Copy-Item $src -Destination $stage -Force }
    else { Write-Host "[package] 警告：缺少 $doc，已跳过" -ForegroundColor Yellow }
}
$manual = Join-Path $repoRoot 'docs\USER_MANUAL.md'
if (Test-Path $manual) {
    New-Item -ItemType Directory -Path (Join-Path $stage 'docs') -Force | Out-Null
    Copy-Item $manual -Destination (Join-Path $stage 'docs') -Force
}

# 4) 压缩（Compress-Archive 需要 .zip 扩展名）
Compress-Archive -Path $stage -DestinationPath $zipPath -CompressionLevel Optimal

# 5) 清理暂存目录，只留 zip
Remove-Item $stage -Recurse -Force

$zipInfo = Get-Item $zipPath
Write-Host ("[package] 完成：{0}（{1:N0} 字节）" -f $zipInfo.FullName, $zipInfo.Length) -ForegroundColor Green
