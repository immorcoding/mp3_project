Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

function Get-HarnessRules {
    [CmdletBinding()]
    param()

    $rulesPath = Join-Path -Path $PSScriptRoot -ChildPath 'rules/verification.psd1'
    if (-not (Test-Path -LiteralPath $rulesPath -PathType Leaf)) {
        throw "缺少验证规则：$rulesPath"
    }

    $rulesText = [System.IO.File]::ReadAllText($rulesPath)
    $rules = & ([scriptblock]::Create($rulesText))
    if ($rules -isnot [hashtable]) {
        throw "验证规则必须返回 Hashtable：$rulesPath"
    }

    return $rules
}

function Get-HarnessNormalizedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$RelativePath
    )

    return (($RelativePath -replace '\\', '/').Trim().TrimStart('/'))
}

function Get-HarnessGitOutputLines {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [int[]]$AllowedExitCodes = @(0)
    )

    $git = Get-ExternalCommand -Name 'git'
    $gitArguments = @('-C', $RepositoryRoot, '-c', 'core.quotepath=false') + $Arguments
    $previousErrorActionPreference = $ErrorActionPreference
    $previousNativePreference = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
    try {
        $ErrorActionPreference = 'Continue'
        if ($null -ne $previousNativePreference) {
            $PSNativeCommandUseErrorActionPreference = $false
        }

        $output = & $git @gitArguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
        if ($null -ne $previousNativePreference) {
            $PSNativeCommandUseErrorActionPreference = $previousNativePreference.Value
        }
    }

    if ($null -eq $exitCode) {
        $exitCode = 0
    }
    if ($exitCode -notin $AllowedExitCodes) {
        throw "Git 命令失败，退出码：$exitCode；参数：$($Arguments -join ' ')"
    }

    return @($output | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-HarnessUniquePaths {
    [CmdletBinding()]
    param(
        [string[]]$Path
    )

    $uniquePaths = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($item in @($Path)) {
        $normalized = Get-HarnessNormalizedPath -RelativePath $item
        if (-not [string]::IsNullOrWhiteSpace($normalized)) {
            [void]$uniquePaths.Add($normalized)
        }
    }

    return @($uniquePaths | Sort-Object)
}

function Get-HarnessIndexChangedPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $paths = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
        'diff', '--cached', '--name-only', '--diff-filter=ACMRD'
    )
    return Get-HarnessUniquePaths -Path $paths
}

function Get-HarnessWorkingTreeChangedPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $tracked = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
        'diff', '--name-only', 'HEAD'
    ) -AllowedExitCodes @(0, 1)
    $untracked = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
        'ls-files', '--others', '--exclude-standard'
    )
    return Get-HarnessUniquePaths -Path @($tracked + $untracked)
}

function Get-HarnessChangedPathsBetween {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$BaseRef,

        [Parameter(Mandatory)]
        [string]$HeadRef
    )

    $paths = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
        'diff', '--name-only', '--diff-filter=ACMRD', $BaseRef, $HeadRef, '--'
    )
    return Get-HarnessUniquePaths -Path $paths
}

function ConvertFrom-HarnessPushInput {
    [CmdletBinding()]
    param(
        [AllowEmptyString()]
        [string]$InputText
    )

    $updates = New-Object System.Collections.Generic.List[object]
    foreach ($line in @($InputText -split "`r?`n")) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        $fields = @($line.Trim() -split '\s+')
        if ($fields.Count -ne 4) {
            throw "无法解析 pre-push 输入行：$line"
        }
        if (($fields[1] -notmatch '^[0-9a-fA-F]{40,64}$') -or
            ($fields[3] -notmatch '^[0-9a-fA-F]{40,64}$')) {
            throw "pre-push 输入包含无效对象 ID：$line"
        }

        [void]$updates.Add([pscustomobject]@{
            LocalRef = $fields[0]
            LocalSha = $fields[1].ToLowerInvariant()
            RemoteRef = $fields[2]
            RemoteSha = $fields[3].ToLowerInvariant()
        })
    }

    return @($updates | ForEach-Object { $_ })
}

