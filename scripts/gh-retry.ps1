# gh 写操作的瞬断重试包装：用 gh 的完整参数调用，如 `issue comment 7 --body-file <临时文件>`。
# 失败且输出含 EOF、connection reset 或 HTTP 5xx 时间隔重试，最多 3 次（不含首次）；其余错误原样返回。
# 间隔默认 2 秒，可用环境变量 GH_RETRY_DELAY_SECONDS 覆盖。
#
# 调用方式：
#   PowerShell：  & ./scripts/gh-retry.ps1 issue comment 7 --body-file note.md
#   bash：        powershell -NoProfile -Command "& ./scripts/gh-retry.ps1 issue comment 7 --body-file note.md"
# 正文一律写进临时文件后用 --body-file <文件路径> 传入。不要用 `powershell -File ... --body-file -`：
# PowerShell 5.1 绑定单独的 `-` 会失败。
#
# stdin：有管道输入（或参数里有单独的 `-` 且 stdin 被重定向）时，首次调用前一次性读入缓冲，
# 每次尝试（含重试）都把同一份缓冲喂给 gh；没有时不碰 stdin，gh 收到空输入。
#
# 只用于写操作（create、comment、edit、close、api 写入等）。`gh pr checks`、`--watch` 之类
# 长时间流式输出的只读命令不要经它：输出被缓冲到结束才显示。
#
# 不声明 param/CmdletBinding，以免 gh 的 -b、-R 等短选项被当成 PowerShell 公共参数的前缀。
# 写操作若在服务端已生效而响应丢失，重试可能重复一次（如重复评论），重试前如有疑问先查看。
$ErrorActionPreference = 'Continue'
$retryCount = 3
$delay = 2.0
if (-not [string]::IsNullOrWhiteSpace($env:GH_RETRY_DELAY_SECONDS)) {
    $delay = [double]$env:GH_RETRY_DELAY_SECONDS
}
$transientPattern = '(?i)\bEOF\b|connection reset|\bHTTP 5\d\d\b'
# PowerShell 5.1 管道到原生命令默认按 ASCII 编码，中文正文会变成 `?`。
$OutputEncoding = New-Object System.Text.UTF8Encoding($false)

$buffer = $null
if ($MyInvocation.ExpectingInput) {
    $buffer = @($input)
}
elseif (($args -contains '-') -and [Console]::IsInputRedirected) {
    $buffer = @([Console]::In.ReadToEnd())
}

$attempt = 0
while ($true) {
    $attempt++
    if ($null -ne $buffer) {
        $output = @($buffer | & gh @args 2>&1 | ForEach-Object { [string]$_ })
    }
    else {
        $output = @(& gh @args 2>&1 | ForEach-Object { [string]$_ })
    }
    $code = $LASTEXITCODE
    $transient = ($code -ne 0) -and (($output -join "`n") -match $transientPattern)
    if (($code -eq 0) -or -not $transient -or ($attempt -gt $retryCount)) {
        $output
        exit $code
    }
    Write-Warning "gh 遇网络瞬断，$delay 秒后重试（$attempt/$retryCount）：$($output | Select-Object -Last 1)"
    if ($delay -gt 0) { Start-Sleep -Seconds $delay }
}
