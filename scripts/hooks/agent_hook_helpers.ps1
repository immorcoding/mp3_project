Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Claude Code 与 Codex 共用的 Agent Hook 判定（HAR-3/HAR-4）。受保护路径复用 Git Hook 的同一份规则。
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'check-generated-write.ps1')
. (Join-Path -Path (Split-Path -Parent $PSScriptRoot) -ChildPath 'check-layer-includes.ps1')

# Codex 的 apply_patch 不支持 permissionDecision=ask（会放行），需要确认的写入一律拒绝。
$script:AgentHookAskUnsupportedTools = @('apply_patch')

$script:AgentHookCubeMxSourcePatterns = @(
    '(?i)\.ioc$'
    '(?i)\.ld$'
    '(?i)(^|/)startup_[^/]*\.s$'
    '(?i)^\.mxproject$'
)

$script:AgentHookGeneratorConfigHeaders = @(
    'Middlewares/Third_Party/LVGL/lv_conf.h'
    'FreeRTOSConfig.h'
    'ffconf.h'
    'stm32h7xx_hal_conf.h'
)

$script:AgentHookUserCodeRoots = @('Core/', 'FATFS/', 'USB_DEVICE/')

function Get-AgentHookProperty {
    param(
        $Object,
        [Parameter(Mandatory)][string]$Name
    )

    if ($null -eq $Object) {
        return $null
    }

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }

    return $property.Value
}

# Claude 的 Edit/Write/MultiEdit 给 file_path，NotebookEdit 给 notebook_path；Codex 的 apply_patch 给补丁全文。
function Get-AgentHookEditPath {
    param(
        $ToolInput
    )

    $paths = New-Object System.Collections.Generic.List[string]
    foreach ($name in @('file_path', 'notebook_path')) {
        $value = Get-AgentHookProperty -Object $ToolInput -Name $name
        if (-not [string]::IsNullOrWhiteSpace([string]$value)) {
            [void]$paths.Add([string]$value)
        }
    }

    $patch = Get-AgentHookProperty -Object $ToolInput -Name 'command'
    if (-not [string]::IsNullOrWhiteSpace([string]$patch)) {
        $pattern = '(?m)^\*\*\* (?:(?:Add|Update|Delete) File|Move to): (.+?)\s*$'
        foreach ($match in [regex]::Matches([string]$patch, $pattern)) {
            [void]$paths.Add($match.Groups[1].Value)
        }
    }

    return $paths.ToArray()
}

# 仓库外路径返回 $null，不归本仓库 Hook 管。
function ConvertTo-AgentHookRelativePath {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$Path
    )

    $normalized = ($Path -replace '\\', '/').Trim()
    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        return $normalized.TrimStart('/') -replace '^\./', ''
    }

    $rootPrefix = (($RepositoryRoot -replace '\\', '/').TrimEnd('/')) + '/'
    if ($normalized.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $normalized.Substring($rootPrefix.Length)
    }

    return $null
}

function Test-AgentHookUserCodeFile {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RelativePath
    )

    if ($RelativePath -match '(?i)^FATFS/Target/bsp_driver_user_diskio\.') {
        return $false
    }

    $underRoot = $false
    foreach ($root in $script:AgentHookUserCodeRoots) {
        if ($RelativePath.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
            $underRoot = $true
            break
        }
    }
    if (-not $underRoot) {
        return $false
    }

    $fullPath = Join-Path -Path $RepositoryRoot -ChildPath $RelativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        return $false
    }

    return [bool](Select-String -LiteralPath $fullPath -Pattern 'USER CODE BEGIN' -SimpleMatch -Quiet)
}

# 返回 @{ Decision = 'allow'|'ask'|'deny'; Reason = '...' }。
function Get-AgentHookEditDecision {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$RelativePath,
        [string]$ToolName = ''
    )

    $normalized = ($RelativePath -replace '\\', '/').TrimStart('/')
    $askOrDeny = if ($script:AgentHookAskUnsupportedTools -contains $ToolName) { 'deny' } else { 'ask' }

    foreach ($header in $script:AgentHookGeneratorConfigHeaders) {
        $leaf = ($header -split '/')[-1]
        if (($normalized -ieq $header) -or ($normalized -match ('(?i)(^|/)' + [regex]::Escape($leaf) + '$'))) {
            return @{ Decision = $askOrDeny; Reason = "$normalized 是生成器配置头，改动前须经用户确认（AGENTS.md 硬规则）。" }
        }
    }

    if (Test-GeneratedWriteProtectedPath -RelativePath $normalized) {
        return @{ Decision = 'deny'; Reason = "$normalized 属于 SquareLine/CubeMX/Vendor 生成目录，只认生成器（ARC-2）；请给出源工程配置步骤，由用户导出。" }
    }

    foreach ($pattern in $script:AgentHookCubeMxSourcePatterns) {
        if ($normalized -match $pattern) {
            return @{ Decision = 'deny'; Reason = "$normalized 由 CubeMX 维护（ARC-2）；参数改 io_sheet.ioc 后由用户重新生成。" }
        }
    }

    if (Test-AgentHookUserCodeFile -RepositoryRoot $RepositoryRoot -RelativePath $normalized) {
        return @{ Decision = $askOrDeny; Reason = "$normalized 是 CubeMX 生成文件，只准在 USER CODE 区放对自维护入口的调用或转发（ARC-3），须经用户确认。" }
    }

    return @{ Decision = 'allow'; Reason = '' }
}

# 返回违规原因；命令合规时返回 $null。
function Get-AgentHookShellViolation {
    param(
        [AllowEmptyString()][string]$Command
    )

    if ([string]::IsNullOrWhiteSpace($Command)) {
        return $null
    }

    $segment = '[^;&|\r\n]*'
    if ($Command -match "\bgit\b$segment\s--no-verify\b" -or $Command -match "\bgit\b$segment\bcommit\b$segment\s-[a-zA-Z]*n[a-zA-Z]*\b") {
        return '禁止跳过 Git Hook（--no-verify / commit -n）：FAST/CHANGED 是唯一自动闸门（GIT-4）。'
    }

    if ($Command -match "\bgit\b$segment\bpush\b$segment(\s--force(-with-lease)?\b|\s-[a-zA-Z]*f[a-zA-Z]*\b|\s\+\S)") {
        return '禁止 Agent 强推（GIT-4）；确需覆盖远端时由用户本人执行。'
    }

    if ($Command -match '(?i)ALLOW_GENERATED_UPDATE\s*=|setx\s+ALLOW_GENERATED_UPDATE|SetEnvironmentVariable\(\s*[''"]ALLOW_GENERATED_UPDATE') {
        return '禁止 Agent 设置 ALLOW_GENERATED_UPDATE；只有维护者本人在当前会话为重新导出设置。'
    }

    if ($Command -match "\bgit\b$segment\bcore\.hooksPath\b") {
        return '禁止改写 core.hooksPath；Hook 安装只走 scripts/install-git-hooks.ps1。'
    }

    return $null
}

function Read-AgentHookInput {
    $stdin = [Console]::In.ReadToEnd().TrimStart([char]0xFEFF)
    if ([string]::IsNullOrWhiteSpace($stdin)) {
        return $null
    }

    return $stdin | ConvertFrom-Json
}

function Initialize-AgentHookConsole {
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [Console]::InputEncoding = $utf8
    [Console]::OutputEncoding = $utf8
}

function Get-AgentHookRepositoryRoot {
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
