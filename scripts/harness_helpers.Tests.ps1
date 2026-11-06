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
Assert-HarnessSequence -Expected @('gui_font', 'resource_pack') -Actual $resourceImpact.HostModules `
    -Message '资源包变化选择资源包主机测试与编译它的字体解码主机测试'

$fontDecoderImpact = Get-HarnessImpact -ChangedPath @('Service/gui/font/gui_service_font_decoder.c')
Assert-HarnessSequence -Expected @('gui_font') -Actual $fontDecoderImpact.HostModules `
    -Message '字体解码器变化必须选择 GUI 字体主机测试'
Assert-HarnessEqual -Expected $true -Actual $fontDecoderImpact.RequiresGuiScenarios -Message '字体解码器位于 Service/gui，须同时跑 GUI 场景回归'

Assert-HarnessEqual -Expected $true -Actual $resourceImpact.RequiresGuiScenarios -Message '模拟器链接真实 ResourcePack Component，其变化须跑 GUI 场景回归'

$fontBinImpact =Get-HarnessImpact -ChangedPath @('Resources/fonts/noto_sans_cjk_sc_12_4bpp.bin')
Assert-HarnessSequence -Expected @('gui_font', 'resource_pack') -Actual $fontBinImpact.HostModules `
    -Message '字体 bin 变化必须选择读取它的资源包与 GUI 字体主机测试'

$docsImpact = Get-HarnessImpact -ChangedPath @('docs/shape/sources/verification.md', 'GLOSSARY.md')
Assert-HarnessEqual -Expected 0 -Actual $docsImpact.HostModules.Count -Message '纯文档变化不跑主机测试'

$simulatorImpact = Get-HarnessImpact -ChangedPath @('Tools/gui_simulator/sim_main.c')
Assert-HarnessEqual -Expected 0 -Actual $simulatorImpact.HostModules.Count -Message 'GUI 模拟器变化不跑固件主机测试'
Assert-HarnessEqual -Expected $false -Actual $simulatorImpact.RequiresAllHostTests -Message 'GUI 模拟器变化不应回退全部测试'
Assert-HarnessEqual -Expected $true -Actual $simulatorImpact.RequiresGuiScenarios -Message 'GUI 模拟器变化必须跑场景回归'

$viewImpact = Get-HarnessImpact -ChangedPath @('Service/gui/view/gui_service_view_music.c')
Assert-HarnessEqual -Expected $true -Actual $viewImpact.RequiresGuiScenarios -Message '界面层变化必须跑场景回归'
$lvConfImpact = Get-HarnessImpact -ChangedPath @('Middlewares/Third_Party/LVGL/lv_conf.h')
Assert-HarnessEqual -Expected $true -Actual $lvConfImpact.RequiresGuiScenarios -Message 'LVGL 配置变化必须跑场景回归'
Assert-HarnessEqual -Expected $false -Actual $docsImpact.RequiresGuiScenarios -Message '纯文档变化不跑场景回归'
Assert-HarnessEqual -Expected $false -Actual $ftlImpact.RequiresGuiScenarios -Message '非 GUI 变化不跑场景回归'
$platformLcdImpact = Get-HarnessImpact -ChangedPath @('Platform/lcd/platform_lcd.h')
Assert-HarnessEqual -Expected $true -Actual $platformLcdImpact.RequiresGuiScenarios -Message '模拟器编译的 Platform LCD 头变化必须跑场景回归'
$serviceHeaderImpact = Get-HarnessImpact -ChangedPath @('Service/service.h')
Assert-HarnessEqual -Expected $true -Actual $serviceHeaderImpact.RequiresGuiScenarios -Message 'Service 状态头变化必须跑场景回归'
$guiMusicImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/gui/gui_music.c')
$appTableImpact = Get-HarnessImpact -ChangedPath @('APP/apps/app_table.c')
Assert-HarnessEqual -Expected $true -Actual $appTableImpact.RequiresGuiScenarios -Message '模拟器编译真实应用注册表，其变化必须跑场景回归'
$appTableHeaderImpact = Get-HarnessImpact -ChangedPath @('APP/apps/app_table.h')
Assert-HarnessEqual -Expected $true -Actual $appTableHeaderImpact.RequiresGuiScenarios -Message '应用注册表头变化必须跑场景回归'
Assert-HarnessEqual -Expected $true -Actual $guiMusicImpact.RequiresGuiScenarios -Message '模拟器编译的 APP 音乐分区变化必须跑场景回归'
$playbackContractGuiImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_contract.h')
Assert-HarnessEqual -Expected $true -Actual $playbackContractGuiImpact.RequiresGuiScenarios -Message '假播放后端实现的合同头变化必须跑场景回归'
$playbackConfigGuiImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_task_config.h')
Assert-HarnessEqual -Expected $true -Actual $playbackConfigGuiImpact.RequiresGuiScenarios -Message '假播放后端引用的命令 FIFO 深度变化必须跑场景回归'
$playbackMachineGuiImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_machine.c')
Assert-HarnessEqual -Expected $false -Actual $playbackMachineGuiImpact.RequiresGuiScenarios -Message '模拟器不编译的播放状态机变化不跑场景回归'
$logHeaderGuiImpact = Get-HarnessImpact -ChangedPath @('Service/log/log_service.h')
Assert-HarnessEqual -Expected $true -Actual $logHeaderGuiImpact.RequiresGuiScenarios -Message '假日志实现的 Service Log 头变化必须跑场景回归'
$listbufferHeaderImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/catalog/storage_listbuffer.h')
Assert-HarnessEqual -Expected $true -Actual $listbufferHeaderImpact.RequiresGuiScenarios -Message '假 Storage 实现的窗口槽头变化必须跑场景回归'
Assert-HarnessEqual -Expected $true -Actual (@($listbufferHeaderImpact.HostModules) -contains 'music_gui') -Message '窗口槽头变化必须选中 music_gui（Queue 行测试按该头构造窗口）'
$namebufferHeaderImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/catalog/storage_namebuffer.h')
Assert-HarnessEqual -Expected $true -Actual $namebufferHeaderImpact.RequiresGuiScenarios -Message '假 Storage 实现的名称槽头变化必须跑场景回归'
$namebufferSourceImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/catalog/storage_namebuffer.c')
Assert-HarnessEqual -Expected $false -Actual $namebufferSourceImpact.RequiresGuiScenarios -Message '模拟器不编译的名称槽实现变化不跑场景回归'
$listbufferSourceImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/catalog/storage_listbuffer.c')
Assert-HarnessEqual -Expected $false -Actual $listbufferSourceImpact.RequiresGuiScenarios -Message '模拟器不编译的窗口槽实现变化不跑场景回归'
$platformLcdSourceImpact = Get-HarnessImpact -ChangedPath @('Platform/lcd/platform_lcd.c')
Assert-HarnessEqual -Expected $false -Actual $platformLcdSourceImpact.RequiresGuiScenarios -Message '模拟器不编译的 Platform 实现变化不跑场景回归'

# 旧提交快照的规则可能没有 GuiScenarioPathPatterns；缺键时不得报错，也不跑场景回归。
$rulesWithoutGui = Get-HarnessRules
$rulesWithoutGui.Remove('GuiScenarioPathPatterns')
$legacyImpact = Get-HarnessImpact -ChangedPath @('Service/gui/view/gui_service_view_music.c') -Rules $rulesWithoutGui
Assert-HarnessEqual -Expected $false -Actual $legacyImpact.RequiresGuiScenarios -Message '规则缺场景键时不跑场景回归'

$agentConfigImpact = Get-HarnessImpact -ChangedPath @('CLAUDE.md', 'Service/CLAUDE.md', '.claude/settings.json', '.codex/hooks.json')
Assert-HarnessEqual -Expected 0 -Actual $agentConfigImpact.HostModules.Count -Message 'Agent 配置变化不跑主机测试'

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
Assert-HarnessSequence -Expected @('music_gui') -Actual $queueImpact.HostModules `
    -Message 'Queue 行 Module 变化必须选择音乐 GUI 主机测试'

$musicImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/gui/gui_music.c')
Assert-HarnessSequence -Expected @('music_gui') -Actual $musicImpact.HostModules `
    -Message '音乐 GUI 分区变化必须选择对应主机测试'

$inputImpact = Get-HarnessImpact -ChangedPath @('Service/gui/gui_service_input.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $inputImpact.HostModules `
    -Message 'GUI 输入单槽变化必须选择 GUI Task 主机测试'

$sleepImpact = Get-HarnessImpact -ChangedPath @('Service/gui/gui_service_sleep.c')
Assert-HarnessSequence -Expected @('gui_task') -Actual $sleepImpact.HostModules `
    -Message 'GUI 睡眠时长计算变化必须选择 GUI Task 主机测试'

$transportImpact = Get-HarnessImpact -ChangedPath @('Service/gui/main/transport/gui_service_main_transport.c')
Assert-HarnessSequence -Expected @('music_gui') -Actual $transportImpact.HostModules `
    -Message 'Now Playing 三键 Module 变化必须选择音乐 GUI 主机测试'

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

$playbackImpact = Get-HarnessImpact -ChangedPath @('Service/playback/playback_service_decoder_minimp3.c')
Assert-HarnessSequence -Expected @('playback_service') -Actual $playbackImpact.HostModules `
    -Message '播放模块解码变化必须选择播放模块主机测试'

$minimp3Impact = Get-HarnessImpact -ChangedPath @('Middlewares/Third_Party/minimp3/minimp3.h')
Assert-HarnessSequence -Expected @('playback_service') -Actual $minimp3Impact.HostModules `
    -Message 'minimp3 升级必须选择播放模块主机测试'

$audioRangeImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/audio_reader/audio_reader_range.c')
Assert-HarnessSequence -Expected @('audio_reader') -Actual $audioRangeImpact.HostModules `
    -Message '越过标签纯函数变化必须选择音频读取任务主机测试'

$audioStreamImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/audio_reader/audio_reader_stream.c')
Assert-HarnessSequence -Expected @('audio_reader') -Actual $audioStreamImpact.HostModules `
    -Message '音频读取任务目录变化必须选择音频读取任务主机测试'

# 音频读取任务（#215）：纯函数（决定等多久、失败结果加卷序号、日志格式）、任务入口和测试用断言头都选中 audio_reader，不落入未知路径。
$audioReaderImpact = Get-HarnessImpact -ChangedPath @(
    'APP/tasks/audio_reader/audio_reader_pace.c'
    'APP/tasks/audio_reader/audio_reader_diag.c'
    'APP/tasks/audio_reader/audio_reader_task.c'
    'APP/tasks/audio_reader/audio_reader_config.h'
    'Tests/audio_reader/host_test_assert.h')
Assert-HarnessSequence -Expected @('audio_reader') -Actual $audioReaderImpact.HostModules `
    -Message '音频读取任务的纯函数、任务入口、配置头和测试用断言头变化只选择音频读取任务主机测试'
Assert-HarnessEqual -Expected 0 -Actual $audioReaderImpact.UnknownPaths.Count `
    -Message 'APP/tasks/audio_reader 与 Tests/audio_reader 由主机模块规则覆盖，不应落入未知路径'

$catalogNameImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/catalog/catalog_name.c')
Assert-HarnessSequence -Expected @('music_catalog') -Actual $catalogNameImpact.HostModules `
    -Message '曲目显示名纯函数变化必须选择音乐曲库主机测试'

$catalogTextImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/catalog/catalog_text.c')
Assert-HarnessSequence -Expected @('music_catalog') -Actual $catalogTextImpact.HostModules `
    -Message '曲目文本纯函数变化必须选择音乐曲库主机测试'

$catalogOpenImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/catalog/catalog_track_open.c', 'APP/apps/music/catalog/catalog.c', 'APP/apps/music/catalog/catalog_track_diag.c')
Assert-HarnessSequence -Expected @('music_catalog') -Actual $catalogOpenImpact.HostModules `
    -Message '曲库扫描、按定位符打开与打开诊断变化必须选择音乐曲库主机测试'

$id3ReaderImpact = Get-HarnessImpact -ChangedPath @('Components/id3_reader/id3_reader.c')
Assert-HarnessSequence -Expected @('music_catalog') -Actual $id3ReaderImpact.HostModules `
    -Message 'id3_reader Component 变化必须选择音乐曲库主机测试（它只经曲目文本函数被测）'

$cc936Impact = Get-HarnessImpact -ChangedPath @('Middlewares/Third_Party/FatFs/src/option/cc936.c')
Assert-HarnessSequence -Expected @('filesystem_locator', 'flash_ftl', 'music_catalog') -Actual $cc936Impact.HostModules `
    -Message 'CP936 表变化必须同时选择文件定位符、Flash 与音乐曲库主机测试（后者链接真实 cc936.c 做 GBK 查表）'

$catalogConfigImpact = Get-HarnessImpact -ChangedPath @('APP/apps/music/catalog/catalog_config.h')
Assert-HarnessSequence -Expected @('music_catalog') -Actual $catalogConfigImpact.HostModules `
    -Message '曲库配置头（含曲目标签读取常量与持锁统计开关）变化必须选择音乐曲库主机测试'

$storageTaskHeaderImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/storage/storage_task.h')
Assert-HarnessEqual -Expected $true -Actual (@($storageTaskHeaderImpact.HostModules) -contains 'music_catalog') `
    -Message 'Storage 任务头变化必须选中音乐曲库主机测试（窗口槽与名字槽经它带入状态类型）'

$sdComponentImpact = Get-HarnessImpact -ChangedPath @('Components/sd/sd.c')
Assert-HarnessSequence -Expected @('flash_ftl') -Actual $sdComponentImpact.HostModules `
    -Message 'SD Card Component 变化必须选择 flash_ftl 主机测试（显式中止的状态机测试在其中）'

$sdChainImpact = Get-HarnessImpact -ChangedPath @('Platform/sd/platform_sd.c', 'APP/tasks/volume/sd/volume_sd.c')
Assert-HarnessEqual -Expected $true -Actual (@($sdChainImpact.HostModules) -contains 'flash_ftl') `
    -Message 'Platform SD 与卷任务的 SD 部分变化必须选择 flash_ftl 主机测试（读时拔卡整条链的测试在其中）'

# 卷任务（#211）：生产路径与测试目录选中 volume 主机测试；SD 部分同时选中 flash_ftl（整条链）；不落入未知路径。
$volumeImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/volume/volume_reconcile.c', 'Tests/volume/test_volume_sd.c')
Assert-HarnessSequence -Expected @('flash_ftl', 'volume') -Actual $volumeImpact.HostModules `
    -Message '卷任务的判断与卷事件变化必须选择 volume 与 flash_ftl（整条链编译了它们）主机测试'

