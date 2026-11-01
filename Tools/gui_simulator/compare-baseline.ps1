[CmdletBinding()]
param(
    # 基线提交（任何 git 能解析的引用）；在临时 worktree 里构建并渲染它的场景截图。
    [Parameter(Mandatory)][string]$BaselineRef,
    # 只对比这些场景（文件名不含 .args）；默认两侧场景的并集。
    [string[]]$Scenario,
    # 变化帧清单与拼图输出目录；默认在会话临时目录下新建。
    [string]$OutDir,
    # 临时 worktree 与基线构建产物的父目录；默认在会话临时目录下新建，结束时清理。
    [string]$WorkRoot,
    # 当前工作树的模拟器构建目录；默认 build\gui_simulator。
    [string]$BuildDir,
    [string]$SdlSourceDir,
    # 测试接缝：param($SourceRoot, $BuildDir) 构建 $SourceRoot 下的模拟器并返回可执行文件路径。
    [scriptblock]$SimulatorBuilder
)

# 只读基线对比：不改基线文件、不用 stash、不动当前工作树。
# 在临时 worktree 里检出 -BaselineRef，构建并跑场景；当前工作树（含未提交改动）同样跑一遍；
# 逐帧按帧哈希比较，对变化帧输出“基线 | 当前 | 差异掩膜”拼图（PNG）。结束后移除临时 worktree。

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$simRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $simRoot '..\..')).Path
$jobTemp = if ($env:CLAUDE_JOB_DIR -and (Test-Path (Join-Path $env:CLAUDE_JOB_DIR 'tmp'))) { Join-Path $env:CLAUDE_JOB_DIR 'tmp' } else { [IO.Path]::GetTempPath() }
$stamp = [Guid]::NewGuid().ToString('N').Substring(0, 6)
if (-not $WorkRoot) { $WorkRoot = Join-Path $jobTemp "gb-$stamp" }
if (-not $OutDir) { $OutDir = Join-Path $jobTemp "gui-compare-$stamp" }
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build\gui_simulator' }

function Invoke-GitQuiet {
    param([string[]]$Arguments)
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; $out = & git -C $repoRoot @Arguments 2>$null; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    return [pscustomobject]@{ Code = $code; Output = @($out | ForEach-Object { [string]$_ }) }
}

if (-not $SimulatorBuilder) {
    if (-not $SdlSourceDir) {
        $cached = Join-Path $BuildDir '_deps\sdl2_mingw-src'
        if (Test-Path $cached) { $SdlSourceDir = $cached }
    }
    $sdl = $SdlSourceDir
    $SimulatorBuilder = {
        param([string]$SourceRoot, [string]$Dir)
        $configure = @('-S', (Join-Path $SourceRoot 'Tools\gui_simulator'), '-B', $Dir, '-G', 'Ninja', '-DCMAKE_C_COMPILER=gcc')
        if ($sdl) { $configure += "-DFETCHCONTENT_SOURCE_DIR_SDL2_MINGW=$sdl" }
        & cmake @configure | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "模拟器 CMake 配置失败：$SourceRoot" }
        & cmake --build $Dir | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "模拟器构建失败：$SourceRoot" }
        return (Join-Path $Dir 'gui_simulator.exe')
    }.GetNewClosure()
}

