Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$checkerScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-test-paths.ps1'
if (-not (Test-Path -LiteralPath $checkerScript -PathType Leaf)) {
    throw "缺少主机测试路径检查脚本：$checkerScript"
}

. $checkerScript

function Assert-Equal {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyString()]
        [string]$Expected,

        [AllowNull()]
        $Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if ($Expected -ne [string]$Actual) {
        throw ("{0} 期望 '{1}'，实际 '{2}'。" -f $Message, $Expected, $Actual)
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Action,

        [Parameter(Mandatory)]
        [string]$MessagePattern,

        [Parameter(Mandatory)]
        [string]$Message
    )

    $thrown = $null
    try {
        & $Action | Out-Null
    }
    catch {
        $thrown = $_.Exception.Message
    }

    if ($null -eq $thrown) {
        throw "$Message 期望抛出异常，实际通过。"
    }

    if ($thrown -notmatch $MessagePattern) {
        throw "$Message 异常信息应匹配 '$MessagePattern'，实际 '$thrown'。"
    }
}

# --- Makefile 依赖文本解析 ---------------------------------------------------

$dependencyText = "dep: \`n C:\repo\Tests\m\a.c \`n C:/repo/APP/x.h \`r`n C:/repo/dir\ with\ space/y.h`n"
$parsed = @(ConvertFrom-MakeDependencyText -Text $dependencyText)
Assert-Equal -Expected '3' -Actual $parsed.Count -Message '应解析出三个依赖并跳过目标名'
Assert-Equal -Expected 'C:\repo\Tests\m\a.c' -Actual $parsed[0] -Message '应保留反斜杠路径'
Assert-Equal -Expected 'C:/repo/APP/x.h' -Actual $parsed[1] -Message '应处理续行（含 CRLF）'
Assert-Equal -Expected 'C:/repo/dir with space/y.h' -Actual $parsed[2] -Message '应还原转义空格'

# --- compile_commands 命令拆分 -----------------------------------------------

