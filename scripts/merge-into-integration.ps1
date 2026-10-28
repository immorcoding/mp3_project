[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Branch,
    [Parameter(Mandatory)][string]$Integration,
    [Parameter(Mandatory)][string]$Summary,
    [int]$Ticket = 0,
    [string]$CoAuthor,
    [string]$Remote = 'origin',
    # 网络瞬断时 fetch/push 的重试次数（不含首次）与间隔。
    [int]$RetryCount = 3,
    [double]$RetryDelaySeconds = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# implement-spec 的合并步骤：把实施分支以 --no-ff 合入集成分支并推送。提交标题固定为
# `chore(merge): #<票号> <摘要>`（无票号时省略 `#<票号> `），commit-msg Hook 接受此形式。
# 在调用方当前 worktree 内执行，须无未提交改动；结束时回到调用前的 HEAD（分离）。
# 本地集成分支与远端不一致时拒绝执行；冲突时中止合并、不推送；推送被拒（如 pre-push 失败）时
# 把本地集成分支复位到远端。fetch/push 遇网络瞬断（见 $TransientPattern）时原地重试，pre-push Hook
# 失败等非网络原因不重试；报错只带最后一个 HARNESS_STATUS= 行与输出末尾 $ErrorTailLines 行。成功退出码 0，失败抛错；只输出结果行，不输出 HARNESS_STATUS。

function Invoke-MergeGit {
    param([Parameter(Mandatory)][string[]]$Arguments, [switch]$AllowFailure)

    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& git @Arguments 2>&1 | ForEach-Object { [string]$_ })
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if (($exitCode -ne 0) -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') 失败（$exitCode）：$($output -join ' ')"
    }
    return [pscustomobject]@{ ExitCode = $exitCode; Output = $output }
}

$TransientPattern = '(?i)curl\s*35|schannel|RPC failed|unexpected disconnect|remote end hung up|Could not resolve host|Connection reset|Connection timed out|early EOF'
$ErrorTailLines = 12
$StatusLinePattern = '^\s*HARNESS_STATUS='

function Get-OutputLine {
    param([string[]]$Output)
    return @($Output | ForEach-Object { $_ -split "`r?`n" } | Where-Object { $_ -ne '' })
}

# 报错只带输出的最后一个 HARNESS_STATUS= 行（若有）加末尾 $ErrorTailLines 行。
function Get-OutputTail {
    param([string[]]$Output)
    $lines = @(Get-OutputLine -Output $Output)
    if ($lines.Count -le $ErrorTailLines) { return ($lines -join "`n") }
    $tail = @($lines | Select-Object -Last $ErrorTailLines)
    $head = @()
    $statusIndex = -1
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match $StatusLinePattern) { $statusIndex = $i; break }
    }
    if (($statusIndex -ge 0) -and ($statusIndex -lt ($lines.Count - $ErrorTailLines))) {
        $head = @($lines[$statusIndex])
    }
    return "…（中间 $($lines.Count - $ErrorTailLines - $head.Count) 行略）`n" + ((@($head) + $tail) -join "`n")
}

# 瞬断只看最后一个 HARNESS_STATUS= 行之后的输出（Hook 已跑完，后面才是 git/网络层的输出）；
# 没有该行时看全部。Hook 日志里的 schannel、timed out 之类因此不会触发重跑 pre-push。
function Test-TransientOutput {
    param([string[]]$Output)
    $lines = @(Get-OutputLine -Output $Output)
    $start = 0
    for ($i = $lines.Count - 1; $i -ge 0; $i--) {
        if ($lines[$i] -match $StatusLinePattern) { $start = $i + 1; break }
    }
    if ($start -ge $lines.Count) { return $false }
    return (($lines[$start..($lines.Count - 1)] -join "`n") -match $TransientPattern)
}

