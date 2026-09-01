Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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
    $command = Get-Command -Name $commandName -CommandType Application -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Path
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
    if ($exitCode -ne 0) {
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
