Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# merge-into-integration.ps1：真实 Git 夹具上的成功合并、冲突中止与推送被拒后复位。
. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment
$script:MergeScript = Join-Path -Path $PSScriptRoot -ChildPath 'merge-into-integration.ps1'

function Invoke-FixtureGit {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string[]]$Arguments)
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & git -C $Root @Arguments 2>&1
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
    Set-Content -LiteralPath $full -Value $Text -Encoding ascii
}

# 夹具：origin 裸仓库上有 main 与 integration/x；本地另有 feature（改 a.txt）与 clash（改 shared.txt）。
function New-MergeFixture {
    param([Parameter(Mandatory)][string]$Parent)
    $root = Join-Path -Path $Parent -ChildPath 'work'
    $bare = Join-Path -Path $Parent -ChildPath 'origin.git'
    $hooks = Join-Path -Path $Parent -ChildPath 'no-hooks'
    New-Item -ItemType Directory -Path $root, $hooks -Force | Out-Null
    Invoke-FixtureGit -Root $Parent -Arguments @('init', '-q', '--bare', $bare) | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('init', '-q', '-b', 'main') | Out-Null
    foreach ($pair in @(@('core.autocrlf', 'false'), @('core.hooksPath', $hooks), @('commit.gpgsign', 'false'),
            @('user.email', 'merge-test@example.invalid'), @('user.name', 'merge-test'))) {
        Invoke-FixtureGit -Root $root -Arguments @('config', $pair[0], $pair[1]) | Out-Null
    }
    Set-FixtureText -Root $root -Path 'shared.txt' -Text 'base'
    Invoke-FixtureGit -Root $root -Arguments @('add', '-A') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('commit', '-qm', 'base') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('remote', 'add', 'origin', $bare) | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('push', '-q', 'origin', 'main') | Out-Null

    Invoke-FixtureGit -Root $root -Arguments @('checkout', '-q', '-b', 'integration/x') | Out-Null
    Set-FixtureText -Root $root -Path 'shared.txt' -Text 'integration'
    Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'integration change') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('push', '-q', 'origin', 'integration/x') | Out-Null

    Invoke-FixtureGit -Root $root -Arguments @('checkout', '-q', '-b', 'feature', 'integration/x') | Out-Null
    Set-FixtureText -Root $root -Path 'a.txt' -Text 'feature'
    Invoke-FixtureGit -Root $root -Arguments @('add', '-A') | Out-Null
    Invoke-FixtureGit -Root $root -Arguments @('commit', '-qm', 'feature change') | Out-Null

    Invoke-FixtureGit -Root $root -Arguments @('checkout', '-q', '-b', 'clash', 'main') | Out-Null
    Set-FixtureText -Root $root -Path 'shared.txt' -Text 'clash'
    Invoke-FixtureGit -Root $root -Arguments @('commit', '-qam', 'clash change') | Out-Null

    Invoke-FixtureGit -Root $root -Arguments @('checkout', '-q', '--detach', 'main') | Out-Null
    return [pscustomobject]@{ Root = $root; Hooks = $hooks }
}

# 写一个按计数失败的 pre-push Hook：前 FailUntil 次输出 Message（及 NoiseLines 行噪声）并失败，之后放行。
function Set-PrePushHook {
    param(
        [Parameter(Mandatory)][string]$Hooks,
        [Parameter(Mandatory)][string]$Counter,
        [Parameter(Mandatory)][int]$FailUntil,
        [Parameter(Mandatory)][string]$Message,
        [int]$NoiseLines = 0
    )
    $counterPath = $Counter.Replace('\', '/')
    $lines = @(
        '#!/bin/sh'
        "COUNT='$counterPath'"
        'n=$(cat "$COUNT" 2>/dev/null || echo 0)'
        'n=$((n+1))'
        'echo $n > "$COUNT"'
        "if [ `"`$n`" -le $FailUntil ]; then"
        "  i=1; while [ `$i -le $NoiseLines ]; do echo `"noise line `$i`" >&2; i=`$((i+1)); done"
        "  echo '$Message' >&2"
        '  exit 1'
        'fi'
        'exit 0'
    )
    [IO.File]::WriteAllText((Join-Path $Hooks 'pre-push'), (($lines -join "`n") + "`n"))
}

function Invoke-MergeScript {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][hashtable]$Parameters)
    Push-Location -LiteralPath $Root
    try {
        & $script:MergeScript @Parameters | Out-Null
        return $null
    }
    catch {
        return $_.Exception.Message
    }
    finally {
        Pop-Location
    }
}

