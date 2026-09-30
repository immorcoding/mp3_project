# PostToolUse：只对刚编辑的源文件做毫秒级分层 include 检查（HAR-4）；越层时退出码 2，把原因反馈给 Agent。

. (Join-Path -Path $PSScriptRoot -ChildPath 'agent_hook_helpers.ps1')

try {
    Initialize-AgentHookConsole
    $hookInput = Read-AgentHookInput
    if ($null -eq $hookInput) {
        exit 0
    }

    $repositoryRoot = Get-AgentHookRepositoryRoot
    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($path in @(Get-AgentHookEditPath -ToolInput (Get-AgentHookProperty -Object $hookInput -Name 'tool_input'))) {
        $relativePath = ConvertTo-AgentHookRelativePath -RepositoryRoot $repositoryRoot -Path $path
        if ($null -eq $relativePath) {
            continue
        }

        foreach ($violation in @(Get-LayerFileIncludeViolation -RepositoryRoot $repositoryRoot -RelativePath $relativePath)) {
            [void]$violations.Add($violation)
        }
    }

    if ($violations.Count -gt 0) {
        [Console]::Error.WriteLine("分层 include 越界（ARC-1），请改回合规的依赖方向：" + [Environment]::NewLine + ($violations -join [Environment]::NewLine))
        exit 2
    }
    exit 0
}
catch {
    [Console]::Error.WriteLine("check-edited.ps1 自身出错，已跳过：$($_.Exception.Message)")
    exit 0
}
