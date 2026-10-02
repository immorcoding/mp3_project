[CmdletBinding()]
param(
    # 只运行这些场景（文件名不含 .args）；默认全部。
    [string[]]$Scenario,
    # 用本次结果覆盖 scenarios/*.expected（仅在确认视觉变化是有意的之后使用）。
    [switch]$Update,
    [string]$BuildDir,
    # 已解压的 SDL2 MinGW 开发包目录；给出时不再联网下载（CHANGED 推送快照复用主仓库缓存）。
    [string]$SdlSourceDir
)

# GUI 模拟器场景回归：以确定性虚拟时钟运行 scenarios/*.args，比较每张截图的帧哈希。
# 截图保存在 <BuildDir>/shots/<场景>/，哈希不一致时可直接打开对应 BMP 比对。

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$simRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $simRoot '..\..')).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build\gui_simulator'
}

$configureArguments = @('-S', $simRoot, '-B', $BuildDir, '-G', 'Ninja', '-DCMAKE_C_COMPILER=gcc')
if ($SdlSourceDir) {
    $configureArguments += "-DFETCHCONTENT_SOURCE_DIR_SDL2_MINGW=$SdlSourceDir"
}
else {
    # 不让早先缓存的覆盖值钉住旧 SDL；按 CMakeLists 的 URL 与 SHA256 正常下载/校验。
    $configureArguments += '-UFETCHCONTENT_SOURCE_DIR_SDL2_MINGW'
}
& cmake @configureArguments | Out-Null
if ($LASTEXITCODE -ne 0) { throw "模拟器 CMake 配置失败" }
& cmake --build $BuildDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw "模拟器构建失败" }

$exe = Join-Path $BuildDir 'gui_simulator.exe'
$scenarioFiles = Get-ChildItem -Path (Join-Path $simRoot 'scenarios') -Filter '*.args' | Sort-Object Name
if ($Scenario) {
    $scenarioFiles = $scenarioFiles | Where-Object { $Scenario -contains $_.BaseName }
}

$failed = 0
foreach ($file in $scenarioFiles) {
    $name = $file.BaseName
    $shotDir = Join-Path $BuildDir "shots\$name"
    New-Item -ItemType Directory -Force -Path $shotDir | Out-Null

    $arguments = @('--hidden', '--out-dir', $shotDir)
    foreach ($line in Get-Content -Path $file.FullName -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed -eq '' -or $trimmed.StartsWith('#')) { continue }
        $arguments += ($trimmed -split '\s+')
    }

    $output = & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "场景 $name 运行失败（退出码 $LASTEXITCODE）" }

    $actual = [ordered]@{}
    foreach ($row in $output) {
        if ($row -match '^\[shot\] (\S+) ([0-9a-f]{8})') { $actual[$Matches[1]] = $Matches[2] }
    }

    $expectedPath = Join-Path $simRoot "scenarios\$name.expected"
    if ($Update) {
        $actual.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" } |
            Set-Content -Path $expectedPath -Encoding ascii
        Write-Host "已更新 $name（$($actual.Count) 帧）"
        continue
    }

    if (-not (Test-Path $expectedPath)) { throw "缺少基线 $expectedPath；首次请加 -Update" }
    $expected = [ordered]@{}
    foreach ($row in Get-Content -Path $expectedPath) {
        $parts = $row -split '\s+'
        if ($parts.Count -eq 2) { $expected[$parts[0]] = $parts[1] }
    }

    $mismatches = @()
    if ($expected.Count -eq 0) { $mismatches += '（基线为空）' }
    foreach ($key in $expected.Keys) {
        if (-not $actual.Contains($key)) { $mismatches += "$key（缺失）" }
        elseif ($actual[$key] -ne $expected[$key]) { $mismatches += $key }
    }
    foreach ($key in $actual.Keys) {
        if (-not $expected.Contains($key)) { $mismatches += "$key（基线中没有）" }
    }

    if ($mismatches.Count -eq 0) {
        Write-Host "PASS $name（$($expected.Count) 帧）"
    }
    else {
        $failed++
        Write-Host "FAIL $name：$($mismatches -join ', ')；截图见 $shotDir"
    }
}

if ($failed -gt 0) {
    Write-Host "GUI 场景回归失败：$failed 个场景与基线不一致。"
    exit 1
}
Write-Host 'GUI 场景回归通过。'