$command = Split-CompileCommand -Command 'E:\mingw64\bin\gcc.exe -IE:/r/Tests/m/../.. -DX=1 -Wall -o CMakeFiles\t.dir\a.c.obj -c E:\r\Tests\m\a.c'
Assert-Equal -Expected 'E:\mingw64\bin\gcc.exe' -Actual $command.Executable -Message '应取出编译器路径'
Assert-Equal -Expected '-IE:/r/Tests/m/../.. -DX=1 -Wall -c E:\r\Tests\m\a.c' -Actual $command.Arguments `
    -Message '应去掉 -o 及其参数，保留其余编译参数'

$quotedCommand = Split-CompileCommand -Command '"C:\Program Files\gcc\gcc.exe" -I"C:/a b" -o out.obj -c a.c'
Assert-Equal -Expected 'C:\Program Files\gcc\gcc.exe' -Actual $quotedCommand.Executable -Message '应去掉带空格编译器路径的引号'
Assert-Equal -Expected '-I"C:/a b" -c a.c' -Actual $quotedCommand.Arguments -Message '带引号的 -o 也应整体移除'

# --- 依赖路径归属 ------------------------------------------------------------

$fixtureBase = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
    -ChildPath ('mp3-testpaths-selftest-' + [Guid]::NewGuid().ToString('N'))
try {
    $repoRoot = Join-Path -Path $fixtureBase -ChildPath 'repo'
    $buildRoot = Join-Path -Path $fixtureBase -ChildPath 'build'
    New-Item -ItemType Directory -Path $repoRoot, $buildRoot -Force | Out-Null

    $inRepo = Get-DependencyRelativePath -RepositoryRoot $repoRoot -ExternalRoot $buildRoot `
        -Path (($repoRoot -replace '\\', '/') + '/Tests/m/../../Lib/dep.h')
    Assert-Equal -Expected 'Lib/dep.h' -Actual $inRepo -Message '应规范化 .. 并返回仓库相对路径'

    $generated = Get-DependencyRelativePath -RepositoryRoot $repoRoot -ExternalRoot $buildRoot `
        -Path (Join-Path -Path $buildRoot -ChildPath 'm/gen/gen.h')
    Assert-Equal -Expected '' -Actual $generated -Message '构建目录下的生成物不属于仓库路径'

    Assert-Throws -MessagePattern '无法归属' -Message '既不在仓库也不在构建目录的依赖必须失败而非静默忽略' -Action {
        Get-DependencyRelativePath -RepositoryRoot $repoRoot -ExternalRoot $buildRoot -Path 'Z:/elsewhere/x.h'
    }

    # --- 覆盖判定（纯函数） --------------------------------------------------
    $rules = @{
        AllHostModules = @('alpha', 'beta')
        HostModuleRules = @(
            @{ Name = 'alpha'; PathPatterns = @('^Lib/a/', '^Tests/alpha/') }
            @{ Name = 'beta'; PathPatterns = @('^Lib/b\.h$') }
        )
    }
    $uncovered = @(Get-UncoveredHostModulePath -Rules $rules -Dependency @{
            alpha = @('Lib/a/x.c', 'Lib/shared.h', 'Tests/alpha/t.c')
            beta = @('Lib/b.h', 'Lib/shared.h', 'Tests/alpha/t.c')
        })
    Assert-Equal -Expected 'alpha <- Lib/shared.h|beta <- Lib/shared.h|beta <- Tests/alpha/t.c' `
        -Actual ($uncovered -join '|') -Message '应只报告未被本模块 PathPatterns 覆盖的依赖，豁免自身 Tests/<模块>/ 树，并按模块与路径排序'

    $missingRule = @{ AllHostModules = @('alpha', 'gamma'); HostModuleRules = @($rules.HostModuleRules[0]) }
    Assert-Throws -MessagePattern 'gamma' -Message 'AllHostModules 中缺规则的模块必须失败' -Action {
        Get-UncoveredHostModulePath -Rules $missingRule -Dependency @{ alpha = @(); gamma = @() }
    }

    # --- 端到端：临时仓库 + 真实 CMake 配置与 gcc -MM ---------------------------
    New-Item -ItemType Directory -Path (Join-Path $repoRoot 'Tests/demo'), (Join-Path $repoRoot 'Lib/core'),
        (Join-Path $repoRoot 'Lib/extra') -Force | Out-Null
    $utf8 = New-Object System.Text.UTF8Encoding $false
    $files = @{
        'Tests/demo/CMakeLists.txt' = @'
cmake_minimum_required(VERSION 3.22)
project(demo_tests LANGUAGES C)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen/generated.h" "#define GENERATED 1\n")
add_executable(demo_tests test_demo.c ../../Lib/core/core.c)
target_include_directories(demo_tests PRIVATE ../.. "${CMAKE_CURRENT_BINARY_DIR}/gen")
'@
        'Tests/demo/test_demo.c' = "#include `"Lib/core/core.h`"`n#include `"generated.h`"`nint main(void) { return core_value() - GENERATED; }`n"
        'Lib/core/core.h' = "#include `"Lib/extra/extra.h`"`nint core_value(void);`n"
        'Lib/core/core.c' = "#include `"core.h`"`nint core_value(void) { return EXTRA_VALUE; }`n"
        'Lib/extra/extra.h' = "#define EXTRA_VALUE 1`n"
    }
    foreach ($relative in $files.Keys) {
        [System.IO.File]::WriteAllText((Join-Path $repoRoot $relative), $files[$relative], $utf8)
    }

    $incomplete = @{
        AllHostModules = @('demo')
        HostModuleRules = @(@{ Name = 'demo'; PathPatterns = @('^Lib/core/', '^Tests/demo/') })
    }
    Assert-Throws -MessagePattern 'demo <- Lib/extra/extra\.h' -Message '传递依赖的头文件未被覆盖时必须失败并列出 module <- path' -Action {
        Invoke-TestPathsCheck -RepositoryRoot $repoRoot -Rules $incomplete
    }

    $complete = @{
        AllHostModules = @('demo')
        HostModuleRules = @(@{ Name = 'demo'; PathPatterns = @('^Lib/core/', '^Lib/extra/extra\.h$', '^Tests/demo/') })
    }
    $summary = Invoke-TestPathsCheck -RepositoryRoot $repoRoot -Rules $complete
    Assert-Equal -Expected '1' -Actual $summary.ModuleCount -Message '全部覆盖时应通过并统计模块数'
    # test_demo.c、core.c、core.h、extra.h 与 CMake 生成的 generated.h（构建目录，不计）
    Assert-Equal -Expected '4' -Actual $summary.DependencyCount -Message '依赖应含源文件与传递头文件，且不含构建目录生成物'
}
finally {
    if (Test-Path -LiteralPath $fixtureBase) {
        Remove-Item -LiteralPath $fixtureBase -Recurse -Force
    }
}

Write-Output 'check-test-paths 自测通过。'
