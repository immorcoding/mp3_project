# 用 gh 的完整参数调用：`./scripts/gh-retry.ps1 issue comment 7 --body-file note.md`。
# 失败且输出含 EOF、connection reset 或 HTTP 5xx 时间隔重试，最多 3 次（不含首次）；其余错误原样返回。
# 间隔默认 2 秒，可用环境变量 GH_RETRY_DELAY_SECONDS 覆盖。不声明 param/CmdletBinding，
# 以免 gh 的 -b、-R 等短选项被当成 PowerShell 公共参数的前缀。写操作若在服务端已生效而响应丢失，
# 重试可能重复一次（如重复评论），重试前如有疑问先查看。
$ErrorActionPreference = 'Continue'
$retryCount = 3
$delay = 2.0
if (-not [string]::IsNullOrWhiteSpace($env:GH_RETRY_DELAY_SECONDS)) {
    $delay = [double]$env:GH_RETRY_DELAY_SECONDS
}
$transientPattern = '(?i)\bEOF\b|connection reset|\bHTTP 5\d\d\b'

$attempt = 0
while ($true) {
    $attempt++
    $output = @(& gh @args 2>&1 | ForEach-Object { [string]$_ })
    $code = $LASTEXITCODE
    $transient = ($code -ne 0) -and (($output -join "`n") -match $transientPattern)
    if (($code -eq 0) -or -not $transient -or ($attempt -gt $retryCount)) {
        $output
        exit $code
    }
    Write-Warning "gh 遇网络瞬断，$delay 秒后重试（$attempt/$retryCount）：$($output | Select-Object -Last 1)"
    if ($delay -gt 0) { Start-Sleep -Seconds $delay }
}
