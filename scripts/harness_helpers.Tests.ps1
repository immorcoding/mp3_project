Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path -Path $PSScriptRoot -ChildPath 'harness_helpers.ps1')

$inheritedGitEnvironment = Suspend-InheritedGitEnvironment

function Assert-HarnessEqual {
    param(
        [Parameter(Mandatory)]
        $Expected,

        [Parameter(Mandatory)]
        $Actual,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if ([string]$Expected -ne [string]$Actual) {
        throw "$Message 期望 '$Expected'，实际 '$Actual'。"
    }
}

function Assert-HarnessSequence {
    param(
        [string[]]$Expected,
        [string[]]$Actual,
        [Parameter(Mandatory)][string]$Message
    )

    Assert-HarnessEqual -Expected ($Expected -join ',') -Actual ($Actual -join ',') -Message $Message
}

$ftlImpact = Get-HarnessImpact -ChangedPath @('Components/flash_ftl/flash_ftl.c')
Assert-HarnessSequence -Expected @('flash_ftl', 'w25qxx') -Actual $ftlImpact.HostModules `
    -Message 'FTL 变化必须覆盖直接测试及编译该实现的 W25Qxx 测试'
Assert-HarnessEqual -Expected $false -Actual $ftlImpact.RequiresAllHostTests -Message '已知 FTL 路径不应回退全部测试'

$resourceImpact = Get-HarnessImpact -ChangedPath @('Components/resource_pack/resource_pack.c')
Assert-HarnessSequence -Expected @('resource_pack') -Actual $resourceImpact.HostModules `
    -Message '资源包变化只选择资源包主机测试'

$docsImpact = Get-HarnessImpact -ChangedPath @('docs/verification.md')
Assert-HarnessEqual -Expected 0 -Actual $docsImpact.HostModules.Count -Message '纯文档变化不跑主机测试'

$loaderTestImpact = Get-HarnessImpact -ChangedPath @('Tools/external_loader/tests/loader_geometry_test.c')
Assert-HarnessSequence -Expected @('external_loader') -Actual $loaderTestImpact.HostModules `
    -Message '外部加载器几何变化必须选择对应主机测试'
Assert-HarnessEqual -Expected $false -Actual $loaderTestImpact.RequiresHardware `
    -Message '外部加载器纯主机测试变化不应要求上板'

$loaderProductionImpact = Get-HarnessImpact -ChangedPath @('Tools/external_loader/src/loader_geometry.c')
Assert-HarnessSequence -Expected @('external_loader') -Actual $loaderProductionImpact.HostModules `
    -Message '外部加载器生产代码必须选择对应主机测试'
Assert-HarnessEqual -Expected $true -Actual $loaderProductionImpact.RequiresHardware `
    -Message '外部加载器生产代码仍必须要求上板'

$guiTaskImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/gui/gui_task.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $guiTaskImpact.HostModules `
    -Message 'GUI Task 窗口协议变化必须选择对应主机测试'

$queueImpact = Get-HarnessImpact -ChangedPath @('Service/gui/main/queue/gui_service_main_queue.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $queueImpact.HostModules `
    -Message 'Queue 行 Module 变化必须选择 GUI Task 主机测试'

$musicImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/gui/music/gui_music.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $musicImpact.HostModules `
    -Message 'GUI Task 音乐分区变化必须选择对应主机测试'

$inputImpact = Get-HarnessImpact -ChangedPath @('Service/gui/gui_service_input.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $inputImpact.HostModules `
    -Message 'GUI 输入单槽变化必须选择 GUI Task 主机测试'

$transportImpact = Get-HarnessImpact -ChangedPath @('Service/gui/main/transport/gui_service_main_transport.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $transportImpact.HostModules `
    -Message 'Now Playing 三键 Module 变化必须选择 GUI Task 主机测试'

$themeImpact = Get-HarnessImpact -ChangedPath @('Service/gui/theme/gui_service_theme.c')
Assert-HarnessSequence -Expected @('gui_theme') -Actual $themeImpact.HostModules `
    -Message '调色板变化必须选择 GUI 主题主机测试'

$guiServiceImpact = Get-HarnessImpact -ChangedPath @('Service/gui/gui_service.c')
Assert-HarnessSequence -Expected @('gui_task', 'gui_theme') -Actual $guiServiceImpact.HostModules `
    -Message 'GUI Service 公开输入接缝变化必须选择 GUI Task 与主题主机测试'

$canvasImpact = Get-HarnessImpact -ChangedPath @('Service/gui/canvas/gui_service_canvas_compositor.c')
Assert-HarnessSequence -Expected @('gui_canvas') -Actual $canvasImpact.HostModules `
    -Message 'Canvas 合成变化必须选择 GUI Canvas 主机测试'

$vinylImpact = Get-HarnessImpact -ChangedPath @('Service/gui/main/vinyl/gui_service_main_vinyl.c')
Assert-HarnessSequence -Expected @('gui_canvas') -Actual $vinylImpact.HostModules `
    -Message '假唱盘绑定变化必须选择 GUI Canvas 主机测试'

$backgroundImpact = Get-HarnessImpact -ChangedPath @('Service/gui/main/background/gui_service_main_background.c')
Assert-HarnessSequence -Expected @('gui_theme') -Actual $backgroundImpact.HostModules `
    -Message 'Main 毛玻璃开关变化必须选择 GUI 主题主机测试'

$catalogImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/catalog/storage_playback_cursor.c')
Assert-HarnessSequence -Expected @('storage_catalog') -Actual $catalogImpact.HostModules `
    -Message '播放列表游标变化必须选择 Catalog 主机测试'

$harnessImpact = Get-HarnessImpact -ChangedPath @('scripts/verify_changed.ps1')
Assert-HarnessSequence -Expected @('external_loader', 'flash_ftl', 'gui_canvas', 'gui_task', 'gui_theme', 'resource_pack', 'storage_catalog', 'w25qxx') -Actual $harnessImpact.HostModules `
    -Message 'Harness 变化必须回退全部主机测试'
Assert-HarnessEqual -Expected $true -Actual $harnessImpact.RequiresAllHostTests -Message 'Harness 变化应标记全部测试'

$unknownImpact = Get-HarnessImpact -ChangedPath @('Service/playback/player.c')
Assert-HarnessSequence -Expected @('external_loader', 'flash_ftl', 'gui_canvas', 'gui_task', 'gui_theme', 'resource_pack', 'storage_catalog', 'w25qxx') -Actual $unknownImpact.HostModules `
    -Message '未知生产路径不得以零测试通过'
Assert-HarnessSequence -Expected @('Service/playback/player.c') -Actual $unknownImpact.UnknownPaths `
    -Message '应保留触发保守回退的路径证据'

$hardwareImpact = Get-HarnessImpact -ChangedPath @('Platform/sd/platform_sd.c')
Assert-HarnessEqual -Expected $true -Actual $hardwareImpact.RequiresHardware -Message 'Platform 变化必须要求上板'

$hardwarePaths = @(
    'Components/w25qxx/w25qxx.c'
    'Adapters/bridge/flash_ftl_w25qxx/flash_ftl_w25qxx_bridge.c'
    'Adapters/bridge/axp2101_soft_i2c/axp2101_soft_i2c_adapter.c'
    'Service/resource/resource_service.c'
    'Service/log/log_service.c'
    'startup_stm32h743xx.s'
    'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c'
    'Middlewares/Third_Party/FreeRTOS/Source/tasks.c'
    'Tools/external_loader/src/loader_w25q256.c'
    'Tools/external_loader/CMakeLists.txt'
    'Tools/external_loader/cmake/arm-none-eabi-gcc.cmake'
)
foreach ($hardwarePath in $hardwarePaths) {
    $hardwareImpact = Get-HarnessImpact -ChangedPath @($hardwarePath)
    Assert-HarnessEqual -Expected $true -Actual $hardwareImpact.RequiresHardware `
        -Message "$hardwarePath 变化必须要求上板"
}

$hardwareDocsImpact = Get-HarnessImpact -ChangedPath @('Platform/audio/README.md')
Assert-HarnessEqual -Expected $false -Actual $hardwareDocsImpact.RequiresHardware `
    -Message '硬件目录中的纯文档变化不应要求上板'

$pushUpdates = @(ConvertFrom-HarnessPushInput -InputText (
        "refs/heads/main 0123456789012345678901234567890123456789 " +
        "refs/heads/main abcdefabcdefabcdefabcdefabcdefabcdefabcd`n"
    ))
