[CmdletBinding()]
param(
    # 只运行这些场景（文件名不含 .args）；默认全部。
    [string[]]$Scenario,
    # 用本次结果覆盖 scenarios/*.expected（仅在确认视觉变化是有意的之后使用）。
    [switch]$Update,
    [string]$BuildDir,
    # 已解压的 SDL2 MinGW 开发包目录；给出时不再联网下载（CHANGED 推送快照复用主仓库缓存）。
    [string]$SdlSourceDir,
    # 只读对比模式：在临时 worktree 渲染该提交的场景，与当前工作树逐帧对比（见 compare-baseline.ps1）。
    [string]$BaselineRef,
    # 对比模式的输出目录（变化帧清单与拼图）；默认在会话临时目录下新建。
    [string]$CompareOutDir
)

# GUI 模拟器场景回归：以确定性虚拟时钟运行 scenarios/*.args，比较每张截图的帧哈希。
# 截图保存在 <BuildDir>/shots/<场景>/，哈希不一致时可直接打开对应 BMP 比对。

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'scenario_common.ps1')

# 对比模式只读；-Update 会改写 scenarios/*.expected，两者同时给出时不能默默忽略其一。
if ($BaselineRef -and $Update) {
    throw '-BaselineRef（只读对比）与 -Update（改写 expected 基线）不能同时使用；请分开运行。'
}

if ($BaselineRef) {
    $compareArguments = @{ BaselineRef = $BaselineRef }
    if ($Scenario) { $compareArguments.Scenario = $Scenario }
    if ($CompareOutDir) { $compareArguments.OutDir = $CompareOutDir }
    if ($BuildDir) { $compareArguments.BuildDir = $BuildDir }
    if ($SdlSourceDir) { $compareArguments.SdlSourceDir = $SdlSourceDir }
    & (Join-Path $PSScriptRoot 'compare-baseline.ps1') @compareArguments
    return
}

$simRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $simRoot '..\..')).Path
if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build\gui_simulator'
}

# 未给 -SdlSourceDir 时不让早先缓存的覆盖值钉住旧 SDL；细节见 Build-GuiSimulator。
$exe = Build-GuiSimulator -SimulatorDir $simRoot -BuildDir $BuildDir -SdlSourceDir $SdlSourceDir -Label '当前工作树'
$scenarioFiles = Get-ChildItem -Path (Join-Path $simRoot 'scenarios') -Filter '*.args' | Sort-Object Name
if ($Scenario) {
    $scenarioFiles = $scenarioFiles | Where-Object { $Scenario -contains $_.BaseName }
}

$failed = 0
foreach ($file in $scenarioFiles) {
    $name = $file.BaseName
    $shotDir = Join-Path $BuildDir "shots\$name"
    New-Item -ItemType Directory -Force -Path $shotDir | Out-Null

    $arguments = @(Get-ScenarioArguments -ArgsFile $file.FullName -ShotDir $shotDir)

    $output = & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "场景 $name 运行失败（退出码 $LASTEXITCODE）" }

    $actual = ConvertFrom-SimulatorOutput -Output $output

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
