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
        [string]$RequestedHostCompiler,
        [string]$RepositoryRoot = $TreeRoot
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
        Write-NativeUtf8Line -Text 'CHANGED：本次没有需要运行的主机模块测试。'
    }
    else {
        $hostParameters = @{ Module = $Impact.HostModules }
        if (-not [string]::IsNullOrWhiteSpace($RequestedHostCompiler)) {
            $hostParameters.HostCompiler = $RequestedHostCompiler
        }
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $TreeRoot -ChildPath 'scripts/test-host.ps1') `
            -Parameters $hostParameters
    }

    # 旧提交的 Impact 没有该属性；推送快照按其自身规则判定。
    if (($Impact.PSObject.Properties.Name -contains 'RequiresGuiScenarios') -and $Impact.RequiresGuiScenarios) {
        $scenarioScript = Join-Path -Path $TreeRoot -ChildPath 'Tools/gui_simulator/run-scenarios.ps1'
        $scenarioParameters = @{}
        # 推送快照在临时目录从零构建；主仓库已下载的 SDL2 只有在其 SHA256 与快照
        # CMakeLists 固定的值一致时才复用，SDL 升级后自动回到联网下载与校验。
        if ($TreeRoot -ne $RepositoryRoot) {
            $cachedSdl = Join-Path -Path $RepositoryRoot -ChildPath 'build/gui_simulator/_deps/sdl2_mingw-src'
            $cachedStamp = Join-Path -Path $RepositoryRoot -ChildPath 'build/gui_simulator/_deps/sdl2_mingw-subbuild/CMakeLists.txt'
            $snapshotCmake = Join-Path -Path $TreeRoot -ChildPath 'Tools/gui_simulator/CMakeLists.txt'
            $pinned = Select-String -LiteralPath $snapshotCmake -Pattern 'URL_HASH\s+(SHA256=[0-9a-fA-F]+)' |
                Select-Object -First 1
            if (($null -ne $pinned) -and
                (Test-Path -LiteralPath $cachedSdl -PathType Container) -and
                (Test-Path -LiteralPath $cachedStamp -PathType Leaf) -and
                (Select-String -LiteralPath $cachedStamp -Pattern $pinned.Matches[0].Groups[1].Value -SimpleMatch -Quiet)) {
                $scenarioParameters.SdlSourceDir = $cachedSdl
            }
        }
        Write-NativeUtf8Line -Text 'CHANGED：变更命中 GUI 路径，运行模拟器场景回归。'
        Invoke-PowerShellScript -ScriptPath $scenarioScript -Parameters $scenarioParameters
    }
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

        # 领域文件只在写入分支改：按推送目标分支判断；旧提交没有该 checker 时跳过。
        $shapeWriterScript = Join-Path -Path $snapshotRoot -ChildPath 'scripts/shape_writer.ps1'
        if (Test-Path -LiteralPath $shapeWriterScript -PathType Leaf) {
            . $shapeWriterScript
            foreach ($item in $Update) {
                $shapeViolation = Get-ShapePushViolation -RepositoryRoot $RepositoryRoot `
                    -RemoteName $PushRemoteName -Update $item
                if ($null -ne $shapeViolation) {
                    throw $shapeViolation
                }
            }
        }

        . (Join-Path -Path $snapshotRoot -ChildPath 'scripts/check-generated-write.ps1')
        Invoke-GeneratedWriteCheck -RepositoryRoot $snapshotRoot -ChangedPath $changedPaths `
            -AllowGeneratedUpdate:$AllowGenerated
        $impact = Get-HarnessImpact -ChangedPath $changedPaths
        $RequiresHardware.Value = [bool]$impact.RequiresHardware
        Invoke-ChangedValidationInTree -TreeRoot $snapshotRoot -Impact $impact `
            -RequestedHostCompiler $RequestedHostCompiler -RepositoryRoot $RepositoryRoot
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
