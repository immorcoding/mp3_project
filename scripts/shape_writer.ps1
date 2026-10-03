Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# shape 写入分支：领域文件 docs/shape/*.md 只在写入分支改，其他分支写本分支 inbox。
# 约定与 shape-your-project skill 的 hooks/ 样例一致；由 FAST（pre-commit）、CHANGED（pre-push）、
# Agent Hook 与 SessionStart 简报点源使用。只依赖 git，不依赖其他 Harness 脚本。

function Invoke-ShapeGit {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & git -C $RepositoryRoot -c core.quotepath=false @Arguments 2>$null
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Lines = @($output | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    }
}

# 写入分支：git config shape.writerBranch，否则 origin/HEAD 指向的分支，否则 main。
function Get-ShapeWriterBranch {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $configured = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('config', '--get', 'shape.writerBranch')
    if (($configured.ExitCode -eq 0) -and ($configured.Lines.Count -gt 0)) {
        return $configured.Lines[0].Trim()
    }

    $remoteHead = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('symbolic-ref', '--quiet', '--short', 'refs/remotes/origin/HEAD')
    if (($remoteHead.ExitCode -eq 0) -and ($remoteHead.Lines.Count -gt 0)) {
        return ($remoteHead.Lines[0].Trim() -replace '^origin/', '')
    }

    return 'main'
}

# 当前分支名；分离 HEAD（推送快照、rebase 中）返回 $null，表示无从判断。
function Get-ShapeCurrentBranch {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $result = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('symbolic-ref', '--quiet', '--short', 'HEAD')
    if (($result.ExitCode -ne 0) -or ($result.Lines.Count -eq 0)) {
        return $null
    }
    return $result.Lines[0].Trim()
}

function Test-ShapeAreaPath {
    param([Parameter(Mandatory)][AllowEmptyString()][string]$RelativePath)

    return (($RelativePath -replace '\\', '/').TrimStart('/')) -match '^docs/shape/[^/]+\.md$'
}

function Get-ShapeInboxPath {
    param([Parameter(Mandatory)][string]$Branch)

    return 'docs/shape/inbox/' + ($Branch -replace '/', '-') + '.md'
}

function Get-ShapeWriterViolationMessage {
    param(
        [Parameter(Mandatory)][string]$Branch,
        [Parameter(Mandatory)][string]$WriterBranch,
        [Parameter(Mandatory)][string[]]$Path
    )

    $lines = @("领域文件只在 $WriterBranch 上改，当前是 ${Branch}：")
    $lines += @($Path | ForEach-Object { "  $_" })
    $lines += "请把 Signal 与草稿写进 $(Get-ShapeInboxPath -Branch $Branch)，合并后在 $WriterBranch 上处理（shape-your-project drain）。"
    return $lines -join [Environment]::NewLine
}

# pre-commit：暂存区改了领域文件且不在写入分支时抛出。合并提交交给 pre-push 判断。
function Invoke-ShapeIndexCheck {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [string[]]$ChangedPath
    )

    $merging = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('rev-parse', '-q', '--verify', 'MERGE_HEAD')
    if ($merging.ExitCode -eq 0) {
        return
    }
    $branch = Get-ShapeCurrentBranch -RepositoryRoot $RepositoryRoot
    if ($null -eq $branch) {
        return
    }
    $writer = Get-ShapeWriterBranch -RepositoryRoot $RepositoryRoot
    if ($branch -eq $writer) {
        return
    }

    # 未给出路径时自取暂存区，--no-renames 让改名或移出 docs/shape/ 也列出旧路径。
    if (-not $PSBoundParameters.ContainsKey('ChangedPath')) {
        $ChangedPath = (Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('diff', '--cached', '--name-only', '--no-renames')).Lines
    }
    $areaPaths = @($ChangedPath | Where-Object { Test-ShapeAreaPath -RelativePath $_ } | Sort-Object)
    if ($areaPaths.Count -gt 0) {
        throw (Get-ShapeWriterViolationMessage -Branch $branch -WriterBranch $writer -Path $areaPaths)
    }
}

# pre-push：按推送目标分支判断，只看分支自己的改动（与写入分支最新的 merge-base 起算），
# 所以把写入分支合进来不会误报。违规返回说明文字，否则返回 $null。
function Get-ShapePushViolation {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RemoteName,
        [Parameter(Mandatory)]$Update
    )

    if ($Update.RemoteRef -notmatch '^refs/heads/(?<target>.+)$') {
        return $null
    }
    $target = $Matches['target']
    if ($Update.LocalSha -match '^0+$') {
        return $null
    }
    $writer = Get-ShapeWriterBranch -RepositoryRoot $RepositoryRoot
    if ($target -eq $writer) {
        return $null
    }

    # 本地与远端跟踪的写入分支取较新的 merge-base，过期的 origin 引用不会误报已合入的写入分支改动。
    $base = $null
    foreach ($ref in @("refs/remotes/$RemoteName/$writer", "refs/heads/$writer")) {
        if ((Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('rev-parse', '-q', '--verify', $ref)).ExitCode -ne 0) {
            continue
        }
        $candidate = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('merge-base', $ref, $Update.LocalSha)
        if (($candidate.ExitCode -ne 0) -or ($candidate.Lines.Count -eq 0)) {
            continue
        }
        $candidateSha = $candidate.Lines[0].Trim()
        if (($null -eq $base) -or
            ((Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('merge-base', '--is-ancestor', $base, $candidateSha)).ExitCode -eq 0)) {
            $base = $candidateSha
        }
    }
    if ($null -eq $base) {
        return $null
    }

    $diff = Invoke-ShapeGit -RepositoryRoot $RepositoryRoot -Arguments @('diff', '--name-only', '--no-renames', $base, $Update.LocalSha, '--')
    $areaPaths = @($diff.Lines | Where-Object { Test-ShapeAreaPath -RelativePath $_ } | Sort-Object)
    if ($areaPaths.Count -eq 0) {
        return $null
    }
    return Get-ShapeWriterViolationMessage -Branch $target -WriterBranch $writer -Path $areaPaths
}

# SessionStart：写入分支上有未处理的 inbox 时返回提示，否则返回 $null。
function Get-ShapeInboxReminder {
    param([Parameter(Mandatory)][string]$RepositoryRoot)

    $branch = Get-ShapeCurrentBranch -RepositoryRoot $RepositoryRoot
    if (($null -eq $branch) -or ($branch -ne (Get-ShapeWriterBranch -RepositoryRoot $RepositoryRoot))) {
        return $null
    }
    $inbox = Join-Path -Path $RepositoryRoot -ChildPath 'docs/shape/inbox'
    $files = @(Get-ChildItem -LiteralPath $inbox -Filter '*.md' -File -ErrorAction SilentlyContinue)
    if ($files.Count -eq 0) {
        return $null
    }
    return "shape：docs/shape/inbox/ 有 $($files.Count) 个文件待处理（shape-your-project drain）。"
}
