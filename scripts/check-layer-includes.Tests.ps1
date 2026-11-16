Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$checkerScript = Join-Path -Path $PSScriptRoot -ChildPath 'check-layer-includes.ps1'
if (-not (Test-Path -LiteralPath $checkerScript -PathType Leaf)) {
    throw "缺少分层检查脚本：$checkerScript"
}

. $checkerScript

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment

function Assert-Equal {
    param(
        [Parameter(Mandatory)]
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

function Assert-Null {
    param(
        $Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if ($null -ne $Actual) {
        throw ("{0} 期望为空，实际 '{1}'。" -f $Message, $Actual)
    }
}

function Assert-NotNull {
    param(
        $Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if ($null -eq $Actual) {
        throw ("{0} 期望非空。" -f $Message)
    }
}

$include = Get-CIncludeDirective -Line '  #include "stm32h7xx_hal.h"'
Assert-Equal -Expected 'stm32h7xx_hal.h' -Actual $include -Message '应解析带缩进的引号 include'

$include = Get-CIncludeDirective -Line '# include <main.h>'
Assert-Equal -Expected 'main.h' -Actual $include -Message '应解析空格形式的尖括号 include'

Assert-Null -Actual (Get-CIncludeDirective -Line '// #include "main.h"') -Message '注释中的 include 应忽略'
Assert-Null -Actual (Get-CIncludeDirective -Line "printf(`"#include`");") -Message '字符串中的 include 应忽略'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'stdint.h') `
    -Message 'Component 允许标准 C 头'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'Components/log/log.h') `
    -Message 'Component 允许同层公开头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'stm32h7xx_hal.h') `
    -Message 'Component 禁止 HAL 头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'main.h') `
    -Message 'Component 禁止 main.h'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h') `
    -Message 'Component 禁止 FreeRTOS'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'Platform/power/platform_power.h') `
    -Message 'Component 禁止 Platform'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'component' -IncludePath 'Adapters/stm32_hal/sd/stm32_hal_sd_adapter.h') `
    -Message 'Component 禁止 Adapter'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'bridge' -IncludePath 'Components/w25qxx/w25qxx.h') `
    -Message 'Bridge 允许 Component 头'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'bridge' -IncludePath 'Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.h') `
    -Message 'Bridge 允许自身头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'bridge' -IncludePath 'Adapters/stm32_hal/w25qxx_qspi/w25qxx_qspi_adapter.h') `
    -Message 'Bridge 禁止 HAL Adapter'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'bridge' -IncludePath 'FreeRTOS.h') `
    -Message 'Bridge 禁止 FreeRTOS'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Middlewares/Third_Party/FreeRTOS/Source/include/FreeRTOS.h') `
    -Message 'Service 允许 FreeRTOS'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'FATFS/App/fatfs.h') `
    -Message 'Service 允许 FatFs Glue'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'lvgl.h') `
    -Message 'Service 允许 LVGL'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Platform/lcd/platform_lcd.h') `
    -Message 'Service 允许 Platform'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Adapters/cortex/cache/cortex_m7_dcache_adapter.h') `
    -Message 'Service 允许 Cortex Adapter'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'stm32h7xx_hal.h') `
    -Message 'Service 禁止 HAL 头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'main.h') `
    -Message 'Service 禁止 main.h'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_gpio.h') `
    -Message 'Service 禁止 Drivers'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Adapters/stm32_hal/sd/stm32_hal_sd_adapter.h') `
    -Message 'Service 禁止 HAL Adapter'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'APP/app.h') `
    -Message 'Service 禁止 APP'
foreach ($uiCellInclude in @('APP/ui_cell/ui_cell.h', 'ui_cell/ui_cell.h', '../../APP/ui_cell/ui_cell.h', 'ui_cell.h')) {
    $uiCellViolation = Get-LayerIncludeViolation -SourceKind 'service' -IncludePath $uiCellInclude
    Assert-NotNull -Actual $uiCellViolation -Message "Service/gui 禁止包含 ui_cell：$uiCellInclude"
    if ($uiCellViolation -notmatch 'ui_cell') { throw "ui_cell 违规说明应点名 ui_cell：$uiCellViolation" }
}
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'service' -IncludePath 'Service/gui/gui_ui_cell_helper.h') `
    -Message '名字里含 ui_cell 字样但不是 ui_cell 模块的头不误报'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'platform' -IncludePath 'main.h') `
    -Message 'Platform 允许 main.h'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'platform' -IncludePath 'Adapters/stm32_hal/sd/sd_stm32_hal_adapter.h') `
    -Message 'Platform 允许 HAL Adapter'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'platform' -IncludePath 'Service/gui/gui_service.h') `
    -Message 'Platform 禁止 Service'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'platform' -IncludePath 'APP/app.h') `
    -Message 'Platform 禁止 APP'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'stm32_hal' -IncludePath 'stm32h7xx_hal.h') `
    -Message 'HAL Adapter 允许 HAL 头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'stm32_hal' -IncludePath 'Service/filesystem/filesystem_service.h') `
    -Message 'HAL Adapter 禁止 Service'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'stm32_hal' -IncludePath 'APP/app.h') `
    -Message 'HAL Adapter 禁止 APP'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'cortex' -IncludePath 'stm32h7xx.h') `
    -Message 'Cortex Adapter 允许 CMSIS 头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'cortex' -IncludePath 'Service/gui/gui_service.h') `
    -Message 'Cortex Adapter 禁止 Service'

Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'APP/apps/music/gui/gui_music.h') `
    -Message '应用允许包含自己目录的头'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'APP/tasks/playback/playback_contract.h') `
    -Message '应用允许包含 APP/tasks 的公开合同'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'Service/gui/gui_service.h') `
    -Message '应用允许包含 Service 公开头'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'gui_music_text.h') `
    -Message '应用允许同目录相对包含'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'APP/apps/books/gui/gui_books.h') `
    -Message '应用禁止包含另一个应用的头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'APP\apps\books\gui\gui_books.h') `
    -Message '应用禁止包含另一个应用的头（反斜杠写法）'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -SourceDirectory 'APP/apps/music/gui' -IncludePath '../../books/gui/gui_books.h') `
    -Message '应用禁止经 .. 相对路径包含另一个应用的头'
Assert-Null -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -SourceDirectory 'APP/apps/music/gui' -IncludePath '../foreground/music_foreground.h') `
    -Message '应用允许经 .. 相对路径包含自己目录的头'