if (-not ('GuiCompareMontage' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System.Drawing;
using System.Drawing.Imaging;
public static class GuiCompareMontage {
    // 左：基线；中：当前；右：差异掩膜（差异像素红色，其余为压暗的当前帧）。返回差异像素数。
    public static int Save(string basePath, string curPath, string outPath) {
        using (Bitmap a = new Bitmap(basePath))
        using (Bitmap b = new Bitmap(curPath)) {
            int w = System.Math.Max(a.Width, b.Width), h = System.Math.Max(a.Height, b.Height), gap = 8, diff = 0;
            using (Bitmap m = new Bitmap(w * 3 + gap * 2, h)) {
                using (Graphics g = Graphics.FromImage(m)) {
                    g.Clear(Color.White);
                    g.DrawImageUnscaled(a, 0, 0);
                    g.DrawImageUnscaled(b, w + gap, 0);
                }
                for (int y = 0; y < h; y++) {
                    for (int x = 0; x < w; x++) {
                        bool inA = x < a.Width && y < a.Height, inB = x < b.Width && y < b.Height;
                        Color pa = inA ? a.GetPixel(x, y) : Color.Empty, pb = inB ? b.GetPixel(x, y) : Color.Empty;
                        bool differs = !inA || !inB || pa.ToArgb() != pb.ToArgb();
                        Color o;
                        if (differs) { diff++; o = Color.Red; }
                        else { o = Color.FromArgb(pb.R / 4, pb.G / 4, pb.B / 4); }
                        m.SetPixel(w * 2 + gap * 2 + x, y, o);
                    }
                }
                m.Save(outPath, ImageFormat.Png);
            }
            return diff;
        }
    }
}
'@
}

function Get-ScenarioArguments {
    param([string]$ArgsFile, [string]$ShotDir)
    $arguments = @('--hidden', '--out-dir', $ShotDir)
    foreach ($line in Get-Content -Path $ArgsFile -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed -eq '' -or $trimmed.StartsWith('#')) { continue }
        $arguments += ($trimmed -split '\s+')
    }
    return $arguments
}

# 返回 场景名 -> (键 -> 哈希/动画数)；截图写入 $ShotRoot\<场景>\。
function Invoke-ScenarioSet {
    param([string]$Exe, [string]$SourceRoot, [string]$ShotRoot, [string[]]$Names)
    $result = @{}
    $scenarioDir = Join-Path $SourceRoot 'Tools\gui_simulator\scenarios'
    foreach ($name in $Names) {
        $file = Join-Path $scenarioDir "$name.args"
        if (-not (Test-Path $file)) { continue }
        $shotDir = Join-Path $ShotRoot $name
        New-Item -ItemType Directory -Force -Path $shotDir | Out-Null
        $simArguments = @(Get-ScenarioArguments -ArgsFile $file -ShotDir $shotDir)
        $output = & $Exe @simArguments
        if ($LASTEXITCODE -ne 0) { throw "场景 $name 运行失败（退出码 $LASTEXITCODE）：$SourceRoot" }
        $keys = [ordered]@{}
        foreach ($row in $output) {
            if ($row -match '^\[shot\] (\S+) ([0-9a-f]{8})') { $keys[$Matches[1]] = $Matches[2] }
            elseif ($row -match '^\[anims\] (\S+) (\d+)') { $keys["anims.$($Matches[1])"] = $Matches[2] }
        }
        $result[$name] = $keys
    }
    return $result
}

