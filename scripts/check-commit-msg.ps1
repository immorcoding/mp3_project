[CmdletBinding()]
param(
    [string]$MessagePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

# Conventional Commits 标题（GIT-2）：type(scope)!: 描述。
$script:CommitTitlePattern = '^(feat|fix|refactor|perf|test|docs|build|ci|chore|revert)(\([A-Za-z0-9_./-]+\))?!?: \S.*$'

# Git 自动生成的标题不受格式约束。
$script:CommitTitleExemptPattern = '^(Merge |Revert "|fixup! |squash! |amend! )'

# 标题合规时返回 $null，否则返回原因。
function Get-CommitTitleViolation {
    param(
        [AllowEmptyString()][string]$Title
    )

    if ([string]::IsNullOrWhiteSpace($Title)) {
        return '提交标题为空。'
    }

    if ($Title -match $script:CommitTitleExemptPattern) {
        return $null
    }

    if ($Title -cnotmatch $script:CommitTitlePattern) {
        return "提交标题不符合 Conventional Commits（docs/shape/git.md GIT-2）：$Title`n格式：type(scope): 中文描述，type 取 feat|fix|refactor|perf|test|docs|build|ci|chore|revert，例：feat(gui): 唱盘随播放状态旋转"
    }

    return $null
}

# 取第一行非注释、非空的内容作为标题。
function Get-CommitTitle {
    param(
        [Parameter(Mandatory)][string]$Path
    )

    foreach ($line in [System.IO.File]::ReadAllLines($Path, (New-Object System.Text.UTF8Encoding($false)))) {
        if ($line.StartsWith('#') -or [string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        return $line.TrimEnd()
    }

    return ''
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $violation = Get-CommitTitleViolation -Title (Get-CommitTitle -Path $MessagePath)
        if ($null -ne $violation) {
            throw $violation
        }
    }
}