# 卷事件公开头（#212）：曲库读卷序号，音乐曲库主机测试以假实现替换 volume_status() 但包含这个头。
$volumeEventHeaderImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/volume/volume_event.h')
Assert-HarnessSequence -Expected @('background_task', 'flash_ftl', 'music_catalog', 'volume') -Actual $volumeEventHeaderImpact.HostModules `
    -Message '卷事件公开头变化必须选择 volume、flash_ftl（整条链）、音乐曲库与后台任务（包含该头）主机测试'
$volumeTestImpact = Get-HarnessImpact -ChangedPath @('Tests/volume/test_volume_flash.c')
Assert-HarnessSequence -Expected @('volume') -Actual $volumeTestImpact.HostModules `
    -Message 'Tests/volume 变化只选择 volume 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $volumeImpact.UnknownPaths.Count `
    -Message 'APP/tasks/volume 由主机模块规则覆盖，不应落入未知生产路径'
$volumeSdImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/volume/sd/volume_sd.c')
Assert-HarnessSequence -Expected @('flash_ftl', 'volume') -Actual $volumeSdImpact.HostModules `
    -Message '卷任务的 SD 部分变化必须同时选择 volume 与 flash_ftl（读时拔卡整条链）主机测试'
$volumeFlashImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/volume/flash/volume_flash.c', 'APP/tasks/volume/flash/volume_flash_task.c')
Assert-HarnessSequence -Expected @('volume') -Actual $volumeFlashImpact.HostModules `
    -Message 'Flash 启动流程变化必须选择 volume 主机测试'

# 后台任务（#213）：一圈的判断与 Flash 压力钩子作业。生产路径、测试目录各自选中 background_task；
# 卷事件头文件被它编译（见上）；任务创建（background_task.c）也在该目录内，改它同样选中；不落入未知路径。
$backgroundImpact = Get-HarnessImpact -ChangedPath @(
    'APP/tasks/background/background_loop.c'
    'APP/tasks/background/background_flash_pressure.c'
    'APP/tasks/background/background_task.c'
    'APP/tasks/background/background_task_config.h'
    'Tests/background_task/test_background_loop.c')
Assert-HarnessSequence -Expected @('background_task') -Actual $backgroundImpact.HostModules `
    -Message '后台任务变化必须选择 background_task 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $backgroundImpact.UnknownPaths.Count `
    -Message 'APP/tasks/background 由主机模块规则覆盖，不应落入未知生产路径'
$volumeTaskConfigImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/volume/volume_task_config.h')
Assert-HarnessSequence -Expected @('background_task', 'flash_ftl', 'volume') -Actual $volumeTaskConfigImpact.HostModules `
    -Message '卷任务配置头被后台任务主机测试包含（通过卷事件头），变化必须一并选中'

$monitorWarnImpact =Get-HarnessImpact -ChangedPath @('APP/tasks/monitor/monitor_warn.c')
Assert-HarnessSequence -Expected @('monitor_warn') -Actual $monitorWarnImpact.HostModules `
    -Message 'Monitor 变化选择 monitor_warn 主机测试'

$playbackMachineImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_machine.c')
Assert-HarnessSequence -Expected @('playback_task') -Actual $playbackMachineImpact.HostModules `
    -Message '播放任务状态机变化必须选择播放任务主机测试'

$playbackConfigHostImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_task_config.h')
Assert-HarnessEqual -Expected $false -Actual (@($playbackConfigHostImpact.HostModules) -contains 'music_catalog') `
    -Message 'T_data 变化不再选中音乐曲库主机测试（单行预算是 1 s 的日志阈值，不再与 T_data 比较）'
Assert-HarnessEqual -Expected $true -Actual (@($playbackConfigHostImpact.HostModules) -contains 'playback_task') `
    -Message 'T_data 变化必须选中播放任务主机测试'

$playbackContractImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/playback/playback_contract.h')
Assert-HarnessSequence -Expected @('music_gui', 'playback_task') -Actual $playbackContractImpact.HostModules `
    -Message '播放任务公开合同变化必须选择播放任务与编译它的音乐 GUI（传输 reducer）主机测试'