Assert-HarnessEqual -Expected 1 -Actual $pushUpdates.Count -Message '应解析一条 pre-push 更新'
Assert-HarnessEqual -Expected 'refs/heads/main' -Actual $pushUpdates[0].LocalRef -Message '应保留本地 ref'

$invalidPushRejected = $false
try {
    [void](ConvertFrom-HarnessPushInput -InputText 'refs/heads/main HEAD refs/heads/main bad')
}
catch {
    $invalidPushRejected = $true
}
Assert-HarnessEqual -Expected $true -Actual $invalidPushRejected -Message '必须拒绝非对象 ID 的 pre-push 输入'

$pushFixtureRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) `
    -ChildPath ('harness-push-fixture-' + [Guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path (Join-Path -Path $pushFixtureRoot -ChildPath 'Components/resource_pack') `
        -Force | Out-Null
    Set-Content -LiteralPath (Join-Path -Path $pushFixtureRoot -ChildPath 'README.md') `
        -Value 'seed' -Encoding ascii
    $git = Get-ExternalCommand -Name 'git'
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $pushFixtureRoot, 'init', '--quiet')
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $pushFixtureRoot, 'add', '--all')
    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $pushFixtureRoot,
        '-c', 'commit.gpgsign=false',
        '-c', 'user.email=harness-push@example.invalid',
        '-c', 'user.name=harness-push',
        'commit', '--quiet', '-m', 'seed'
    )
    $baseCommit = @(Get-HarnessGitOutputLines -RepositoryRoot $pushFixtureRoot `
        -Arguments @('rev-parse', 'HEAD'))[0]
    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $pushFixtureRoot, 'update-ref', 'refs/remotes/origin/main', $baseCommit
    )

    Set-Content -LiteralPath (Join-Path -Path $pushFixtureRoot -ChildPath 'Components/resource_pack/change.c') `
        -Value 'int changed;' -Encoding ascii
    Invoke-ExternalCommand -CommandPath $git -Arguments @('-C', $pushFixtureRoot, 'add', '--all')
    Invoke-ExternalCommand -CommandPath $git -Arguments @(
        '-C', $pushFixtureRoot,
        '-c', 'commit.gpgsign=false',
        '-c', 'user.email=harness-push@example.invalid',
        '-c', 'user.name=harness-push',
        'commit', '--quiet', '-m', 'change'
    )
    $headCommit = @(Get-HarnessGitOutputLines -RepositoryRoot $pushFixtureRoot `
        -Arguments @('rev-parse', 'HEAD'))[0]
    $zeroCommit = '0000000000000000000000000000000000000000'

    $normalUpdate = [pscustomobject]@{ LocalSha = $headCommit; RemoteSha = $baseCommit }
    $normalPaths = @(Get-HarnessPushChangedPaths -RepositoryRoot $pushFixtureRoot `
        -RemoteName origin -Update $normalUpdate)
    Assert-HarnessSequence -Expected @('Components/resource_pack/change.c') -Actual $normalPaths `
        -Message '普通更新应按远端旧 SHA 到本地新 SHA 取路径'

    $newBranchUpdate = [pscustomobject]@{ LocalSha = $headCommit; RemoteSha = $zeroCommit }
    $newBranchPaths = @(Get-HarnessPushChangedPaths -RepositoryRoot $pushFixtureRoot `
        -RemoteName origin -Update $newBranchUpdate)
    Assert-HarnessSequence -Expected @('Components/resource_pack/change.c') -Actual $newBranchPaths `
        -Message '新分支应只收集远端尚不存在的提交路径'

    $deleteUpdate = [pscustomobject]@{ LocalSha = $zeroCommit; RemoteSha = $headCommit }
    $deletePaths = @(Get-HarnessPushChangedPaths -RepositoryRoot $pushFixtureRoot `
        -RemoteName origin -Update $deleteUpdate)
    Assert-HarnessEqual -Expected 0 -Actual $deletePaths.Count -Message '删除 ref 不应触发代码测试'
}
finally {
    if (Test-Path -LiteralPath $pushFixtureRoot) {
        Remove-Item -LiteralPath $pushFixtureRoot -Recurse -Force
    }
}

$env:GIT_DIR = 'Z:/hook-fixture/.git'
$probeEnvironment = Suspend-InheritedGitEnvironment
Assert-HarnessEqual -Expected 'absent' -Actual $(if ($null -eq $env:GIT_DIR) { 'absent' } else { $env:GIT_DIR }) `
    -Message '挂起后夹具不得继承 Hook 导出的 GIT_DIR。'
Restore-InheritedGitEnvironment -Saved $probeEnvironment
Assert-HarnessEqual -Expected 'Z:/hook-fixture/.git' -Actual $env:GIT_DIR -Message '恢复后应还原 GIT_DIR。'
Remove-Item -Path Env:GIT_DIR

Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment

Write-Output 'harness_helpers 路由测试通过。'