function Get-HarnessPushChangedPaths {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$RemoteName,

        [Parameter(Mandatory)]
        $Update
    )

    $zeroObjectIdPattern = '^0+$'
    if ($Update.LocalSha -match $zeroObjectIdPattern) {
        return @()
    }

    if ($Update.RemoteSha -notmatch $zeroObjectIdPattern) {
        return Get-HarnessChangedPathsBetween -RepositoryRoot $RepositoryRoot `
            -BaseRef $Update.RemoteSha -HeadRef $Update.LocalSha
    }

    $commits = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
        'rev-list', $Update.LocalSha, '--not', "--remotes=$RemoteName"
    )
    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($commit in $commits) {
        $commitPaths = Get-HarnessGitOutputLines -RepositoryRoot $RepositoryRoot -Arguments @(
            'diff-tree', '--root', '--no-commit-id', '--name-only', '-r',
            '--diff-filter=ACMRD', $commit, '--'
        )
        foreach ($path in $commitPaths) {
            [void]$paths.Add($path)
        }
    }

    return Get-HarnessUniquePaths -Path $paths
}

function Test-HarnessPathMatchesAny {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RelativePath,

        [string[]]$Pattern
    )

    $normalized = Get-HarnessNormalizedPath -RelativePath $RelativePath
    foreach ($item in @($Pattern)) {
        if ($normalized -match $item) {
            return $true
        }
    }

    return $false
}

function Get-HarnessImpact {
    [CmdletBinding()]
    param(
        [string[]]$ChangedPath,

        [hashtable]$Rules = (Get-HarnessRules)
    )

    $paths = @(Get-HarnessUniquePaths -Path $ChangedPath)
    $modules = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)
    $unknownPaths = New-Object System.Collections.Generic.List[string]
    $requiresAllHostTests = $false
    $requiresHardware = $false
    $requiresGuiScenarios = $false
    # 旧提交的规则可能没有该键；推送快照按其自身规则判定。
    $guiScenarioPatterns = @(if ($Rules.ContainsKey('GuiScenarioPathPatterns')) { $Rules.GuiScenarioPathPatterns })

    foreach ($path in $paths) {
        if (($guiScenarioPatterns.Count -gt 0) -and
            (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $guiScenarioPatterns)) {
            $requiresGuiScenarios = $true
        }

        if (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $Rules.NoHostTestPathPatterns) {
            continue
        }

        if (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $Rules.HardwareSensitivePathPatterns) {
            $requiresHardware = $true
        }

        if (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $Rules.FullHostTestPathPatterns) {
            $requiresAllHostTests = $true
            continue
        }

        $matched = $false
        foreach ($moduleRule in $Rules.HostModuleRules) {
            if (Test-HarnessPathMatchesAny -RelativePath $path -Pattern $moduleRule.PathPatterns) {
                [void]$modules.Add([string]$moduleRule.Name)
                $matched = $true
            }
        }

        if (-not $matched) {
            [void]$unknownPaths.Add($path)
            $requiresAllHostTests = $true
        }
    }

    if ($requiresAllHostTests) {
        $modules.Clear()
        foreach ($module in $Rules.AllHostModules) {
            [void]$modules.Add([string]$module)
        }
    }

    return [pscustomobject]@{
        ChangedPaths = $paths
        HostModules = @($modules | Sort-Object)
        UnknownPaths = @($unknownPaths | Sort-Object)
        RequiresAllHostTests = $requiresAllHostTests
        RequiresHardware = $requiresHardware
        RequiresGuiScenarios = $requiresGuiScenarios
    }
}

function Write-HarnessStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [ValidateSet('PASS', 'PASS_HOST_ONLY', 'NEEDS_HARDWARE_VALIDATION', 'FAIL')]
        [string]$Status,

        [Parameter(Mandatory)]
        [string]$Summary
    )

    Write-NativeUtf8Line -Text "HARNESS_STATUS=$Status"
    Write-NativeUtf8Line -Text $Summary
}