$playbackServiceInterfaceImpact = Get-HarnessImpact -ChangedPath @('Service/playback/playback_service.h')
Assert-HarnessSequence -Expected @('playback_service', 'playback_task') -Actual $playbackServiceInterfaceImpact.HostModules `
    -Message '播放模块公开头变化必须同时选择播放模块与播放任务主机测试'

# 主机测试编译的跨层聚合头与配置头：由 check-test-paths.ps1 按真实依赖发现，路由必须与之一致。
$serviceHeaderHostImpact = Get-HarnessImpact -ChangedPath @('Service/service.h')
Assert-HarnessSequence -Expected @('audio_reader', 'background_task', 'filesystem_locator', 'flash_ftl', 'gui_canvas', 'gui_task', 'gui_theme', 'music_catalog', 'music_gui', 'playback_service', 'playback_task', 'volume') `
    -Actual $serviceHeaderHostImpact.HostModules -Message 'Service 聚合头变化必须选择所有编译它的主机测试'

$platformHeaderHostImpact = Get-HarnessImpact -ChangedPath @('Platform/platform.h')
Assert-HarnessSequence -Expected @('background_task', 'filesystem_locator', 'flash_ftl', 'gui_task', 'gui_theme', 'playback_service') `
    -Actual $platformHeaderHostImpact.HostModules -Message 'Platform 聚合头变化必须选择所有编译它的主机测试'

$platformLcdHostImpact = Get-HarnessImpact -ChangedPath @('Platform/lcd/platform_lcd.h')
Assert-HarnessSequence -Expected @('gui_task', 'gui_theme') -Actual $platformLcdHostImpact.HostModules `
    -Message 'Platform LCD 头变化必须选择 GUI Task 与主题主机测试'

$platformAudioHostImpact = Get-HarnessImpact -ChangedPath @('Platform/audio/platform_audio.h')
Assert-HarnessSequence -Expected @('playback_service') -Actual $platformAudioHostImpact.HostModules `
    -Message 'Platform 音频头变化必须选择播放模块主机测试'

$platformFlashHostImpact = Get-HarnessImpact -ChangedPath @('Platform/flash/platform_flash.h', 'Platform/sd/platform_sd.h')
Assert-HarnessSequence -Expected @('background_task', 'filesystem_locator', 'flash_ftl') -Actual $platformFlashHostImpact.HostModules `
    -Message 'Platform Flash/SD 头变化必须选择 FTL 与后台任务（钩子读 FTL 诊断）主机测试'

$dcacheConfigHostImpact = Get-HarnessImpact -ChangedPath @('Adapters/cortex/cache/cortex_m7_dcache_adapter_config.h')
Assert-HarnessSequence -Expected @('background_task', 'filesystem_locator', 'flash_ftl', 'gui_task', 'gui_theme', 'playback_service') `
    -Actual $dcacheConfigHostImpact.HostModules -Message 'DCache 配置头变化必须选择所有编译它的主机测试'

$logHeaderHostImpact = Get-HarnessImpact -ChangedPath @('Service/log/log_service.h', 'Service/log/log_service_config.h', 'APP/app_config.h')
Assert-HarnessSequence -Expected @('background_task', 'filesystem_locator', 'flash_ftl', 'music_catalog', 'volume') -Actual $logHeaderHostImpact.HostModules `
    -Message 'Service Log 头与 APP 配置头变化必须选择 FTL 主机测试'

$fatfsPatchImpact = Get-HarnessImpact -ChangedPath @('patches/fatfs/0001-open-by-dirent.patch')
Assert-HarnessSequence -Expected @('filesystem_locator') -Actual $fatfsPatchImpact.HostModules `
    -Message 'FatFs 补丁文件变化只选择应用它的文件定位符主机测试'
Assert-HarnessEqual -Expected 0 -Actual $fatfsPatchImpact.UnknownPaths.Count `
    -Message 'patches/fatfs 由文件定位符模块的路径规则覆盖，不应落入未知生产路径'

$filesystemHostImpact = Get-HarnessImpact -ChangedPath @('Service/filesystem/filesystem_file_locator.c', 'Middlewares/Third_Party/FatFs/src/ff.c')
Assert-HarnessSequence -Expected @('filesystem_locator', 'flash_ftl') -Actual $filesystemHostImpact.HostModules `
    -Message 'Filesystem Service 与 FatFs 变化必须同时选择文件定位符与 FTL 主机测试'

# 共用 FreeRTOS 桩（Tests/stubs/freertos）被 flash_ftl、filesystem_locator、ui_cell、volume、music_catalog 与 audio_reader（只取头）编译：改桩必须选中它们，且不落入未知路径。
$freertosStubImpact = Get-HarnessImpact -ChangedPath @(
    'Tests/stubs/freertos/freertos_stub.c'
    'Tests/stubs/freertos/freertos_stub.h'
    'Tests/stubs/freertos/Middlewares/Third_Party/FreeRTOS/Source/include/semphr.h')
Assert-HarnessSequence -Expected @('audio_reader', 'background_task', 'filesystem_locator', 'flash_ftl', 'music_catalog', 'ui_cell', 'volume') -Actual $freertosStubImpact.HostModules `
    -Message '共用 FreeRTOS 桩变化必须选择 audio_reader、background_task、filesystem_locator、flash_ftl、music_catalog、ui_cell 与 volume 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $freertosStubImpact.UnknownPaths.Count `
    -Message 'Tests/stubs/freertos 由主机模块规则覆盖，不应落入未知路径'
