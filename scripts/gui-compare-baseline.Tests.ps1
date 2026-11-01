Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Tools/gui_simulator/compare-baseline.ps1（run-scenarios.ps1 -BaselineRef）：真实 Git 夹具 + 假模拟器构建器。
# 假模拟器是一个 .ps1：按夹具里 fake_color.txt 的颜色写 f1.bmp 并打印 [shot]，不依赖 cmake/SDL。
. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')
$saved = Suspend-InheritedGitEnvironment
$compareScript = Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'Tools/gui_simulator/compare-baseline.ps1'
$temp = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ('gcb-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))

function Invoke-Git {
    param([string]$Root, [string[]]$Arguments)
    $previous = $ErrorActionPreference
    try { $ErrorActionPreference = 'Continue'; $out = & git -C $Root @Arguments 2>$null; $code = $LASTEXITCODE }
    finally { $ErrorActionPreference = $previous }
    if ($code -ne 0) { throw "夹具 git 失败（$code）：$($Arguments -join ' ')" }
    return @($out | ForEach-Object { [string]$_ })
}

function Assert-That {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

# 假构建器：在 BuildDir 写 sim.ps1，运行时读构建源码根的 fake_color.txt。
$builder = {
    param([string]$SourceRoot, [string]$BuildDir)
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    $colorFile = Join-Path $SourceRoot 'Tools/gui_simulator/fake_color.txt'
    $sim = Join-Path $BuildDir 'sim.ps1'
    $body = @'
Add-Type -AssemblyName System.Drawing
$all = @($args | ForEach-Object { [string]$_ })
$outDir = $all[$all.IndexOf('--out-dir') + 1]
$color = (Get-Content -LiteralPath '__COLOR__' -Raw).Trim()
$bmp = New-Object System.Drawing.Bitmap 24, 32
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.Clear([System.Drawing.Color]::FromName($color))
$g.Dispose()
$bmp.Save((Join-Path $outDir 'f1.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
Write-Output ('[shot] f1 ' + ($color.GetHashCode() -band 0x7fffffff).ToString('x8'))
'@
    Set-Content -LiteralPath $sim -Value $body.Replace('__COLOR__', $colorFile) -Encoding ascii
    return $sim
}

function New-Fixture {
    $root = Join-Path $temp 'repo'
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'Tools/gui_simulator/scenarios') | Out-Null
    foreach ($script in 'compare-baseline.ps1', 'run-scenarios.ps1', 'scenario_common.ps1') {
        Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $compareScript) $script) -Destination (Join-Path $root 'Tools/gui_simulator')
    }
    Set-Content -LiteralPath (Join-Path $root 'Tools/gui_simulator/scenarios/s1.args') -Value '--shot 100:f1' -Encoding ascii
    Set-Content -LiteralPath (Join-Path $root 'Tools/gui_simulator/fake_color.txt') -Value 'Red' -Encoding ascii
    Set-Content -LiteralPath (Join-Path $root '.gitignore') -Value "build/`n" -Encoding ascii
    Invoke-Git $root @('init', '--quiet') | Out-Null
    Invoke-Git $root @('add', '.') | Out-Null
    Invoke-Git $root @('-c', 'user.name=T', '-c', 'user.email=t@example.invalid', 'commit', '-qm', 'seed') | Out-Null
    return $root
}

function Invoke-Compare {
    param([string]$Root, [string]$Ref, [string]$Work, [string]$Out)
    $script = Join-Path $Root 'Tools/gui_simulator/compare-baseline.ps1'
    $result = & $script -BaselineRef $Ref -WorkRoot $Work -OutDir $Out -BuildDir (Join-Path $Work 'cur') `
        -SimulatorBuilder $builder 6>&1 | Out-String
    return $result
}

try {
    $root = New-Fixture
    $head = @(Invoke-Git $root @('rev-parse', 'HEAD'))[0]
    $work = Join-Path $temp 'w'
    $out = Join-Path $temp 'out'

    # 1. 同一提交对比：0 帧变化，临时 worktree 移除，仓库状态不变，无 stash。
    $worktreesBefore = @(Invoke-Git $root @('worktree', 'list')).Count
    $statusBefore = (Invoke-Git $root @('status', '--porcelain')) -join "`n"
    $text = Invoke-Compare -Root $root -Ref $head -Work $work -Out $out
    Assert-That ($text -match '0 帧变化') "同一提交应报 0 帧变化：$text"
    Assert-That (@(Invoke-Git $root @('worktree', 'list')).Count -eq $worktreesBefore) '临时 worktree 未移除'
    Assert-That (-not (Test-Path (Join-Path $work 'base'))) '临时 worktree 目录仍在'
    Assert-That (((Invoke-Git $root @('status', '--porcelain')) -join "`n") -eq $statusBefore) '对比改变了仓库状态'
    Assert-That (@(Invoke-Git $root @('stash', 'list')).Count -eq 0) '对比使用了 stash'

    # 2. 工作树未提交改动（Red -> Blue）：报告变化帧，生成拼图，未提交改动原样保留，仍无 stash。
    Set-Content -LiteralPath (Join-Path $root 'Tools/gui_simulator/fake_color.txt') -Value 'Blue' -Encoding ascii
    $statusBefore = (Invoke-Git $root @('status', '--porcelain')) -join "`n"
    $text = Invoke-Compare -Root $root -Ref $head -Work $work -Out $out
    Assert-That ($text -match '1 帧变化') "应报 1 帧变化：$text"
    Assert-That ($text -match 's1/f1') "变化清单缺 s1/f1：$text"
    Assert-That (Test-Path (Join-Path $out 's1/f1.png')) '缺少拼图 s1/f1.png'
    Assert-That ((Get-Content -LiteralPath (Join-Path $root 'Tools/gui_simulator/fake_color.txt') -Raw).Trim() -eq 'Blue') '未提交改动被改走'
    Assert-That (((Invoke-Git $root @('status', '--porcelain')) -join "`n") -eq $statusBefore) '对比改变了仓库状态'
    Assert-That (@(Invoke-Git $root @('stash', 'list')).Count -eq 0) '对比使用了 stash'
    Assert-That (@(Invoke-Git $root @('worktree', 'list')).Count -eq $worktreesBefore) '临时 worktree 未移除'

    # 3. 构建失败路径：临时 worktree 仍被移除。
    $failing = { param($SourceRoot, $BuildDir) throw '假构建失败' }
    $failed = $false
    try {
        & (Join-Path $root 'Tools/gui_simulator/compare-baseline.ps1') -BaselineRef $head -WorkRoot $work -OutDir $out `
            -BuildDir (Join-Path $work 'cur') -SimulatorBuilder $failing 6>&1 | Out-Null
    }
    catch { $failed = $true }
    Assert-That $failed '构建失败应抛错'
    Assert-That (@(Invoke-Git $root @('worktree', 'list')).Count -eq $worktreesBefore) '失败后临时 worktree 未移除'

    # 4. 不给 -BuildDir 时，当前工作树也构建到会话临时目录，不写仓库共享的 build\gui_simulator（不与 FULL 共用）。
    $recordFile = Join-Path $temp 'build-dirs.txt'
    $recording = {
        param([string]$SourceRoot, [string]$BuildDir)
        Add-Content -LiteralPath $recordFile -Value $BuildDir -Encoding ascii
        return (& $builder $SourceRoot $BuildDir)
    }.GetNewClosure()
    $null = & (Join-Path $root 'Tools/gui_simulator/compare-baseline.ps1') -BaselineRef $head -WorkRoot $work -OutDir $out `
        -SimulatorBuilder $recording 6>&1 | Out-String
    $sharedBuild = Join-Path $root 'build\gui_simulator'
    $recorded = @(Get-Content -LiteralPath $recordFile)
    Assert-That ($recorded.Count -eq 2) "应构建基线与当前各一次：$($recorded -join ';')"
    foreach ($dir in $recorded) {
        Assert-That (-not $dir.StartsWith($sharedBuild, [StringComparison]::OrdinalIgnoreCase)) "不得构建到共享目录：$dir"
    }
    Assert-That (-not (Test-Path -LiteralPath $sharedBuild)) '对比不得创建共享构建目录 build\gui_simulator'
    Assert-That (-not (Test-Path -LiteralPath (Join-Path $work 'cur'))) '默认的当前构建目录用后应清理'

    # 5. run-scenarios：-BaselineRef 与 -Update 互斥（对比模式只读，不得改写 expected）。
    $conflict = $null
    try {
        & (Join-Path $root 'Tools/gui_simulator/run-scenarios.ps1') -BaselineRef $head -Update 6>&1 | Out-Null
    }
    catch { $conflict = $_.Exception.Message }
    Assert-That (($null -ne $conflict) -and ($conflict -match 'BaselineRef.*Update|Update.*BaselineRef')) "-BaselineRef 与 -Update 同时给出必须报错：$conflict"

    Write-Output 'GUI 基线对比模式测试通过。'
}
finally {
    Restore-InheritedGitEnvironment -Saved $saved
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
$global:LASTEXITCODE = 0