Assert-NotNull -Actual (Get-LayerIncludeViolation -SourceKind 'app' -SourceApp 'music' -IncludePath 'apps/books/gui/gui_books.h') `
    -Message '应用禁止以 apps/ 起头的路径包含另一个应用'
Assert-Equal -Expected 'music' -Actual (Get-LayerSourceApp -RelativePath 'APP/apps/music/gui/gui_music.c') `
    -Message '应从路径取出应用名'
Assert-Null -Actual (Get-LayerSourceApp -RelativePath 'APP/apps/app_table.c') `
    -Message '注册表本身不属于任何应用'
Assert-Equal -Expected 'app' -Actual (Get-LayerSourceKind -RelativePath 'APP/apps/music/gui/gui_music.c') `
    -Message 'APP/apps 下的源文件按应用检查'
Assert-Null -Actual (Get-LayerSourceKind -RelativePath 'APP/tasks/gui/gui_task.c') `
    -Message 'APP/tasks 不在分层检查扫描根内'

$repositoryRoot = Split-Path -Path $PSScriptRoot -Parent
Invoke-LayerIncludeCheck -RepositoryRoot $repositoryRoot

function Assert-FixtureViolation {
    param(
        [Parameter(Mandatory)]
        [string]$RelativeFile,

        [Parameter(Mandatory)]
        [string]$IncludeLine,

        [Parameter(Mandatory)]
        [string]$ExpectedPattern,

        [Parameter(Mandatory)]
        [string]$FailureMessage
    )

    $fixtureRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('layer-include-fixture-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Components') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Adapters/bridge') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Adapters/stm32_hal') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Adapters/cortex') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Platform') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'Service') | Out-Null
        New-Item -ItemType Directory -Path (Join-Path -Path $fixtureRoot -ChildPath 'APP/apps') -Force | Out-Null
        $targetFile = Join-Path -Path $fixtureRoot -ChildPath $RelativeFile
        New-Item -ItemType Directory -Path (Split-Path -Path $targetFile -Parent) -Force | Out-Null
        Set-Content -LiteralPath $targetFile -Value "$IncludeLine`n" -Encoding ascii

        $fixtureFailed = $false
        try {
            Invoke-LayerIncludeCheck -RepositoryRoot $fixtureRoot
        }
        catch {
            $fixtureFailed = $true
            if ($_.Exception.Message -notmatch $ExpectedPattern) {
                throw ("夹具应报告 {0}，实际：{1}" -f $ExpectedPattern, $_.Exception.Message)
            }
        }

        if (-not $fixtureFailed) {
            throw $FailureMessage
        }
    }
    finally {
        if (Test-Path -LiteralPath $fixtureRoot) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
        }
    }
}

