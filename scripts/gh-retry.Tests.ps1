Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# gh-retry.ps1：以函数替身代替真实 gh，核对瞬断重试、重试耗尽与非瞬断错误原样返回。
$script:GhRetry = Join-Path -Path $PSScriptRoot -ChildPath 'gh-retry.ps1'
$env:GH_RETRY_DELAY_SECONDS = '0'

# 替身按脚本化的结果序列依次返回；记录每次收到的参数。
function gh {
    $global:GhInputs += , (@($input) -join "`n")
    $global:GhCalls += , @($args)
    $step = $global:GhSteps[[Math]::Min($global:GhCalls.Count, $global:GhSteps.Count) - 1]
    if ($step.Text) { Write-Output $step.Text }
    $global:LASTEXITCODE = $step.Code
}

function Invoke-GhRetryCase {
    param([Parameter(Mandatory)][object[]]$Steps, [string[]]$GhArguments = @('issue', 'comment', '7', '--body', 'x'), [string[]]$Stdin)
    $global:GhCalls = @()
    $global:GhInputs = @()
    $global:GhSteps = $Steps
    if ($PSBoundParameters.ContainsKey('Stdin')) {
        $output = @($Stdin | & $script:GhRetry @GhArguments 2>$null)
    }
    else {
        $output = @(& $script:GhRetry @GhArguments 2>$null)
    }
    return [pscustomobject]@{ Code = $LASTEXITCODE; Output = ($output -join "`n"); Calls = $global:GhCalls; Inputs = $global:GhInputs }
}

$blip = @{ Code = 1; Text = 'error connecting to api.github.com: Post "https://api.github.com/graphql": EOF' }
$ok = @{ Code = 0; Text = 'done' }

# 1. 成功：只调用一次，参数原样传递。
$r = Invoke-GhRetryCase -Steps @($ok)
if (($r.Code -ne 0) -or ($r.Calls.Count -ne 1) -or ($r.Output -ne 'done')) { throw "成功用例异常：$($r | ConvertTo-Json -Compress)" }
if (($r.Calls[0] -join ' ') -ne 'issue comment 7 --body x') { throw "参数应原样传递：$($r.Calls[0] -join ' ')" }

# 2. EOF 瞬断后重试成功。
$r = Invoke-GhRetryCase -Steps @($blip, $blip, $ok)
if (($r.Code -ne 0) -or ($r.Calls.Count -ne 3)) { throw "瞬断后应第 3 次成功：$($r | ConvertTo-Json -Compress)" }

# 3. connection reset 与 HTTP 5xx 也算瞬断。
foreach ($text in @('read tcp: Connection reset by peer', 'HTTP 502: Bad Gateway (https://api.github.com/x)', 'gh: HTTP 503 Service Unavailable')) {
    $r = Invoke-GhRetryCase -Steps @(@{ Code = 1; Text = $text }, $ok)
    if (($r.Code -ne 0) -or ($r.Calls.Count -ne 2)) { throw "应按瞬断重试：$text" }
}

# 4. 重试耗尽：共 1+3 次，退出码与输出为最后一次。
$r = Invoke-GhRetryCase -Steps @($blip)
if (($r.Code -ne 1) -or ($r.Calls.Count -ne 4) -or ($r.Output -notmatch 'EOF')) { throw "耗尽应调用 4 次并返回失败：$($r | ConvertTo-Json -Compress)" }

# 5. 其余错误（含 HTTP 4xx）原样返回，不重试。
foreach ($text in @('HTTP 404: Not Found', 'GraphQL: Could not resolve to an Issue with the number of 9999.', 'HTTP 422: Validation Failed')) {
    $r = Invoke-GhRetryCase -Steps @(@{ Code = 1; Text = $text })
    if (($r.Code -ne 1) -or ($r.Calls.Count -ne 1) -or ($r.Output -ne $text)) { throw "非瞬断错误应原样返回且不重试：$text -> $($r | ConvertTo-Json -Compress)" }
}

# 6. 管道输入：首次调用前缓冲，每次尝试（含重试）都把同一份正文喂给 gh；无管道输入时 gh 收到空输入。
$body = @('第一行 `code`', 'line two')
$r = Invoke-GhRetryCase -Steps @($blip, $blip, $ok) -GhArguments @('issue', 'comment', '7', '--body-file', '-') -Stdin $body
if (($r.Code -ne 0) -or ($r.Calls.Count -ne 3)) { throw "带 stdin 的瞬断重试应第 3 次成功：$($r | ConvertTo-Json -Compress)" }
foreach ($index in 0..2) {
    if ($r.Inputs[$index] -ne ($body -join "`n")) { throw "第 $($index + 1) 次尝试应收到同一份正文，实际：$($r.Inputs[$index])" }
}
$r = Invoke-GhRetryCase -Steps @($ok)
if ($r.Inputs[0] -ne '') { throw "无管道输入时 gh 不应收到正文：$($r.Inputs[0])" }

Remove-Item -LiteralPath Function:\gh
Write-Output 'gh 重试包装脚本测试通过。'
$global:LASTEXITCODE = 0