$worktree = Join-Path $WorkRoot 'base'
$worktreeAdded = $false
try {
    $resolved = Invoke-GitQuiet @('rev-parse', '--verify', "$BaselineRef^{commit}")
    if ($resolved.Code -ne 0) { throw "无法解析基线提交：$BaselineRef" }
    $sha = $resolved.Output[0]

    New-Item -ItemType Directory -Force -Path $WorkRoot, $OutDir | Out-Null
    $added = Invoke-GitQuiet @('worktree', 'add', '--detach', '--force', $worktree, $sha)
    if ($added.Code -ne 0) { throw "创建临时 worktree 失败：$($added.Output -join ' ')" }
    $worktreeAdded = $true
    if (-not (Test-Path (Join-Path $worktree 'Tools\gui_simulator\scenarios'))) {
        throw "基线 $BaselineRef 没有 Tools/gui_simulator/scenarios，无法对比"
    }

    $baseExe = & $SimulatorBuilder $worktree (Join-Path $WorkRoot 'bb')
    $curExe = & $SimulatorBuilder $repoRoot $BuildDir

    $names = @()
    foreach ($root in @($worktree, $repoRoot)) {
        $names += Get-ChildItem -Path (Join-Path $root 'Tools\gui_simulator\scenarios') -Filter '*.args' | ForEach-Object { $_.BaseName }
    }
    $names = @($names | Sort-Object -Unique)
    if ($Scenario) { $names = @($names | Where-Object { $Scenario -contains $_ }) }

    $shotsBase = Join-Path $OutDir 'shots\base'
    $shotsCur = Join-Path $OutDir 'shots\cur'
    $baseResult = Invoke-ScenarioSet -Exe $baseExe -SourceRoot $worktree -ShotRoot $shotsBase -Names $names
    $curResult = Invoke-ScenarioSet -Exe $curExe -SourceRoot $repoRoot -ShotRoot $shotsCur -Names $names

    $lines = New-Object System.Collections.Generic.List[string]
    $changedFrames = 0
    $changedScenarios = 0
    $total = 0
    foreach ($name in $names) {
        $b = if ($baseResult.ContainsKey($name)) { $baseResult[$name] } else { $null }
        $c = if ($curResult.ContainsKey($name)) { $curResult[$name] } else { $null }
        $keys = @()
        if ($b) { $keys += $b.Keys }
        if ($c) { $keys += $c.Keys }
        $keys = @($keys | Select-Object -Unique)
        $scenarioChanged = $false
        foreach ($key in $keys) {
            $isFrame = -not $key.StartsWith('anims.')
            if ($isFrame) { $total++ }
            $bv = if ($b -and $b.Contains($key)) { $b[$key] } else { $null }
            $cv = if ($c -and $c.Contains($key)) { $c[$key] } else { $null }
            if ($bv -eq $cv) { continue }
            $scenarioChanged = $true
            $reason = if ($null -eq $bv) { '当前新增' } elseif ($null -eq $cv) { '当前缺失' } else { "$bv -> $cv" }
            if (-not $isFrame) {
                $lines.Add("  $name/$key（动画数断言）：$reason")
                continue
            }
            $changedFrames++
            $detail = $reason
            $baseBmp = Join-Path $shotsBase "$name\$key.bmp"
            $curBmp = Join-Path $shotsCur "$name\$key.bmp"
            if ((Test-Path $baseBmp) -and (Test-Path $curBmp)) {
                $montageDir = Join-Path $OutDir $name
                New-Item -ItemType Directory -Force -Path $montageDir | Out-Null
                $pixels = [GuiCompareMontage]::Save($baseBmp, $curBmp, (Join-Path $montageDir "$key.png"))
                $detail = "$reason；$pixels 像素不同；拼图 $name/$key.png"
            }
            $lines.Add("  $name/$key：$detail")
        }
        if ($scenarioChanged) { $changedScenarios++ }
    }

    $summary = "基线 $($sha.Substring(0, 7)) 对比当前工作树：共 $changedFrames 帧变化（$changedScenarios 个场景受影响，共比较 $total 帧）。"
    $report = @($summary) + $lines
    $report | Set-Content -Path (Join-Path $OutDir 'report.txt') -Encoding UTF8
    $report | ForEach-Object { Write-Host $_ }
    Write-Host "输出目录：$OutDir"
}
finally {
    if ($worktreeAdded) {
        [void](Invoke-GitQuiet @('worktree', 'remove', '--force', $worktree))
    }
    if (Test-Path $worktree) { Remove-Item -LiteralPath $worktree -Recurse -Force -ErrorAction SilentlyContinue }
    [void](Invoke-GitQuiet @('worktree', 'prune'))
    Remove-Item -LiteralPath (Join-Path $WorkRoot 'bb') -Recurse -Force -ErrorAction SilentlyContinue
    if ((Test-Path $WorkRoot) -and -not (Get-ChildItem -LiteralPath $WorkRoot -Force)) {
        Remove-Item -LiteralPath $WorkRoot -Force -ErrorAction SilentlyContinue
    }
}
