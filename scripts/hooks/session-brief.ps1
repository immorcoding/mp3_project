# SessionStart：纯 stdout 在 Claude Code 与 Codex 中都会注入为上下文（WF-5）。
# 只给指针与计数，不给正文；GitHub 查询超时或失败时降级为一行说明。

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8
$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Invoke-BriefCommand {
    param(
        [Parameter(Mandatory)][string]$FileName,
        [Parameter(Mandatory)][string]$Arguments,
        [int]$TimeoutMilliseconds = 5000
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FileName
    $startInfo.Arguments = $Arguments
    $startInfo.WorkingDirectory = $repositoryRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardOutputEncoding = $utf8
    $startInfo.CreateNoWindow = $true

    $process = [System.Diagnostics.Process]::Start($startInfo)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutMilliseconds)) {
        try { $process.Kill() } catch { }
        return $null
    }
    if ($process.ExitCode -ne 0) {
        return $null
    }
    return $stdoutTask.Result
}

$lines = New-Object System.Collections.Generic.List[string]
try {
    $branch = Invoke-BriefCommand -FileName 'git' -Arguments 'rev-parse --abbrev-ref HEAD'
    $status = Invoke-BriefCommand -FileName 'git' -Arguments 'status --porcelain'
    $dirtyCount = if ($null -eq $status) { '?' } else { @($status -split "`n" | Where-Object { $_.Trim() }).Count }
    [void]$lines.Add("[会话简报] 分支 $(([string]$branch).Trim())，未提交文件 $dirtyCount 个。进度与板上状态在 GitHub Issues（见 docs/agents/issue-tracker.md）。")

    $ghQueries = @(
        @{ Label = 'ready-for-agent'; Title = '可交给 Agent' }
        @{ Label = 'hw:pending'; Title = '待上板' }
    )
    foreach ($query in $ghQueries) {
        $json = Invoke-BriefCommand -FileName 'gh' -Arguments "issue list --state open --label $($query.Label) --limit 5 --json number,title"
        if ($null -eq $json) {
            [void]$lines.Add('GitHub 事项未取到（离线、超时或 gh 未登录）；需要时手动 gh issue list。')
            break
        }
        # PowerShell 5.1 把 "[]" 解析为单个空数组对象；经管道展开后再过滤。
        $issues = @($json | ConvertFrom-Json | ForEach-Object { $_ } | Where-Object { $null -ne $_ })
        if ($issues.Count -gt 0) {
            $items = ($issues | ForEach-Object { "#$($_.number) $($_.title)" }) -join '；'
            [void]$lines.Add("$($query.Title)（$($query.Label)）：$items")
        }
    }
}
catch {
    [void]$lines.Add("会话简报生成失败：$($_.Exception.Message)")
}

[Console]::Out.WriteLine(($lines -join [Environment]::NewLine))
exit 0