$temp = Join-Path -Path ([IO.Path]::GetTempPath()) -ChildPath ('merge-into-integration-' + [Guid]::NewGuid().ToString('N'))
try {
    # 1. 成功：远端集成分支前进一个 --no-ff 合并提交，标题与 trailer 按约定，调用方回到原 HEAD。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'ok')
    $start = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'HEAD'))[0]
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{
        Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature'; Ticket = 7; CoAuthor = 'Tester <t@example.invalid>'
    }
    if ($null -ne $failure) { throw "成功用例抛错：$failure" }
    Invoke-FixtureGit -Root $fixture.Root -Arguments @('fetch', '-q', 'origin') | Out-Null
    $subject = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('log', '-1', '--format=%s', 'origin/integration/x'))[0]
    if ($subject -ne 'chore(merge): #7 add feature') { throw "合并标题不符：$subject" }
    $parents = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('log', '-1', '--format=%P', 'origin/integration/x'))[0]
    if (@($parents -split ' ').Count -ne 2) { throw "应为 --no-ff 合并提交，父提交：$parents" }
    $body = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('log', '-1', '--format=%b', 'origin/integration/x')) -join "`n"
    if ($body -notmatch 'Co-Authored-By: Tester') { throw "缺少 Co-Authored-By：$body" }
    $now = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'HEAD'))[0]
    if ($now -ne $start) { throw "调用方未回到原 HEAD：$now ≠ $start" }

    # 2. 冲突：中止合并、不推送，远端不变，无 MERGE_HEAD。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'clash')
    $before = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'origin/integration/x'))[0]
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{ Branch = 'clash'; Integration = 'integration/x'; Summary = 'clash' }
    if (($null -eq $failure) -or ($failure -notmatch '合并冲突')) { throw "冲突用例应以「合并冲突」失败：$failure" }
    Invoke-FixtureGit -Root $fixture.Root -Arguments @('fetch', '-q', 'origin') | Out-Null
    $after = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'origin/integration/x'))[0]
    if ($after -ne $before) { throw '冲突后远端集成分支不应变化' }
    if (Test-Path -LiteralPath (Join-Path $fixture.Root '.git/MERGE_HEAD')) { throw '冲突后不应残留 MERGE_HEAD' }

    # 3. 推送被拒（pre-push 失败）：本地集成分支复位到远端。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'rejected')
    [IO.File]::WriteAllText((Join-Path $fixture.Hooks 'pre-push'), "#!/bin/sh`nexit 1`n")
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{ Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature' }
    if (($null -eq $failure) -or ($failure -notmatch '推送被拒')) { throw "推送被拒用例应以「推送被拒」失败：$failure" }
    $localTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'refs/heads/integration/x'))[0]
    $remoteTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'origin/integration/x'))[0]
    if ($localTip -ne $remoteTip) { throw "推送被拒后本地集成分支应复位到远端：$localTip ≠ $remoteTip" }

    # 4. 本地集成分支有未推送提交：拒绝执行。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'diverged')
    Invoke-FixtureGit -Root $fixture.Root -Arguments @('branch', '-f', 'integration/x', 'feature') | Out-Null
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{ Branch = 'feature'; Integration = 'integration/x'; Summary = 'x' }
    if (($null -eq $failure) -or ($failure -notmatch '不一致')) { throw "本地与远端不一致时应拒绝：$failure" }

    # 5. 推送遇网络瞬断：原地重试直到成功；远端前进，本地不复位。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'transient')
    $counter = Join-Path $fixture.Hooks 'count.txt'
    Set-PrePushHook -Hooks $fixture.Hooks -Counter $counter -FailUntil 2 -Message 'fatal: unable to access: schannel: failed to receive handshake (curl 35)'
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{
        Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature'; RetryDelaySeconds = 0
    }
    if ($null -ne $failure) { throw "瞬断后重试成功的用例抛错：$failure" }
    $attempts = [int](Get-Content -LiteralPath $counter -Raw)
    if ($attempts -ne 3) { throw "前两次瞬断应在第 3 次推送成功，实际推送 $attempts 次" }
    Invoke-FixtureGit -Root $fixture.Root -Arguments @('fetch', '-q', 'origin') | Out-Null
    $subject = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('log', '-1', '--format=%s', 'origin/integration/x'))[0]
    if ($subject -ne 'chore(merge): add feature') { throw "重试成功后远端应含合并提交：$subject" }

    # 6. Hook 失败（非网络原因）：只推送一次，立即复位，报错只带输出末尾约 8 行且不重复。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'hookfail')
    $counter = Join-Path $fixture.Hooks 'count.txt'
    Set-PrePushHook -Hooks $fixture.Hooks -Counter $counter -FailUntil 99 -Message 'verify FAILED: gui scene mismatch' -NoiseLines 30
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{
        Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature'; RetryDelaySeconds = 0
    }
    if (($null -eq $failure) -or ($failure -notmatch '推送被拒')) { throw "Hook 失败应以「推送被拒」失败：$failure" }
    $attempts = [int](Get-Content -LiteralPath $counter -Raw)
    if ($attempts -ne 1) { throw "Hook 失败不应重试，实际推送 $attempts 次" }
    $localTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'refs/heads/integration/x'))[0]
    $remoteTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'origin/integration/x'))[0]
    if ($localTip -ne $remoteTip) { throw 'Hook 失败后本地集成分支应复位到远端' }
    $markerCount = ([regex]::Matches($failure, 'verify FAILED')).Count
    if ($markerCount -ne 1) { throw "Hook 输出应只出现一次，实际 $markerCount 次：$failure" }
    if ($failure -match 'noise line 1\b') { throw "报错应只带输出末尾，不应含最早的噪声行：$failure" }
    if ($failure -notmatch 'noise line 30\b') { throw "报错应含输出末尾行：$failure" }

    # 7. 重试耗尽：推送 1+3 次后复位并报错。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'exhausted')
    $counter = Join-Path $fixture.Hooks 'count.txt'
    Set-PrePushHook -Hooks $fixture.Hooks -Counter $counter -FailUntil 99 -Message 'fatal: the remote end hung up unexpectedly'
    $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{
        Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature'; RetryDelaySeconds = 0
    }
    if (($null -eq $failure) -or ($failure -notmatch '重试')) { throw "重试耗尽应报错并提到重试：$failure" }
    $attempts = [int](Get-Content -LiteralPath $counter -Raw)
    if ($attempts -ne 4) { throw "应推送 1 次加重试 3 次共 4 次，实际 $attempts 次" }
    $localTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'refs/heads/integration/x'))[0]
    $remoteTip = @(Invoke-FixtureGit -Root $fixture.Root -Arguments @('rev-parse', 'origin/integration/x'))[0]
    if ($localTip -ne $remoteTip) { throw '重试耗尽后本地集成分支应复位到远端' }

    # 8. 首次 fetch 遇瞬断：重试后继续合并（以同名函数替身让前两次 fetch 失败）。
    $fixture = New-MergeFixture -Parent (Join-Path $temp 'fetchblip')
    $global:FetchFailuresLeft = 2
    function git {
        if (($args -contains 'fetch') -and ($global:FetchFailuresLeft -gt 0)) {
            $global:FetchFailuresLeft--
            Write-Output 'fatal: unable to access ''https://github.com/x/y.git/'': Could not resolve host: github.com'
            $global:LASTEXITCODE = 128
            return
        }
        & git.exe @args
    }
    try {
        $failure = Invoke-MergeScript -Root $fixture.Root -Parameters @{
            Branch = 'feature'; Integration = 'integration/x'; Summary = 'add feature'; RetryDelaySeconds = 0
        }
    }
    finally {
        Remove-Item -LiteralPath Function:\git
    }
    if ($null -ne $failure) { throw "fetch 瞬断后重试成功的用例抛错：$failure" }
    if ($global:FetchFailuresLeft -ne 0) { throw 'fetch 替身应被调用过两次' }

    Write-Output '集成分支合并脚本测试通过。'
}
finally {
    Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
$global:LASTEXITCODE = 0
