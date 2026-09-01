[CmdletBinding()]
param(
    [string]$HostCompiler,

    [ValidateSet('flash_ftl', 'w25qxx', 'resource_pack')]
    [string[]]$Module = @('flash_ftl', 'w25qxx', 'resource_pack')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')

function Get-HostCompiler {
    [CmdletBinding()]
    param(
        [string]$RequestedCompiler
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedCompiler)) {
        return Get-ExternalCommand -Name $RequestedCompiler
    }

    if (-not [string]::IsNullOrWhiteSpace($env:CC)) {
        return Get-ExternalCommand -Name $env:CC
    }

    return Get-ExternalCommand -Name 'gcc'
}

function Confirm-HostGccCompiler {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$CompilerPath
    )

    $targetTripleOutput = @(Invoke-ExternalCommand -CommandPath $CompilerPath `
            -Arguments @('-dumpmachine') -CaptureOutput)
    $targetTripleLines = @($targetTripleOutput | ForEach-Object { $_.ToString().Trim() })
    if (($targetTripleLines.Count -ne 1) -or [string]::IsNullOrWhiteSpace($targetTripleLines[0])) {
        throw "主机编译器返回的目标三元组无效：$CompilerPath 的 -dumpmachine 输出必须恰好为一条非空行。"
    }

    $targetTriple = $targetTripleLines[0]

    if ($targetTriple -match '(?i)(^|[-_])(arm|aarch64|thumb)[a-z0-9._]*($|[-_])') {
        throw "主机编译器目标三元组不能为 ARM、AArch64 或 Thumb 交叉目标：$targetTriple。请选择可在本机执行测试的 GCC。"
    }

    $targetTripleFields = @($targetTriple -split '-' | ForEach-Object { $_.Trim() })
    if (($targetTripleFields.Count -lt 2) -or ($targetTripleFields | Where-Object { [string]::IsNullOrWhiteSpace($_) })) {
        throw "主机编译器目标三元组格式无效：$targetTriple。目标三元组必须由非空连字符字段组成。"
    }

    if ($targetTripleFields[0] -notmatch '^(?i:x86_64|amd64|x64|i[3-6]86|x86)$') {
        throw "主机编译器目标三元组 CPU 不受支持：$targetTriple。仅支持可在当前 Windows x86/x64 本机运行的 x86/x64 GCC（x86_64、amd64、i386、i486、i586、i686 或 x86）；RISC-V、MIPS 等其他 CPU 目标不能在当前本机运行。"
    }

    $nonWindowsTargetField = $targetTripleFields | Where-Object {
        $_ -match '^(?i:linux|darwin|bsd|freebsd|netbsd|openbsd|android|elf|none|eabi)'
    } | Select-Object -First 1
    if ($null -ne $nonWindowsTargetField) {
        throw "主机编译器目标三元组包含非 Windows OS 或裸机标识字段 [$nonWindowsTargetField]：$targetTriple。不能将 Linux、Darwin、BSD、Android 或裸机目标用于当前 Windows 主机测试。"
    }

    $windowsRuntimeField = $targetTripleFields | Where-Object {
        $_ -match '^(?i:mingw|mingw32|mingw64|msys|cygwin|windows)$'
    } | Select-Object -First 1
    if ($null -eq $windowsRuntimeField) {
        throw "主机编译器目标三元组不支持：$targetTriple。仅支持含有 mingw、mingw32、mingw64、msys、cygwin 或 windows 连字符字段的 Windows 可执行主机 GCC。"
    }

    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # GCC 将 -v 版本信息输出到标准错误；允许捕获该输出而不将其提升为 PowerShell 错误。
        $ErrorActionPreference = 'Continue'
        $compilerVersionOutput = @(Invoke-ExternalCommand -CommandPath $CompilerPath `
                -Arguments @('-v') -CaptureOutput)
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $compilerVersionText = (($compilerVersionOutput | ForEach-Object { $_.ToString().Trim() }) -join [Environment]::NewLine)
    if ($compilerVersionText -match '(?i)\bclang\b') {
        throw "主机编译器厂商不支持：$CompilerPath 的 -v 输出表明其为 Clang。请选择 GCC。"
    }

    if ($compilerVersionText -notmatch '(?im)^\s*gcc(?:\.exe)?\s+version\b') {
        throw "无法确认主机编译器厂商：$CompilerPath 的 -v 输出未包含明确的 GCC 版本标识。请选择 GCC。"
    }
}

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $hostCompilerPath = Get-HostCompiler -RequestedCompiler $HostCompiler
    Confirm-HostGccCompiler -CompilerPath $hostCompilerPath

    $cmake = Get-ExternalCommand -Name 'cmake'
    [void](Get-ExternalCommand -Name 'ninja')
    $ctest = Get-ExternalCommand -Name 'ctest'

    foreach ($moduleName in $Module) {
        $moduleSourceDirectory = Join-Path -Path $repositoryRoot -ChildPath (Join-Path -Path 'Tests' -ChildPath $moduleName)
        $moduleBuildDirectory = Join-Path -Path $repositoryRoot -ChildPath (Join-Path -Path 'build/host' -ChildPath $moduleName)

        Invoke-ExternalCommand -CommandPath $cmake -Arguments @(
            '-S', $moduleSourceDirectory,
            '-B', $moduleBuildDirectory,
            '-G', 'Ninja',
            "-DCMAKE_C_COMPILER=$hostCompilerPath",
            '-DCMAKE_BUILD_TYPE=Debug'
        )
        Invoke-ExternalCommand -CommandPath $cmake -Arguments @('--build', $moduleBuildDirectory)
        Invoke-InDirectory -Path $moduleBuildDirectory -Action {
            Invoke-ExternalCommand -CommandPath $ctest -Arguments @('--output-on-failure')
        }
    }
}