Assert-HarnessEqual -Expected $false -Actual $freertosStubImpact.RequiresAllHostTests `
    -Message '共用 FreeRTOS 桩变化不应回退全部主机测试'

# Filesystem 持锁统计在主机上依赖的周期计数器与 Service Log 替身（Tests/stubs/lock_env，#208）被 flash_ftl、
# filesystem_locator 编译，volume 只用其中的 Service Log 替身（#211）：改它们必须选中这三个模块，且不落入未知路径。
$lockEnvStubImpact = Get-HarnessImpact -ChangedPath @(
    'Tests/stubs/lock_env/lock_env_stub.h'
    'Tests/stubs/lock_env/lock_env_cycle_counter.c'
    'Tests/stubs/lock_env/lock_env_log.c')
Assert-HarnessSequence -Expected @('background_task', 'filesystem_locator', 'flash_ftl', 'volume') -Actual $lockEnvStubImpact.HostModules `
    -Message '持锁统计的主机替身变化必须选择 background_task、filesystem_locator、flash_ftl 与 volume 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $lockEnvStubImpact.UnknownPaths.Count `
    -Message 'Tests/stubs/lock_env 由主机模块规则覆盖，不应落入未知路径'

# Filesystem 的入口锁模块（filesystem_lock.[ch]）在 Service/filesystem 下，已由两个模块的规则覆盖。
$filesystemLockImpact = Get-HarnessImpact -ChangedPath @('Service/filesystem/filesystem_lock.c', 'Service/filesystem/filesystem_lock.h')
Assert-HarnessSequence -Expected @('filesystem_locator', 'flash_ftl') -Actual $filesystemLockImpact.HostModules `
    -Message 'Filesystem 入口锁变化必须同时选择文件定位符与 FTL 主机测试'

# 音频读取任务对失败结果加卷序号的分类（#211 的 storage_audio_result，#215 改名 audio_reader_result）：生产路径、其头文件依赖与共用桩的头选中 audio_reader。
$audioResultImpact = Get-HarnessImpact -ChangedPath @('APP/tasks/audio_reader/audio_reader_result.c', 'Service/filesystem/filesystem_types.h')
Assert-HarnessEqual -Expected $true -Actual (@($audioResultImpact.HostModules) -contains 'audio_reader') `
    -Message '音频读取结果分类及其依赖的 Filesystem 类型头变化必须选择 audio_reader 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $audioResultImpact.UnknownPaths.Count `
    -Message 'APP/tasks/audio_reader 由主机模块规则覆盖，不应落入未知生产路径'

# ui_cell（#217）是 APP 私有的界面数据短锁模块：生产路径与测试目录各自选中 ui_cell，不落入未知路径。
$uiCellImpact = Get-HarnessImpact -ChangedPath @('APP/ui_cell/ui_cell.c', 'APP/ui_cell/ui_cell.h', 'Tests/ui_cell/test_ui_cell.c')
Assert-HarnessSequence -Expected @('ui_cell') -Actual $uiCellImpact.HostModules `
    -Message 'ui_cell 变化必须选择 ui_cell 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $uiCellImpact.UnknownPaths.Count `
    -Message 'APP/ui_cell 由主机模块规则覆盖，不应落入未知生产路径'

# 样本卡生成器的 Python 测试是主机模块 board_test_card（#183）：改该目录由 CHANGED 选中，不再落入未知路径。
$boardCardImpact = Get-HarnessImpact -ChangedPath @('Tools/board_test_card/card_glyphs.py', 'Tools/board_test_card/test_card_names.py')
Assert-HarnessSequence -Expected @('board_test_card') -Actual $boardCardImpact.HostModules `
    -Message '样本卡生成器及其测试变化选择 board_test_card 主机测试'
Assert-HarnessEqual -Expected 0 -Actual $boardCardImpact.UnknownPaths.Count `
    -Message 'Tools/board_test_card 由 board_test_card 规则覆盖，不应落入未知生产路径'
Assert-HarnessEqual -Expected $false -Actual $boardCardImpact.RequiresAllHostTests `
    -Message '样本卡生成器变化不应回退全部主机测试'
Assert-HarnessEqual -Expected $false -Actual $boardCardImpact.RequiresHardware `
    -Message '样本卡生成器是 PC 工具，不要求上板'
$boardCardFixtureImpact = Get-HarnessImpact -ChangedPath @('Tests/playback_service/fixtures/xing/vbr_toc.mp3')
Assert-HarnessSequence -Expected @('board_test_card', 'playback_service') -Actual $boardCardFixtureImpact.HostModules `
    -Message 'Xing 精度夹具同时被 playback_service 测试与 test_card_seek.py 的逐字节核对使用'
$boardCardReadmeImpact = Get-HarnessImpact -ChangedPath @('Tools/board_test_card/README.md')
Assert-HarnessSequence -Expected @() -Actual $boardCardReadmeImpact.HostModules `
    -Message '样本卡 README 是纯文档，不选主机测试'

