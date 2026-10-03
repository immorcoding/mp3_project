[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('edit', 'shell')]
    [string]$Mode
)

# PreToolUse：Claude Code 与 Codex 共用。拒绝 = 退出码 2 + stderr；需确认 = stdout JSON ask（仅 Claude）。
# 自身异常时放行并告警：最终闸门仍是 Git pre-commit / pre-push。

. (Join-Path -Path $PSScriptRoot -ChildPath 'agent_hook_helpers.ps1')

try {
    Initialize-AgentHookConsole
    $hookInput = Read-AgentHookInput
    if ($null -eq $hookInput) {
        exit 0
    }

    $toolName = [string](Get-AgentHookProperty -Object $hookInput -Name 'tool_name')
    $toolInput = Get-AgentHookProperty -Object $hookInput -Name 'tool_input'

    if ($Mode -eq 'shell') {
        $violation = Get-AgentHookShellViolation -Command ([string](Get-AgentHookProperty -Object $toolInput -Name 'command'))
        if ($null -ne $violation) {
            [Console]::Error.WriteLine($violation)
            exit 2
        }
        exit 0
    }

    $repositoryRoot = Get-AgentHookRepositoryRoot
    $baseDirectory = [string](Get-AgentHookProperty -Object $hookInput -Name 'cwd')
    $askReasons = New-Object System.Collections.Generic.List[string]
    # 只有命中领域文件时才查分支，普通编辑不多花 git 调用。
    $currentBranch = $null
    $writerBranch = $null
    $shapeBranchResolved = $false
    foreach ($path in @(Get-AgentHookEditPath -ToolInput $toolInput)) {
        $relativePath = ConvertTo-AgentHookRelativePath -RepositoryRoot $repositoryRoot -Path $path -BaseDirectory $baseDirectory
        if ($null -eq $relativePath) {
            continue
        }

        if ((-not $shapeBranchResolved) -and (Test-ShapeAreaPath -RelativePath $relativePath)) {
            $currentBranch = Get-ShapeCurrentBranch -RepositoryRoot $repositoryRoot
            $writerBranch = Get-ShapeWriterBranch -RepositoryRoot $repositoryRoot
            $shapeBranchResolved = $true
        }
        $decision = Get-AgentHookEditDecision -RepositoryRoot $repositoryRoot -RelativePath $relativePath -ToolName $toolName `
            -CurrentBranch $currentBranch -WriterBranch $writerBranch
        if ($decision.Decision -eq 'deny') {
            [Console]::Error.WriteLine($decision.Reason)
            exit 2
        }
        if ($decision.Decision -eq 'ask') {
            [void]$askReasons.Add($decision.Reason)
        }
    }

    if ($askReasons.Count -gt 0) {
        $output = @{
            hookSpecificOutput = @{
                hookEventName = 'PreToolUse'
                permissionDecision = 'ask'
                permissionDecisionReason = ($askReasons -join ' ')
            }
        }
        [Console]::Out.WriteLine(($output | ConvertTo-Json -Depth 4 -Compress))
    }
    exit 0
}
catch {
    [Console]::Error.WriteLine("guard.ps1 自身出错，已放行：$($_.Exception.Message)")
    exit 0
}