Assert-FixtureViolation -RelativeFile 'Components/foo/foo.c' `
    -IncludeLine '#include "stm32h7xx_hal.h"' `
    -ExpectedPattern 'stm32h7xx_hal\.h' `
    -FailureMessage '夹具含 HAL 头时分层检查应失败。'
Assert-FixtureViolation -RelativeFile 'Service/foo/foo.c' `
    -IncludeLine '#include "main.h"' `
    -ExpectedPattern 'main\.h' `
    -FailureMessage 'Service 夹具含 main.h 时分层检查应失败。'
Assert-FixtureViolation -RelativeFile 'Platform/foo/foo.c' `
    -IncludeLine '#include "Service/gui/gui_service.h"' `
    -ExpectedPattern 'Service/gui/gui_service\.h' `
    -FailureMessage 'Platform 夹具含 Service 头时分层检查应失败。'
Assert-FixtureViolation -RelativeFile 'Adapters/stm32_hal/foo/foo.c' `
    -IncludeLine '#include "APP/app.h"' `
    -ExpectedPattern 'APP/app\.h' `
    -FailureMessage 'HAL Adapter 夹具含 APP 头时分层检查应失败。'

Assert-FixtureViolation -RelativeFile 'Service/gui/foo/foo.c' `
    -IncludeLine '#include "APP/ui_cell/ui_cell.h"' `
    -ExpectedPattern 'APP/ui_cell/ui_cell\.h' `
    -FailureMessage 'Service/gui 夹具包含 ui_cell 时分层检查应失败。'

Assert-FixtureViolation -RelativeFile 'APP/apps/music/gui/foo.c' `
    -IncludeLine '#include "APP/apps/books/gui/gui_books.h"' `
    -ExpectedPattern 'APP/apps/books/gui/gui_books\.h' `
    -FailureMessage '应用夹具包含另一个应用的头时分层检查应失败。'
Assert-FixtureViolation -RelativeFile 'APP/apps/music/gui/foo.c' `
    -IncludeLine '#include "../../books/gui_books.h"' `
    -ExpectedPattern 'books/gui_books\.h' `
    -FailureMessage '应用夹具经 .. 包含另一个应用的头时分层检查应失败。'

$indexFixtureRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
    -ChildPath ('layer-index-fixture-' + [Guid]::NewGuid().ToString('N'))
try {
    $scanRoots = @('Components', 'Adapters/bridge', 'Adapters/stm32_hal', 'Adapters/cortex', 'Platform', 'Service', 'APP/apps')
    foreach ($scanRoot in $scanRoots) {
        $directory = Join-Path -Path $indexFixtureRoot -ChildPath $scanRoot
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
        Set-Content -LiteralPath (Join-Path -Path $directory -ChildPath 'fixture.c') `
            -Value '#include <stdint.h>' -Encoding ascii
    }

    $git = Get-ExternalCommand -Name 'git'
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $indexFixtureRoot, 'init')
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $indexFixtureRoot, 'add', '--all')
    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $indexFixtureRoot,
        '-c', 'commit.gpgsign=false',
        '-c', 'user.email=layer-index-test@example.invalid',
        '-c', 'user.name=layer-index-test',
        'commit', '-m', 'seed'
    )

    $componentFile = Join-Path -Path $indexFixtureRoot -ChildPath 'Components/fixture.c'
    Set-Content -LiteralPath $componentFile -Value '#include "main.h"' -Encoding ascii
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $indexFixtureRoot, 'add', '--', 'Components/fixture.c')
    Set-Content -LiteralPath $componentFile -Value '#include <stdint.h>' -Encoding ascii

    Invoke-LayerIncludeCheck -RepositoryRoot $indexFixtureRoot
    try {
        Invoke-LayerIndexIncludeCheck -RepositoryRoot $indexFixtureRoot
        throw '工作树已修正时，索引里的违规 include 仍应失败。'
    }
    catch {
        if ($_.Exception.Message -notmatch 'main\.h') {
            throw "索引快照应报告暂存的 main.h，实际：$($_.Exception.Message)"
        }
    }
}
finally {
    if (Test-Path -LiteralPath $indexFixtureRoot) {
        Remove-Item -LiteralPath $indexFixtureRoot -Recurse -Force
    }
}

Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment

Write-Output 'check-layer-includes 函数测试通过。'