# Python 模块的属性由规则声明，脚本不靠 Tools/<名> 约定或硬编码模块名（HAR-11）。
$syntheticRules = @{
    AllHostModules = @('cm', 'py_slow', 'py_fast')
    HostModuleRules = @(
        @{ Name = 'cm'; PathPatterns = @('^Lib/') }
        @{ Name = 'py_slow'; Kind = 'Python'; TestDirectory = 'Tools/elsewhere/tests'; HasSlowTests = $true; PathPatterns = @('^Tools/elsewhere/') }
        @{ Name = 'py_fast'; Kind = 'Python'; TestDirectory = 'Tools/fast'; PathPatterns = @('^Tools/fast/') }
    )
}
Assert-HarnessSequence -Expected @('py_slow', 'py_fast') -Actual (Get-HarnessPythonModuleRule -Rules $syntheticRules | ForEach-Object { $_.Name }) `
    -Message 'Kind = Python 的规则才是 Python 模块'
Assert-HarnessSequence -Expected @('py_slow') -Actual (Get-HarnessSlowTestModuleName -Rules $syntheticRules) `
    -Message '只有声明 HasSlowTests 的模块是慢测试模块'
Assert-HarnessEqual -Expected 'Tools/elsewhere/tests' -Actual (Get-HarnessPythonTestDirectory -ModuleRule (Get-HarnessPythonModuleRule -Rules $syntheticRules | Select-Object -First 1)) `
    -Message 'Python 模块的测试目录来自规则的 TestDirectory'
$noPython = @{ AllHostModules = @('cm'); HostModuleRules = @(@{ Name = 'cm'; PathPatterns = @('^Lib/') }) }
Assert-HarnessEqual -Expected 0 -Actual @(Get-HarnessPythonModuleRule -Rules $noPython).Count -Message '没有 Python 模块时返回空'
Assert-HarnessEqual -Expected 0 -Actual @(Get-HarnessSlowTestModuleName -Rules $noPython).Count -Message '没有慢测试模块时返回空'
$missingDirectory = @{ AllHostModules = @('py'); HostModuleRules = @(@{ Name = 'py'; Kind = 'Python'; PathPatterns = @('^Tools/py/') }) }
$thrown = $null
try { Get-HarnessPythonTestDirectory -ModuleRule $missingDirectory.HostModuleRules[0] | Out-Null } catch { $thrown = $_.Exception.Message }
if (($null -eq $thrown) -or ($thrown -notmatch 'TestDirectory')) { throw "Python 模块缺 TestDirectory 必须报错，实际 '$thrown'" }
$realRules = Get-HarnessRules
$realCard = @(Get-HarnessPythonModuleRule -Rules $realRules | Where-Object { $_.Name -eq 'board_test_card' })
Assert-HarnessEqual -Expected 1 -Actual $realCard.Count -Message '真实规则里 board_test_card 是 Python 模块'
Assert-HarnessEqual -Expected 'Tools/board_test_card' -Actual (Get-HarnessPythonTestDirectory -ModuleRule $realCard[0]) -Message '真实规则声明样本卡测试目录'
Assert-HarnessSequence -Expected @('board_test_card') -Actual (Get-HarnessSlowTestModuleName -Rules $realRules) `
    -Message '真实规则声明样本卡有慢用例'

$harnessImpact = Get-HarnessImpact -ChangedPath @('scripts/verify_changed.ps1')
Assert-HarnessSequence -Expected @('audio_reader', 'background_task', 'board_test_card', 'external_loader', 'filesystem_locator', 'flash_ftl', 'gui_canvas', 'gui_font', 'gui_task', 'gui_theme', 'monitor_warn', 'music_catalog', 'music_gui', 'playback_service', 'playback_task', 'resource_pack', 'ui_cell', 'volume', 'w25qxx') -Actual $harnessImpact.HostModules `
    -Message 'Harness 变化必须回退全部主机测试'
Assert-HarnessEqual -Expected $true -Actual $harnessImpact.RequiresAllHostTests -Message 'Harness 变化应标记全部测试'

$doxygenCheckImpact = Get-HarnessImpact -ChangedPath @('scripts/check-doxygen.ps1', 'scripts/check-doxygen.Tests.ps1')
Assert-HarnessEqual -Expected $true -Actual $doxygenCheckImpact.RequiresAllHostTests `
    -Message 'Doxygen 检查脚本及自测属于 Harness 变化，回退全部主机测试'
