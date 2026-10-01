[CmdletBinding()]
param(
    [ValidateSet('WorkingTree', 'Index')]
    [string]$Snapshot = 'WorkingTree'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

function Get-CIncludeDirective {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$Line
    )

    if ($Line -notmatch '^\s*#\s*include\s*([<"])([^>"]+)[>"]') {
        return $null
    }

    return $Matches[2].Trim()
}

function Get-NormalizedIncludePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$IncludePath
    )

    return (($IncludePath -replace '\\', '/').Trim())
}

function Get-LayerIncludeViolation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [ValidateSet('component', 'bridge', 'service', 'platform', 'stm32_hal', 'cortex')]
        [string]$SourceKind,

        [Parameter(Mandatory)]
        [string]$IncludePath
    )

    $normalized = Get-NormalizedIncludePath -IncludePath $IncludePath
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return $null
    }

    $leaf = ($normalized -split '/')[-1]
    $isHalHeader = $leaf -match '(?i)^(main|i2s|sdmmc|usb_device|stm32[^/]*|cmsis[^/]*|usbd_[^/]+)\.h$'

    if ($SourceKind -in @('platform', 'stm32_hal', 'cortex')) {
        $sourceLabel = switch ($SourceKind) {
            'platform' { 'Platform' }
            'stm32_hal' { 'Adapters/stm32_hal' }
            default { 'Adapters/cortex' }
        }

        if ($normalized -cmatch '(^|/)APP(/|$)') {
            return "禁止包含 '$normalized'。$sourceLabel 禁止反向依赖 APP。"
        }

        if ($normalized -match '(?i)(^|/)Service(/|$)') {
            return "禁止包含 '$normalized'。$sourceLabel 禁止反向依赖 Service。"
        }

        return $null
    }

    if ($SourceKind -eq 'service') {
        if ($isHalHeader) {
            return "禁止包含 '$normalized'。Service 不得依赖 HAL、CMSIS 或 CubeMX 外设头；进入硬件应经 Platform。"
        }

        if ($normalized -match '(?i)(^|/)(Core|Drivers|USB_DEVICE)(/|$)' -or $normalized -cmatch '(^|/)APP(/|$)') {
            return "禁止包含 '$normalized'。Service 禁止包含 APP、生成 MCU 目录或 USB Device glue。"
        }

        if ($normalized -match '(?i)(^|/)Adapters/stm32_hal(/|$)') {
            return "禁止包含 '$normalized'。Service 不得直接包含 HAL Adapter；应经 Platform 进入硬件。"
        }

        return $null
    }

    $sourceLabel = if ($SourceKind -eq 'bridge') { 'Adapters/bridge' } else { 'Components' }

    if ($normalized -match '(?i)FreeRTOS') {
        return "禁止包含 '$normalized'。$sourceLabel 不得把 FreeRTOS 作为硬依赖。"
    }

    if ($isHalHeader) {
        return "禁止包含 '$normalized'。$sourceLabel 不得依赖 HAL、CMSIS 或 CubeMX 外设头；把 MCU 访问放到 Adapters/stm32_hal，由 Platform 装配。"
    }

    if ($normalized -match '(?i)(^|/)(Platform|Service|APP|Core|Drivers|FATFS|USB_DEVICE|GUI|Middlewares)(/|$)') {
        return "禁止包含 '$normalized'。$sourceLabel 禁止反向依赖上层、生成目录或中间件。"
    }

    if ($SourceKind -eq 'component' -and ($normalized -match '(?i)(^|/)Adapters(/|$)')) {
        return "禁止包含 '$normalized'。Components 不得包含 Adapter 头；Ops 由 Component 拥有，Adapter 只负责实现。"
    }

    if ($SourceKind -eq 'bridge' -and ($normalized -match '(?i)(^|/)Adapters(/|$)') -and ($normalized -notmatch '(?i)(^|/)Adapters/bridge(/|$)')) {
        return "禁止包含 '$normalized'。Adapters/bridge 只连接两个 Component Interface，不得引入 HAL Adapter。"
    }

    return $null
}

function Get-RelativeRepositoryPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$FullPath
    )

    $rootPrefix = (Get-NormalizedIncludePath -IncludePath $RepositoryRoot).TrimEnd('/') + '/'
    $normalizedFullPath = Get-NormalizedIncludePath -IncludePath $FullPath
    if ($normalizedFullPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $normalizedFullPath.Substring($rootPrefix.Length)
    }

    return $normalizedFullPath
}

# 扫描根与层级类型的唯一映射；全量扫描与 Agent Hook 的单文件检查共用。
$script:LayerScanRootTable = @(
    @{ Prefix = 'Components/'; Kind = 'component' }
    @{ Prefix = 'Adapters/bridge/'; Kind = 'bridge' }
    @{ Prefix = 'Adapters/stm32_hal/'; Kind = 'stm32_hal' }
    @{ Prefix = 'Adapters/cortex/'; Kind = 'cortex' }
    @{ Prefix = 'Platform/'; Kind = 'platform' }
    @{ Prefix = 'Service/'; Kind = 'service' }
)