# 失败且输出匹配瞬断特征时间隔重试，最多 $RetryCount 次；返回最后一次结果并附 Attempts。
function Invoke-MergeGitWithRetry {
    param([Parameter(Mandatory)][string[]]$Arguments)

    $attempt = 0
    while ($true) {
        $attempt++
        $result = Invoke-MergeGit -Arguments $Arguments -AllowFailure
        $transient = ($result.ExitCode -ne 0) -and (Test-TransientOutput -Output $result.Output)
        if (($result.ExitCode -eq 0) -or -not $transient -or ($attempt -gt $RetryCount)) {
            return [pscustomobject]@{ ExitCode = $result.ExitCode; Output = $result.Output; Attempts = $attempt; Transient = $transient }
        }
        Write-Warning "git $($Arguments[0]) 遇网络瞬断，$RetryDelaySeconds 秒后重试（$attempt/$RetryCount）。"
        if ($RetryDelaySeconds -gt 0) { Start-Sleep -Seconds $RetryDelaySeconds }
    }
}

$dirty = (Invoke-MergeGit -Arguments @('status', '--porcelain', '--untracked-files=no')).Output
if ($dirty.Count -gt 0) {
    throw '当前 worktree 有未提交改动；先提交或清理后再合并。'
}
$startHead = (Invoke-MergeGit -Arguments @('rev-parse', 'HEAD')).Output[0]

$fetch = Invoke-MergeGitWithRetry -Arguments @('fetch', '-q', $Remote)
if ($fetch.ExitCode -ne 0) {
    throw "git fetch 失败（$($fetch.ExitCode)，共尝试 $($fetch.Attempts) 次）：`n$(Get-OutputTail -Output $fetch.Output)"
}
$remoteRef = "$Remote/$Integration"
$remoteSha = (Invoke-MergeGit -Arguments @('rev-parse', '--verify', '-q', "$remoteRef^{commit}")).Output[0]
$local = Invoke-MergeGit -Arguments @('rev-parse', '--verify', '-q', "refs/heads/$Integration^{commit}") -AllowFailure
if (($local.ExitCode -eq 0) -and ($local.Output[0] -ne $remoteSha)) {
    throw "本地 $Integration（$($local.Output[0])）与 $remoteRef（$remoteSha）不一致；先核对未推送的提交。"
}

$subject = if ($Ticket -gt 0) { "chore(merge): #$Ticket $Summary" } else { "chore(merge): $Summary" }
$messageArguments = @('-m', $subject)
if (-not [string]::IsNullOrWhiteSpace($CoAuthor)) {
    $messageArguments += @('-m', "Co-Authored-By: $CoAuthor")
}

try {
    Invoke-MergeGit -Arguments @('checkout', '-q', '-B', $Integration, $remoteRef) | Out-Null

    $merge = Invoke-MergeGit -Arguments (@('merge', '--no-ff') + $messageArguments + @($Branch)) -AllowFailure
    if ($merge.ExitCode -ne 0) {
        Invoke-MergeGit -Arguments @('merge', '--abort') -AllowFailure | Out-Null
        throw "合并冲突，已中止，未推送：$Branch → $Integration。`n$($merge.Output -join "`n")"
    }

    $push = Invoke-MergeGitWithRetry -Arguments @('push', $Remote, $Integration)
    if ($push.ExitCode -ne 0) {
        Invoke-MergeGit -Arguments @('reset', '-q', '--hard', $remoteRef) -AllowFailure | Out-Null
        $tail = Get-OutputTail -Output $push.Output
        if ($push.Transient) {
            throw "推送失败（网络瞬断，已重试 $($push.Attempts - 1) 次仍失败），本地 $Integration 已复位到 $remoteRef。`n$tail"
        }
        throw "推送被拒，本地 $Integration 已复位到 $remoteRef。`n$tail"
    }

    $tip = (Invoke-MergeGit -Arguments @('rev-parse', '--short', 'HEAD')).Output[0]
    $statusLine = @($push.Output | Where-Object { $_ -match '^HARNESS_STATUS=' }) | Select-Object -Last 1
    Write-Output "已合并并推送：$Branch → $Integration，新提交 $tip（$subject）。"
    if ($null -ne $statusLine) {
        Write-Output "pre-push $statusLine"
    }
}
finally {
    Invoke-MergeGit -Arguments @('checkout', '-q', '--detach', $startHead) -AllowFailure | Out-Null
}
