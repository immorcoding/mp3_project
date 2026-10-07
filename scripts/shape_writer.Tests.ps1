Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# shape 写入分支检查：纯函数与真实 Git 夹具（索引、推送、SessionStart 提示）。
. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')
. (Join-Path -Path $PSScriptRoot -ChildPath 'shape_writer.ps1')

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment

function Assert-Equal {
    param($Expected, $Actual, [Parameter(Mandatory)][string]$Message)
    if ($Expected -ne $Actual) {
        throw "$Message：期望 '$Expected'，实际 '$Actual'"
    }
}

function Assert-Throws {
    param([Parameter(Mandatory)][scriptblock]$Action, [Parameter(Mandatory)][string]$Pattern, [Parameter(Mandatory)][string]$Message)
    try {
        & $Action
    }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "$Message：报错应匹配 '$Pattern'，实际：$($_.Exception.Message)"
        }
        return
    }
    throw "$Message：应抛出异常"
}

function Invoke-FixtureGit {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string[]]$Arguments)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & git -C $Root -c core.hooksPath=$script:EmptyHooks -c commit.gpgsign=false `
            -c user.email=shape-writer-test@example.invalid -c user.name=shape-writer-test @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($exitCode -ne 0) {
        throw "夹具 git 失败（$exitCode）：$($Arguments -join ' ')；$($output -join ' ')"
    }
    return @($output | ForEach-Object { [string]$_ })
}

function Set-FixtureText {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Text)
    $full = Join-Path -Path $Root -ChildPath $Path
    New-Item -ItemType Directory -Path (Split-Path -Parent $full) -Force | Out-Null
    Add-Content -LiteralPath $full -Value $Text -Encoding ascii
}

# 一个带 main、origin 裸仓库、一个领域文件与一个源码文件的夹具。
function New-ShapeFixture {
    param([Parameter(Mandatory)][string]$Parent, [Parameter(Mandatory)][string]$Name)
    $root = Join-Path -Path $Parent -ChildPath $Name
    $bare = "$root.git"
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    Invoke-FixtureGit -Root $Parent -Arguments @('init', '-q', '--bare', $bare) | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('init', '-q', '-b', 'main') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('config', 'core.autocrlf', 'false') | Out-Null
    Set-FixtureText -Root $root -Path 'docs/shape/architecture.md' -Text '# Arch'
    Set-FixtureText -Root $root -Path 'src/a.c' -Text 'int a;'
    Invoke-FixtureGit -Root $root -Arguments @('add', '-A') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('commit', '-qm', 'base') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('remote', 'add', 'origin', $bare) | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('push', '-q', 'origin', 'main') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('remote', 'set-head', 'origin', 'main') | Out-Null
    return $root
}

function New-PushUpdate {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$LocalRef, [Parameter(Mandatory)][string]$RemoteRef)
    $sha = if ($LocalRef -match '^0+$') { $LocalRef } else { @(Invoke-FixtureGit -Root $Root -Arguments @('rev-parse', $LocalRef))[0].Trim() }
    return [pscustomobject]@{ LocalRef = $LocalRef; LocalSha = $sha; RemoteRef = $RemoteRef; RemoteSha = ('0' * 40) }
}

try {
    # 纯函数
    Assert-Equal $true (Test-ShapeAreaPath -RelativePath 'docs/shape/git.md') '领域文件'
    Assert-Equal $true (Test-ShapeAreaPath -RelativePath 'docs\shape\gui.md') '反斜杠领域文件'
    Assert-Equal $false (Test-ShapeAreaPath -RelativePath 'docs/shape/inbox/feature-x.md') 'inbox 不是领域文件'
    Assert-Equal $false (Test-ShapeAreaPath -RelativePath 'docs/agents/domain.md') '其他文档'
    Assert-Equal 'docs/shape/inbox/feature-pause.md' (Get-ShapeInboxPath -Branch 'feature/pause') 'inbox 路径'

    $work = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('shape-writer-' + [Guid]::NewGuid().ToString('N'))
    $script:EmptyHooks = Join-Path -Path $work -ChildPath '.empty-hooks'
    New-Item -ItemType Directory -Path $script:EmptyHooks -Force | Out-Null
    try {
        # 写入分支解析
        $root = New-ShapeFixture -Parent $work -Name 'resolve'
        Assert-Equal 'main' (Get-ShapeWriterBranch -RepositoryRoot $root) 'origin/HEAD 指向 main'
        Invoke-FixtureGit -Root $root -Arguments @('config', 'shape.writerBranch', 'trunk') | Out-Null
        Assert-Equal 'trunk' (Get-ShapeWriterBranch -RepositoryRoot $root) 'shape.writerBranch 优先'

        # pre-commit（索引）
        $root = New-ShapeFixture -Parent $work -Name 'index'
        Invoke-ShapeIndexCheck -RepositoryRoot $root -ChangedPath @('docs/shape/architecture.md')
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-qc', 'feature/pause') | Out-Null
        Assert-Throws -Action { Invoke-ShapeIndexCheck -RepositoryRoot $root -ChangedPath @('src/a.c', 'docs/shape/architecture.md') } `
            -Pattern 'docs/shape/inbox/feature-pause\.md' -Message '分支改领域文件应被拦并指出 inbox'
        Invoke-ShapeIndexCheck -RepositoryRoot $root -ChangedPath @('docs/shape/inbox/feature-pause.md')
        Invoke-FixtureGit -Root $root -Arguments @('mv', 'docs/shape/architecture.md', 'docs/moved.md') | Out-Null
        Assert-Throws -Action { Invoke-ShapeIndexCheck -RepositoryRoot $root } `
            -Pattern 'docs/shape/architecture\.md' -Message '把领域文件改名移出 docs/shape/ 也应被拦'
        Invoke-FixtureGit -Root $root -Arguments @('reset', '-q', '--hard') | Out-Null

        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'main') | Out-Null
        Set-FixtureText -Root $root -Path 'docs/shape/architecture.md' -Text 'decided'
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'rule on main') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'feature/pause') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('merge', '-q', '--no-commit', '--no-ff', 'main') | Out-Null
        Invoke-ShapeIndexCheck -RepositoryRoot $root -ChangedPath @('docs/shape/architecture.md')
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qm', 'merge main') | Out-Null

        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', '--detach') | Out-Null
        Invoke-ShapeIndexCheck -RepositoryRoot $root -ChangedPath @('docs/shape/architecture.md')

        # pre-push
        $root = New-ShapeFixture -Parent $work -Name 'push'
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-qc', 'feature/a') | Out-Null
        Set-FixtureText -Root $root -Path 'docs/shape/architecture.md' -Text 'edit'
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'edit area') | Out-Null
        $violation = Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef 'feature/a' -RemoteRef 'refs/heads/feature/a')
        if (($null -eq $violation) -or ($violation -notmatch 'docs/shape/inbox/feature-a\.md')) {
            throw "推送改了领域文件的分支应报告违规，实际：$violation"
        }
        Assert-Equal $null (Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef 'feature/a' -RemoteRef 'refs/heads/main')) '推到写入分支放行'
        Assert-Equal $null (Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef 'feature/a' -RemoteRef 'refs/tags/v1')) 'tag 不判断'
        Assert-Equal $null (Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef ('0' * 40) -RemoteRef 'refs/heads/feature/a')) '删除远端分支放行'

        Invoke-FixtureGit -Root $root -Arguments @('reset', '-q', '--hard', 'HEAD~1') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'main') | Out-Null
        Set-FixtureText -Root $root -Path 'docs/shape/architecture.md' -Text 'decided'
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'rule on main') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('push', '-q', 'origin', 'main') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'feature/a') | Out-Null
        Set-FixtureText -Root $root -Path 'src/a.c' -Text 'int b;'
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'work') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('merge', '-q', '--no-edit', 'main') | Out-Null
        Assert-Equal $null (Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef 'feature/a' -RemoteRef 'refs/heads/feature/a')) '合入写入分支不算分支自己的改动'

        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'main') | Out-Null
        Set-FixtureText -Root $root -Path 'docs/shape/architecture.md' -Text 'newer, not pushed'
        Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'newer rule') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'feature/a') | Out-Null
        Invoke-FixtureGit -Root $root -Arguments @('merge', '-q', '--no-edit', 'main') | Out-Null
        Assert-Equal $null (Get-ShapePushViolation -RepositoryRoot $root -RemoteName 'origin' `
            -Update (New-PushUpdate -Root $root -LocalRef 'feature/a' -RemoteRef 'refs/heads/feature/a')) 'origin 过期时取较新的 merge-base'

        # SessionStart 提示
        Assert-Equal $null (Get-ShapeInboxReminder -RepositoryRoot $root) '非写入分支不提示'
        Invoke-FixtureGit -Root $root -Arguments @('switch', '-q', 'main') | Out-Null
        Assert-Equal $null (Get-ShapeInboxReminder -RepositoryRoot $root) '没有 inbox 不提示'
        Set-FixtureText -Root $root -Path 'docs/shape/inbox/feature-a.md' -Text '- x'
        $reminder = Get-ShapeInboxReminder -RepositoryRoot $root
        if (($null -eq $reminder) -or ($reminder -notmatch '1 个文件')) {
            throw "写入分支有 inbox 时应提示，实际：$reminder"
        }
    }
    finally {
        if (Test-Path -LiteralPath $work) {
            Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}
finally {
    Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment
}

Write-NativeUtf8Line -Text 'shape 写入分支检查测试通过。'
