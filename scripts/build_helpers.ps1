Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Initialize-Utf8Console {
    [CmdletBinding()]
    param()

    $utf8 = New-Object System.Text.UTF8Encoding $false
    try {
        [Console]::OutputEncoding = $utf8
        [Console]::InputEncoding = $utf8
    }
    catch {
    }

    $global:OutputEncoding = $utf8
}

function Write-NativeUtf8Line {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Text,

        [ValidateSet('Output', 'Error')]
        [string]$Stream = 'Output'
    )

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text + "`n")
    $consoleStream = if ($Stream -eq 'Error') {
        [Console]::OpenStandardError()
    }
    else {
        [Console]::OpenStandardOutput()
    }

    $consoleStream.Write($bytes, 0, $bytes.Length)
    $consoleStream.Flush()
}

function Complete-Utf8EntryScript {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Action
    )

    Initialize-Utf8Console
    try {
        & $Action
    }
    catch {
        Write-NativeUtf8Line -Stream Error -Text $_.Exception.Message
        exit 1
    }
}

function Get-RepositoryRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$EntryScriptPath
    )

    $scriptDirectory = Split-Path -Path $EntryScriptPath -Parent
    $repositoryRoot = Split-Path -Path $scriptDirectory -Parent
    $cmakeListsPath = Join-Path -Path $repositoryRoot -ChildPath 'CMakeLists.txt'
    $testsPath = Join-Path -Path $repositoryRoot -ChildPath 'Tests'

    if (-not (Test-Path -LiteralPath $cmakeListsPath -PathType Leaf)) {
        throw "无法定位仓库根目录：'$repositoryRoot' 不包含 CMakeLists.txt。"
    }

    if (-not (Test-Path -LiteralPath $testsPath -PathType Container)) {
        throw "无法定位仓库根目录：'$repositoryRoot' 不包含 Tests 目录。"
    }

    return (Resolve-Path -LiteralPath $repositoryRoot).Path
}

function Find-ExternalCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Name)) {
        return $null
    }

    $commandName = $Name.Trim()
    $commands = @(Get-Command -Name $commandName -CommandType Application -ErrorAction SilentlyContinue)
    foreach ($command in $commands) {
        if ($null -eq $command) {
            continue
        }

        $candidate = [string]$command.Path
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            return $candidate
        }
    }

    if (-not (Test-Path -LiteralPath $commandName -PathType Leaf)) {
        return $null
    }

    $commandPath = (Resolve-Path -LiteralPath $commandName).Path
    $executableExtensions = @($env:PATHEXT -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $commandExtension = [System.IO.Path]::GetExtension($commandPath)
    if ($executableExtensions -notcontains $commandExtension) {
        return $null
    }

    return $commandPath
}

function Get-ExternalCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Name
    )

    $commandPath = Find-ExternalCommand -Name $Name
    if ($commandPath -is [System.Array]) {
        $selected = $null
        foreach ($item in $commandPath) {
            $text = [string]$item
            if (-not [string]::IsNullOrWhiteSpace($text)) {
                $selected = $text
                break
            }
        }

        $commandPath = $selected
    }
    elseif ($null -ne $commandPath) {
        $commandPath = [string]$commandPath
    }

    if ([string]::IsNullOrWhiteSpace($commandPath)) {
        throw "未找到外部命令：$Name。"
    }

    return $commandPath
}

function Invoke-ExternalCommand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$CommandPath,

        [string[]]$Arguments = @(),

        [switch]$CaptureOutput
    )

    if ($CaptureOutput) {
        $commandOutput = & $CommandPath @Arguments 2>&1
    }
    else {
        & $CommandPath @Arguments
    }

    $exitCode = $LASTEXITCODE
    if (($null -ne $exitCode) -and ($exitCode -ne 0)) {
        throw "外部命令执行失败：$CommandPath，退出码：$exitCode。"
    }

    if ($CaptureOutput) {
        return $commandOutput
    }
}

function Invoke-InDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [scriptblock]$Action
    )

    Push-Location -LiteralPath $Path
    try {
        & $Action
    }
    finally {
        Pop-Location
    }
}

function Invoke-PowerShellScript {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$ScriptPath,

        [hashtable]$Parameters = @{}
    )

    # 清除当前会话可能遗留的原生命令退出码。
    $global:LASTEXITCODE = 0
    & $ScriptPath @Parameters
    $exitCode = $LASTEXITCODE
    if (($null -ne $exitCode) -and ($exitCode -ne 0)) {
        throw "脚本执行失败：$ScriptPath，退出码：$exitCode。"
    }
}

$script:InheritedGitEnvironmentNames = @(
    'GIT_DIR'
    'GIT_WORK_TREE'
    'GIT_INDEX_FILE'
    'GIT_COMMON_DIR'
    'GIT_PREFIX'
    'GIT_OBJECT_DIRECTORY'
    'GIT_ALTERNATE_OBJECT_DIRECTORIES'
)

function Suspend-InheritedGitEnvironment {
    # Git Hook（worktree 中尤甚）以绝对路径导出 GIT_DIR / GIT_INDEX_FILE；
    # 自测夹具的 git -C <临时目录> init/commit 会被它们带回真实仓库。
    $saved = @{}
    foreach ($name in $script:InheritedGitEnvironmentNames) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if ($null -ne $value) {
            $saved[$name] = $value
            Remove-Item -Path "Env:$name"
        }
    }
    return $saved
}

function Restore-InheritedGitEnvironment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [hashtable]$Saved
    )

    foreach ($name in $Saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $Saved[$name], 'Process')
    }
}
