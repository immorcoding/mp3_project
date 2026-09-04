[CmdletBinding()]
param(
    [switch]$Push,
    [string]$RemoteName,
    [string]$RemoteLocation,
    [string]$BaseRef,
    [string]$HeadRef = 'HEAD',
    [string[]]$ChangedPath,
    [string]$HostCompiler,
    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

function Invoke-ChangedValidationInTree {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$TreeRoot,
        [Parameter(Mandatory)]$Impact,
        [string]$RequestedHostCompiler
    )

    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $TreeRoot -ChildPath 'scripts/check_fast.ps1') `
        -Parameters @{
            Snapshot = 'WorkingTree'
            SuppressStatus = $true
        }

    if ($Impact.UnknownPaths.Count -gt 0) {
        Write-NativeUtf8Line -Text ("以下路径没有精确模块映射，已保守运行全部主机测试：" +
            [Environment]::NewLine + (($Impact.UnknownPaths | ForEach-Object { "  $_" }) -join [Environment]::NewLine))
    }

    if ($Impact.HostModules.Count -eq 0) {
        Write-NativeUtf8Line -Text 'CHANGED：本次只有文档或 Agent 配置变化，不需要主机模块测试。'
        return
    }

    $hostParameters = @{ Module = $Impact.HostModules }
    if (-not [string]::IsNullOrWhiteSpace($RequestedHostCompiler)) {
        $hostParameters.HostCompiler = $RequestedHostCompiler
    }
    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $TreeRoot -ChildPath 'scripts/test-host.ps1') `
        -Parameters $hostParameters
}

function Invoke-PushedCommitValidation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$Commit,
        [Parameter(Mandatory)][object[]]$Update,
        [Parameter(Mandatory)][string]$PushRemoteName,
        [string]$RequestedHostCompiler,
        [switch]$AllowGenerated,
        [Parameter(Mandatory)][ref]$RequiresHardware
    )

    $snapshotRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
        -ChildPath ('mp3-push-' + [Guid]::NewGuid().ToString('N'))
    $worktreeAdded = $false
    try {
        $git = Get-ExternalCommand -Name 'git'
        Invoke-ExternalCommand -CommandPath $git -Arguments @(
            '-C', $RepositoryRoot,
            'worktree', 'add', '--detach', '--quiet', $snapshotRoot, $Commit
        )
        $worktreeAdded = $true

        # 从待推送提交加载规则与 checker，当前工作树不能决定测试选择或硬件分类。
        . (Join-Path -Path $snapshotRoot -ChildPath 'scripts/harness_helpers.ps1')
        $paths = New-Object System.Collections.Generic.List[string]
        foreach ($item in $Update) {
            $itemPaths = @(Get-HarnessPushChangedPaths -RepositoryRoot $RepositoryRoot `
                -RemoteName $PushRemoteName -Update $item)
            foreach ($path in $itemPaths) {
                [void]$paths.Add($path)
            }
        }
        $changedPaths = @(Get-HarnessUniquePaths -Path $paths)

        . (Join-Path -Path $snapshotRoot -ChildPath 'scripts/check-generated-write.ps1')
        Invoke-GeneratedWriteCheck -RepositoryRoot $snapshotRoot -ChangedPath $changedPaths `
            -AllowGeneratedUpdate:$AllowGenerated
        $impact = Get-HarnessImpact -ChangedPath $changedPaths
        $RequiresHardware.Value = [bool]$impact.RequiresHardware
        Invoke-ChangedValidationInTree -TreeRoot $snapshotRoot -Impact $impact `
            -RequestedHostCompiler $RequestedHostCompiler
    }
    finally {
        if ($worktreeAdded) {
            $resolvedSnapshot = (Resolve-Path -LiteralPath $snapshotRoot).Path
            $resolvedTemp = (Resolve-Path -LiteralPath ([System.IO.Path]::GetTempPath())).Path.TrimEnd('\')
            if (-not $resolvedSnapshot.StartsWith($resolvedTemp + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "拒绝清理临时推送快照之外的路径：$resolvedSnapshot"
            }

            $git = Get-ExternalCommand -Name 'git'
            Invoke-ExternalCommand -CommandPath $git -Arguments @(
                '-C', $RepositoryRoot,
                'worktree', 'remove', '--force', $resolvedSnapshot
            )
        }
    }
}

$changedPathSpecified = $PSBoundParameters.ContainsKey('ChangedPath')

Complete-Utf8EntryScript -Action {
    try {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        $generatedScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-generated-write.ps1'
        $requiresHardware = $false

        if ($Push) {
            if ([string]::IsNullOrWhiteSpace($RemoteName)) {
                throw 'pre-push 模式缺少远端名称。'
            }

            $updates = @(ConvertFrom-HarnessPushInput -InputText ([Console]::In.ReadToEnd()))
            $commitUpdates = @{}
            foreach ($update in $updates) {
                if ($update.LocalSha -match '^0+$') {
                    continue
                }

                if (-not $commitUpdates.ContainsKey($update.LocalSha)) {
                    $commitUpdates[$update.LocalSha] = @()
                }
                $commitUpdates[$update.LocalSha] = @($commitUpdates[$update.LocalSha] + $update)
            }

            foreach ($commit in @($commitUpdates.Keys | Sort-Object)) {
                $commitRequiresHardware = $false
                Invoke-PushedCommitValidation -RepositoryRoot $repositoryRoot -Commit $commit `
                    -Update $commitUpdates[$commit] -PushRemoteName $RemoteName `
                    -RequestedHostCompiler $HostCompiler -AllowGenerated:$AllowGeneratedUpdate `
                    -RequiresHardware ([ref]$commitRequiresHardware)
                $requiresHardware = $requiresHardware -or $commitRequiresHardware
            }
        }
        else {
            if ($changedPathSpecified) {
                $paths = @(Get-HarnessUniquePaths -Path $ChangedPath)
            }
            elseif (-not [string]::IsNullOrWhiteSpace($BaseRef)) {
                $paths = @(Get-HarnessChangedPathsBetween -RepositoryRoot $repositoryRoot `
                    -BaseRef $BaseRef -HeadRef $HeadRef)
            }
            else {
                $paths = @(Get-HarnessWorkingTreeChangedPaths -RepositoryRoot $repositoryRoot)
            }

            Invoke-PowerShellScript -ScriptPath $generatedScript -Parameters @{
                ChangedPath = $paths
                AllowGeneratedUpdate = $AllowGeneratedUpdate
            }
            $impact = Get-HarnessImpact -ChangedPath $paths
            $requiresHardware = $impact.RequiresHardware
            Invoke-ChangedValidationInTree -TreeRoot $repositoryRoot -Impact $impact `
                -RequestedHostCompiler $HostCompiler
        }

        if ($requiresHardware) {
            Write-HarnessStatus -Status NEEDS_HARDWARE_VALIDATION `
                -Summary 'CHANGED 主机验证通过，但变更命中 MCU/板级敏感路径；允许推送，完成声明前仍需上板证据。'
        }
        else {
            Write-HarnessStatus -Status PASS `
                -Summary 'CHANGED 通过：已按变更路径运行确定性快检与受影响主机测试。'
        }
    }
    catch {
        Write-HarnessStatus -Status FAIL -Summary "CHANGED 失败：$($_.Exception.Message)"
        throw
    }
}
