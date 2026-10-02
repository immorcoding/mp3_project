[CmdletBinding()]
param(
    [ValidateSet('WorkingTree', 'Index')]
    [string]$Snapshot = 'WorkingTree',

    [string[]]$ChangedPath,

    [switch]$AllowGeneratedUpdate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

function Get-NormalizedRepositoryPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$RelativePath
    )

    return (($RelativePath -replace '\\', '/').Trim().TrimStart('/'))
}

function Test-GeneratedWriteUpdateAllowed {
    [CmdletBinding()]
    param(
        [switch]$AllowGeneratedUpdate
    )

    if ($AllowGeneratedUpdate) {
        return $true
    }

    $value = $env:ALLOW_GENERATED_UPDATE
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $false
    }

    return $value.Trim() -match '^(?i:1|true|yes)$'
}

function Test-GeneratedWriteProtectedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$RelativePath
    )

    $normalized = Get-NormalizedRepositoryPath -RelativePath $RelativePath
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return $false
    }

    if ($normalized -eq 'Middlewares/Third_Party/LVGL/lv_conf.h') {
        return $false
    }

    $prefixes = @(
        'Drivers/'
        'Middlewares/ST/'
        'Middlewares/Third_Party/LVGL/'
        'Middlewares/Third_Party/FreeRTOS/Source/'
        'Middlewares/Third_Party/FatFs/'
        'cmake/stm32cubemx/'
    )

    foreach ($prefix in $prefixes) {
        $prefixDirectory = $prefix.TrimEnd('/')
        if ($normalized -eq $prefixDirectory) {
            return $true
        }

        if ($normalized.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Get-GitOutputLines {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$GitPath,

        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [switch]$AllowDifferencesExitCode
    )

    $gitArguments = @(
        '-C', $RepositoryRoot
        '-c', 'core.quotepath=false'
    ) + $Arguments

    $previousErrorAction = $ErrorActionPreference
    $previousNativeErrorAction = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
    $ErrorActionPreference = 'Continue'
    if ($null -ne $previousNativeErrorAction) {
        $PSNativeCommandUseErrorActionPreference = $false
    }

    try {
        $output = & $GitPath @gitArguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
        if ($null -ne $previousNativeErrorAction) {
            $PSNativeCommandUseErrorActionPreference = $previousNativeErrorAction.Value
        }
    }
    if ($null -eq $exitCode) {
        $exitCode = 0
    }

    $allowed = $exitCode -eq 0
    if ($AllowDifferencesExitCode -and ($exitCode -eq 1)) {
        $allowed = $true
    }

    if (-not $allowed) {
        throw "外部命令执行失败：$GitPath，退出码：$exitCode。"
    }

    if ($null -eq $output) {
        return @()
    }

    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($item in @($output)) {
        if ($item -is [System.Management.Automation.ErrorRecord]) {
            continue
        }

        $line = [string]$item
        if ([string]::IsNullOrWhiteSpace($line) -or ($line -match '^(warning|error):')) {
            continue
        }

        [void]$lines.Add($line)
    }

    return @($lines)
}

function Get-GitDirtyRelativePaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    return Get-HarnessWorkingTreeChangedPaths -RepositoryRoot $RepositoryRoot
}

function Invoke-GeneratedWriteCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [ValidateSet('WorkingTree', 'Index')]
        [string]$Snapshot = 'WorkingTree',

        [string[]]$ChangedPath,

        [switch]$AllowGeneratedUpdate
    )

    if (Test-GeneratedWriteUpdateAllowed -AllowGeneratedUpdate:$AllowGeneratedUpdate) {
        Write-NativeUtf8Line -Text '已跳过生成目录写保护（ALLOW_GENERATED_UPDATE）。'
        return
    }

    $changedPaths = if ($PSBoundParameters.ContainsKey('ChangedPath')) {
        Get-HarnessUniquePaths -Path $ChangedPath
    }
    elseif ($Snapshot -eq 'Index') {
        Get-HarnessIndexChangedPaths -RepositoryRoot $RepositoryRoot
    }
    else {
        Get-GitDirtyRelativePaths -RepositoryRoot $RepositoryRoot
    }

    $dirtyPaths = @($changedPaths |
        Where-Object { Test-GeneratedWriteProtectedPath -RelativePath $_ } |
        Sort-Object)

    if ($dirtyPaths.Count -eq 0) {
        $scopeText = if ($Snapshot -eq 'Index') { 'Git 索引快照' } else { '工作树相对 HEAD' }
        Write-NativeUtf8Line -Text "生成目录写保护通过：$scopeText 中的受保护路径无改动。"
        return
    }

    $header = "生成目录写保护失败，共 $($dirtyPaths.Count) 个文件被改动。这些路径只能由 CubeMX 重新导出（或更新 Vendor）后提交，不要手改："
    $body = ($dirtyPaths | ForEach-Object { "  $_" }) -join [Environment]::NewLine
    $hint = @(
        '若这是你本人的重新导出，在本机 PowerShell 当前会话执行：'
        '  $env:ALLOW_GENERATED_UPDATE = ''1'''
        '然后再运行 verify.ps1 或 git commit。不要写入用户或系统环境变量；Git Graph 不会带上该变量。助手禁止设置该变量。'
    ) -join [Environment]::NewLine

    throw ($header + [Environment]::NewLine + $body + [Environment]::NewLine + [Environment]::NewLine + $hint)
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    $entryChangedPathSpecified = $PSBoundParameters.ContainsKey('ChangedPath')
    Complete-Utf8EntryScript -Action {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        $parameters = @{
            RepositoryRoot = $repositoryRoot
            Snapshot = $Snapshot
            AllowGeneratedUpdate = $AllowGeneratedUpdate
        }
        if ($entryChangedPathSpecified) {
            $parameters.ChangedPath = $ChangedPath
        }
        Invoke-GeneratedWriteCheck @parameters
    }
}