Assert-HarnessEqual -Expected 0 -Actual $doxygenCheckImpact.UnknownPaths.Count `
    -Message 'Doxygen 检查脚本由 scripts/ 前缀规则覆盖，不应落入未知生产路径'
Assert-HarnessEqual -Expected $false -Actual $doxygenCheckImpact.RequiresHardware `
    -Message 'Doxygen 检查脚本不要求上板'

$unknownImpact = Get-HarnessImpact -ChangedPath @('Service/unmapped/player.c')
Assert-HarnessSequence -Expected @('audio_reader', 'background_task', 'board_test_card', 'external_loader', 'filesystem_locator', 'flash_ftl', 'gui_canvas', 'gui_font', 'gui_task', 'gui_theme', 'monitor_warn', 'music_catalog', 'music_gui', 'playback_service', 'playback_task', 'resource_pack', 'ui_cell', 'volume', 'w25qxx') -Actual $unknownImpact.HostModules `
    -Message '未知生产路径不得以零测试通过'
Assert-HarnessSequence -Expected @('Service/unmapped/player.c') -Actual $unknownImpact.UnknownPaths `
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

# 静默步骤：成功时不输出，失败时输出缓冲的完整细节并抛错（退出码不变）。
$quietRoot = Join-Path -Path ([System.IO.Path]::GetTempPath()) -ChildPath ('quiet-step-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $quietRoot -Force | Out-Null
try {
    $okScript = Join-Path -Path $quietRoot -ChildPath 'ok.ps1'
    [System.IO.File]::WriteAllText($okScript, "Write-Output '噪声-成功'`n[Console]::Error.WriteLine('噪声-成功-stderr')`n", (New-Object System.Text.UTF8Encoding $true))
    $okOutput = @(Invoke-HarnessQuietStep -ScriptPath $okScript)
    Assert-HarnessEqual -Expected 0 -Actual $okOutput.Count -Message '静默步骤成功时不应输出任何内容'

    $paramScript = Join-Path -Path $quietRoot -ChildPath 'param.ps1'
    [System.IO.File]::WriteAllText($paramScript, "param([string[]]`$Module, [switch]`$Flag)`nif ((`$Module -join ',') -ne 'a,b''c' -or -not `$Flag) { exit 4 }`n", (New-Object System.Text.UTF8Encoding $true))
    $paramOutput = @(Invoke-HarnessQuietStep -ScriptPath $paramScript -Parameters @{ Module = @('a', "b'c"); Flag = $true })
    Assert-HarnessEqual -Expected 0 -Actual $paramOutput.Count -Message '静默步骤应按参数表展开数组与开关'

    # 带 [CmdletBinding()] 的脚本不会把单元素数组压成 [string]；标量值必须按标量字面量传入。
    $scalarScript = Join-Path -Path $quietRoot -ChildPath 'scalar.ps1'
    [System.IO.File]::WriteAllText($scalarScript, "[CmdletBinding()]`nparam([string]`$Dir, [string[]]`$Module, [string[]]`$Single, [switch]`$Flag)`nif (`$Dir -ne 'E:\p ath\it''s' -or (`$Module -join ',') -ne 'a,b' -or `$Single.Count -ne 1 -or `$Single[0] -ne 'x' -or -not `$Flag) { exit 5 }`n", (New-Object System.Text.UTF8Encoding $true))
    $scalarOutput = @(Invoke-HarnessQuietStep -ScriptPath $scalarScript -Parameters @{ Dir = "E:\p ath\it's"; Module = @('a', 'b'); Single = @('x'); Flag = $true })
    Assert-HarnessEqual -Expected 0 -Actual $scalarOutput.Count -Message '静默步骤应把单个字符串按标量传给 CmdletBinding 脚本的 [string] 参数，并保留单元素数组'

    $failScript = Join-Path -Path $quietRoot -ChildPath 'fail.ps1'
    [System.IO.File]::WriteAllText($failScript, "Write-Output '失败细节-stdout'`n[Console]::Error.WriteLine('失败细节-stderr')`nexit 3`n", (New-Object System.Text.UTF8Encoding $true))
    $failMessage = $null
    try {
        Invoke-HarnessQuietStep -ScriptPath $failScript
    }
    catch {
        $failMessage = $_.Exception.Message
    }
    Assert-HarnessEqual -Expected $true -Actual ($null -ne $failMessage) -Message '静默步骤失败时必须抛错以保持非零退出'
    Assert-HarnessEqual -Expected $true -Actual ($failMessage -like '*退出码：3*') -Message '失败消息应带原退出码'
    $failText = $failMessage
    Assert-HarnessEqual -Expected $true -Actual ($failText -like '*失败细节-stdout*') -Message '失败时应输出缓冲的标准输出'
    Assert-HarnessEqual -Expected $true -Actual ($failText -like '*失败细节-stderr*') -Message '失败时应输出缓冲的标准错误'
}
finally {
    Remove-Item -LiteralPath $quietRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Restore-InheritedGitEnvironment -Saved $inheritedGitEnvironment

Write-Output 'harness_helpers 路由测试通过。'