# 非 .c/.h 或不在扫描根内时返回 $null。
function Get-LayerSourceKind {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$RelativePath
    )

    $normalized = (Get-NormalizedIncludePath -IncludePath $RelativePath).TrimStart('/')
    if ($normalized -notmatch '(?i)\.[ch]$') {
        return $null
    }

    foreach ($entry in $script:LayerScanRootTable) {
        if ($normalized.StartsWith($entry.Prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $entry.Kind
        }
    }

    return $null
}

# 单文件越层检查，供编辑后 Agent Hook 调用；返回 `path:line: 原因` 数组。
function Get-LayerFileIncludeViolation {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [Parameter(Mandatory)]
        [string]$RelativePath
    )

    $kind = Get-LayerSourceKind -RelativePath $RelativePath
    $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $RelativePath
    if (($null -eq $kind) -or -not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return @()
    }

    $normalized = (Get-NormalizedIncludePath -IncludePath $RelativePath).TrimStart('/')
    $violations = New-Object System.Collections.Generic.List[string]
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $fullPath) {
        $lineNumber += 1
        $includePath = Get-CIncludeDirective -Line $line
        if ([string]::IsNullOrWhiteSpace($includePath)) {
            continue
        }

        $violation = Get-LayerIncludeViolation -SourceKind $kind -IncludePath $includePath
        if (-not [string]::IsNullOrWhiteSpace($violation)) {
            [void]$violations.Add("${normalized}:${lineNumber}: $violation")
        }
    }

    return $violations.ToArray()
}

function Invoke-LayerIndexIncludeCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $snapshotRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
        -ChildPath ('layer-index-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $snapshotRoot -Force | Out-Null
        $git = Get-ExternalCommand -Name 'git'
        $prefix = (($snapshotRoot -replace '\\', '/').TrimEnd('/')) + '/'
        Invoke-ExternalCommand -CommandPath $git -Arguments @(
            '-C', $RepositoryRoot,
            'checkout-index', '--all', '--force', "--prefix=$prefix"
        )
        Invoke-LayerIncludeCheck -RepositoryRoot $snapshotRoot
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

function Invoke-LayerIncludeCheck {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$RepositoryRoot
    )

    $scanRoots = @($script:LayerScanRootTable | ForEach-Object {
        @{ Path = Join-Path -Path $RepositoryRoot -ChildPath $_.Prefix.TrimEnd('/'); Kind = $_.Kind }
    })

    $violations = New-Object System.Collections.Generic.List[string]
    $scannedFileCount = 0

    foreach ($scanRoot in $scanRoots) {
        if (-not (Test-Path -LiteralPath $scanRoot.Path -PathType Container)) {
            throw "分层检查缺少扫描目录：$($scanRoot.Path)"
        }

        $sourceFiles = @(Get-ChildItem -LiteralPath $scanRoot.Path -Recurse -File |
            Where-Object { $_.Extension -in @('.c', '.h') })

        foreach ($sourceFile in $sourceFiles) {
            $scannedFileCount += 1
            $relativePath = Get-RelativeRepositoryPath -RepositoryRoot $RepositoryRoot -FullPath $sourceFile.FullName
            $lineNumber = 0
            foreach ($line in Get-Content -LiteralPath $sourceFile.FullName) {
                $lineNumber += 1
                $includePath = Get-CIncludeDirective -Line $line
                if ([string]::IsNullOrWhiteSpace($includePath)) {
                    continue
                }

                $violation = Get-LayerIncludeViolation -SourceKind $scanRoot.Kind -IncludePath $includePath
                if ([string]::IsNullOrWhiteSpace($violation)) {
                    continue
                }

                [void]$violations.Add("${relativePath}:${lineNumber}: $violation")
            }
        }
    }

    if ($violations.Count -gt 0) {
        $header = "分层 include 检查失败，共 $($violations.Count) 处："
        throw ($header + [Environment]::NewLine + ($violations -join [Environment]::NewLine))
    }

    Write-NativeUtf8Line -Text "分层 include 检查通过：已扫描 $scannedFileCount 个文件。"
}

$isDotSourced = $MyInvocation.InvocationName -eq '.'
if (-not $isDotSourced) {
    Complete-Utf8EntryScript -Action {
        $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
        if ($Snapshot -eq 'Index') {
            Invoke-LayerIndexIncludeCheck -RepositoryRoot $repositoryRoot
        }
        else {
            Invoke-LayerIncludeCheck -RepositoryRoot $repositoryRoot
        }
    }
}
