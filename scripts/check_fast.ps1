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
    )
    foreach ($selfTest in $selfTests) {
        Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath $selfTest)
    }

    Invoke-PowerShellScript -ScriptPath (Join-Path -Path $PSScriptRoot -ChildPath 'check-layer-includes.ps1') `
        -Parameters @{ Snapshot = 'WorkingTree' }
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
                -Summary "FAST 通过：已验证 Harness 自测、分层 include 与生成目录写保护（$Snapshot）。"
        }
    }
    catch {
        if (-not $SuppressStatus) {
            Write-HarnessStatus -Status FAIL -Summary "FAST 失败：$($_.Exception.Message)"
        }
        throw
    }
}
