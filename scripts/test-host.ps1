[CmdletBinding()]
param(
    [string]$HostCompiler,

    [ValidateSet('external_loader', 'flash_ftl', 'filesystem_locator', 'gui_task', 'gui_theme', 'gui_canvas', 'gui_font', 'w25qxx', 'resource_pack', 'storage_audio_stream', 'storage_catalog', 'playback_service', 'playback_task', 'monitor_warn', 'ui_cell', 'board_test_card')]
    [string[]]$Module = @('external_loader', 'flash_ftl', 'filesystem_locator', 'gui_task', 'gui_theme', 'gui_canvas', 'gui_font', 'w25qxx', 'resource_pack', 'storage_audio_stream', 'storage_catalog', 'playback_service', 'playback_task', 'monitor_warn', 'ui_cell', 'board_test_card'),

    # 跳过慢用例（规则里声明 HasSlowTests 的模块，目前是样本卡的整文件确定性用例）：CHANGED 用，FULL 不带此开关、全跑。
    [switch]$SkipSlow
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'build_helpers.ps1')
. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

function Get-HostCompiler {
    [CmdletBinding()]
    param(
        [string]$RequestedCompiler
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedCompiler)) {
        return Get-ExternalCommand -Name $RequestedCompiler
    }

    # 不读通用 CC：本机其他项目常把 CC 设为 Clang，会误伤本仓库只认 GCC 的主机测试。
    if (-not [string]::IsNullOrWhiteSpace($env:MP3_HOST_CC)) {
        return Get-ExternalCommand -Name $env:MP3_HOST_CC
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

function Invoke-PythonHostModule {
    <#
    .SYNOPSIS
        运行规则里 Kind = 'Python' 的主机模块：规则 TestDirectory 目录下的全部 test_*.py（unittest）。
    .DESCRIPTION
        依赖 lameenc 的用例缺包时由用例自身 skip；这里先探测一次并在输出写明，-v 把每条 skip 原因也打出来。
        -SkipSlow 设置 MP3_SKIP_SLOW_TESTS=1，慢用例据此 skip（FULL 不带，全跑）。
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$ModuleName,
        [Parameter(Mandatory)][string]$TestDirectory,
        [switch]$SkipSlow
    )

    $python = Find-ExternalCommand -Name 'python'
    if ([string]::IsNullOrWhiteSpace([string]$python)) {
        throw "主机模块 '$ModuleName' 需要 python（运行 $TestDirectory 下的 unittest）；未在 PATH 中找到。"
    }
    $python = [string]@($python)[0]

    $moduleDirectory = Join-Path -Path $RepositoryRoot -ChildPath $TestDirectory
    $saved = @{}
    foreach ($name in 'PYTHONDONTWRITEBYTECODE', 'PYTHONUTF8', 'MP3_SKIP_SLOW_TESTS') {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        # 不在源码树里留 __pycache__；中文输出按 UTF-8。
        $env:PYTHONDONTWRITEBYTECODE = '1'
        $env:PYTHONUTF8 = '1'
        if ($SkipSlow) { $env:MP3_SKIP_SLOW_TESTS = '1' } else { Remove-Item -Path Env:MP3_SKIP_SLOW_TESTS -ErrorAction SilentlyContinue }

        $previous = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            & $python -c 'import lameenc' 2>&1 | Out-Null
            $haveLameenc = ($LASTEXITCODE -eq 0)
        }
        finally { $ErrorActionPreference = $previous }
        if (-not $haveLameenc) {
            Write-NativeUtf8Line -Text "${ModuleName}：未安装 lameenc（pip install lameenc），整文件编码用例将被跳过，只跑不依赖它的合同用例。"
        }
        if ($SkipSlow) {
            Write-NativeUtf8Line -Text "${ModuleName}：-SkipSlow，慢用例被跳过（FULL 全跑）。"
        }

        # -I：隔离模式，不读 PYTHONPATH 与当前目录；discover 自己把起始目录放进 sys.path。
        Invoke-ExternalCommand -CommandPath $python -Arguments @(
            '-I', '-m', 'unittest', 'discover', '-v', '-s', $moduleDirectory, '-p', 'test_*.py')
    }
    finally {
        foreach ($name in $saved.Keys) {
            [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
        }
    }
}

Complete-Utf8EntryScript -Action {
    $repositoryRoot = Get-RepositoryRoot -EntryScriptPath $PSCommandPath
    $rules = Get-HarnessRules
    $pythonRules = @{}
    foreach ($pythonRule in @(Get-HarnessPythonModuleRule -Rules $rules)) {
        $pythonRules[[string]$pythonRule.Name] = $pythonRule
    }
    $pythonModules = @($pythonRules.Keys)
    $cmakeModules = @($Module | Where-Object { $pythonModules -notcontains $_ })

    # 只选 Python 模块时不需要 GCC/cmake/ninja，也不因缺它们而失败。
    if ($cmakeModules.Count -gt 0) {
        $hostCompilerPath = Get-HostCompiler -RequestedCompiler $HostCompiler
        Confirm-HostGccCompiler -CompilerPath $hostCompilerPath

        $cmake = Get-ExternalCommand -Name 'cmake'
        [void](Get-ExternalCommand -Name 'ninja')
        $ctest = Get-ExternalCommand -Name 'ctest'
    }

    foreach ($moduleName in $Module) {
        if ($pythonModules -contains $moduleName) {
            Invoke-PythonHostModule -RepositoryRoot $repositoryRoot -ModuleName $moduleName `
                -TestDirectory (Get-HarnessPythonTestDirectory -ModuleRule $pythonRules[$moduleName]) -SkipSlow:$SkipSlow
            continue
        }

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
