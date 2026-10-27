[CmdletBinding()]
param(
    [ValidateSet('WorkingTree', 'Index')]
    [string]$Snapshot = 'WorkingTree',

    [switch]$SuppressStatus,

    [string[]]$IndexChangedPath,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')
. (Join-Path -Path $PSScriptRoot -ChildPath 'shape_writer.ps1')

function Invoke-FastWorkingTreeCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [string[]]$ExplicitChangedPath,
        [switch]$UseExplicitChangedPath,
        [switch]$AllowGenerated
    )

    $selfTests = @(
        'harness_helpers.Tests.ps1'
        'check-layer-includes.Tests.ps1'
        'check-generated-write.Tests.ps1'
        'hooks/agent_hooks.Tests.ps1'
        'sync-agent-config.Tests.ps1'
        'shape_writer.Tests.ps1'
        'check-docs.Tests.ps1'
        'check-doxygen.Tests.ps1'
        'check-test-paths.Tests.ps1'
        'capture-verification.Tests.ps1'
        'capture-verification.MergedStderr.Tests.ps1'
        'build_helpers.Tests.ps1'
        'merge-into-integration.Tests.ps1'
        'check-ps1-encoding.Tests.ps1'
    )
    # 自测成功时静默（夹具仓库、证据目录等噪声不进入工具结果），失败时输出完整细节。
    foreach ($selfTest in $selfTests) {
        Invoke-HarnessQuietStep -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath $selfTest)
    }
    Write-Output "Harness 自测通过：$($selfTests.Count) 项。"

    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'sync-agent-config.ps1') `
        -Parameters @{ Check = $true }

    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-layer-includes.ps1') `
        -Parameters @{ Snapshot = 'WorkingTree' }
    Invoke-PowerShellScript -ScriptPath (Join-Path $PSScriptRoot 'check-docs.ps1')
    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-ps1-encoding.ps1')

    # 主机测试 PathPatterns 与各模块真实编译依赖的漂移检查；只读仓库文件，索引快照里同样适用，故不受快照分支限制。
    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-test-paths.ps1')

    # Doxygen 检查要读 Git 差异；索引快照目录不是仓库，暂存内容已由 Invoke-FastIndexCheck 在真实仓库检查过。
    if ($env:MP3_HARNESS_INDEX_SNAPSHOT -ne '1') {
        $doxygenParameters = @{ Snapshot = 'WorkingTree' }
        if ($UseExplicitChangedPath) {
            $doxygenParameters.ChangedPath = $ExplicitChangedPath
        }
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-doxygen.ps1') `
            -Parameters $doxygenParameters
    }

    $generatedParameters = @{
        Snapshot = 'WorkingTree'
        AllowGeneratedUpdate = $AllowGenerated
    }
    if ($UseExplicitChangedPath) {
        $generatedParameters.ChangedPath = $ExplicitChangedPath
    }
    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-generated-write.ps1') `
        -Parameters $generatedParameters
}

function Invoke-FastIndexCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [switch]$AllowGenerated
    )

    $snapshotRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
        -ChildPath ('mp3-index-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $snapshotRoot -Force | Out-Null
        $git = Get-ExternalCommand -Name 'git'
        $prefix = (($snapshotRoot -replace '\\', '/').TrimEnd('/')) + '/'
        Invoke-ExternalCommand -CommandPath $git -Arguments @(
            '-C', $RepositoryRoot,
            'checkout-index', '--all', '--force', "--prefix=$prefix"
        )

        $changedPaths = @(Get-HarnessIndexChangedPaths -RepositoryRoot $RepositoryRoot)
        # 分支与 MERGE_HEAD 只能在真实仓库判断，索引快照目录不是 Git 仓库。
        Invoke-ShapeIndexCheck -RepositoryRoot $RepositoryRoot
        # STY-2 机械项：只看暂存区新增/修改的函数，存量不报。
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-doxygen.ps1') `
            -Parameters @{ Snapshot = 'Index' }
        $previousIndexSnapshot = $env:MP3_HARNESS_INDEX_SNAPSHOT
        try {
            $env:MP3_HARNESS_INDEX_SNAPSHOT = '1'
            Invoke-PowerShellScript -ScriptPath (Join-Path -Path $snapshotRoot -ChildPath 'scripts/check_fast.ps1') `
                -Parameters @{
                    Snapshot = 'WorkingTree'
                    SuppressStatus = $true
                    IndexChangedPath = $changedPaths
                    AllowGeneratedUpdate = $AllowGenerated
                }
        }
        finally {
            if ($null -eq $previousIndexSnapshot) {
                Remove-Item -Path Env:MP3_HARNESS_INDEX_SNAPSHOT -ErrorAction SilentlyContinue
            }
            else {
                $env:MP3_HARNESS_INDEX_SNAPSHOT = $previousIndexSnapshot
            }
        }
    }
    finally {
        if (Test-Path -LiteralPath $snapshotRoot) {
            $resolvedSnapshot = (Resolve-Path -LiteralPath $snapshotRoot).Path
            $resolvedTemp = (Resolve-Path -LiteralPath ([System.IO.Path]::GetTempPath())).Path.TrimEnd('\')
            if (-not $resolvedSnapshot.StartsWith($resolvedTemp + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "拒绝清理临时索引快照之外的路径：$resolvedSnapshot"
            }
            Remove-Item -LiteralPath $resolvedSnapshot -Recurse -Force
        }
    }
}

$indexChangedPathSpecified = $PSBoundParameters.ContainsKey('IndexChangedPath')

Complete-Utf8EntryScript -Action {
    try {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        if ($Snapshot -eq 'Index') {
            Invoke-FastIndexCheck -RepositoryRoot $repositoryRoot -AllowGenerated:$AllowGeneratedUpdate
        }
        else {
            Invoke-FastWorkingTreeCheck -RepositoryRoot $repositoryRoot `
                -ExplicitChangedPath $IndexChangedPath `
                -UseExplicitChangedPath:$indexChangedPathSpecified `
                -AllowGenerated:$AllowGeneratedUpdate
        }

        if (-not $SuppressStatus) {
            Write-HarnessStatus -Status PASS `
                -Summary "FAST 通过：已验证 Harness 自测、文档与 shape、PowerShell 编码、改动函数的 Doxygen（STY-2）、分层 include、主机测试路径覆盖与生成目录写保护（$Snapshot）。"
        }
    }
    catch {
        if (-not $SuppressStatus) {
            Write-HarnessStatus -Status FAIL -Summary "FAST 失败：$($_.Exception.Message)"
        }
        throw
    }
}
