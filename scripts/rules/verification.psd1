@{
    AllHostModules = @(
        'external_loader'
        'flash_ftl'
        'filesystem_locator'
        'gui_task'
        'music_gui'
        'gui_theme'
        'gui_canvas'
        'gui_font'
        'w25qxx'
        'resource_pack'
        'audio_reader'
        'music_catalog'
        'ui_cell'
        'foreground_task'
        'music_foreground'
        'volume'
        'background_task'
        'playback_service'
        'playback_task'
        'monitor_warn'
        'board_test_card'
    )

    # 每条规则描述一个主机测试工程真正编译或覆盖的生产路径。
    HostModuleRules = @(
        @{
            Name = 'external_loader'
            PathPatterns = @(
                '^Tools/external_loader/'
                '^Tests/external_loader/'
            )
        }
        @{
            Name = 'flash_ftl'
            PathPatterns = @(
                '^Components/flash_ftl/'
                # SD Card Component 的显式中止测试（test_sd_abort.c）编译 Components/sd/sd.c。
                '^Components/sd/'
                # SD 读路径整条链（test_sd_removal_chain.c）：真实的 Platform SD 与卷任务的 SD 部分（对账、闸、卷事件），HAL 一侧用头文件替身。
                '^Platform/sd/platform_sd\.c$'
                '^APP/tasks/volume/(?:sd/volume_sd|volume_event|volume_reconcile|volume_task(?:_config)?)\.(?:c|h)$'
                '^Adapters/stm32_hal/irq/stm32_(?:gpio_exti|sdmmc)_irq\.h$'
                '^Adapters/stm32_hal/sd/sd_stm32_hal_adapter\.h$'
                '^Tests/stubs/stm32_hal/'
                '^FATFS/'
                '^Middlewares/Third_Party/FatFs/'
                '^Service/filesystem/'
                '^APP/tasks/volume/benchmark/'
                # 主机测试编译的 Platform/Service/APP 头（由 check-test-paths.ps1 发现）。
                '^Platform/platform\.h$'
                '^Platform/flash/platform_flash\.h$'
                '^Platform/sd/platform_sd\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^APP/app_config\.h$'
                '^Tests/flash_ftl/'
                # 共用 FreeRTOS 桩：flash_ftl 的文件基准用它的假 tick，freertos_stub_tests 测它自己。
                '^Tests/stubs/freertos/'
                # 持锁统计依赖的周期计数器与 Service Log 的主机替身，以及编进测试的周期计数器头。
                '^Tests/stubs/lock_env/'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
            )
        }
        @{
            Name = 'filesystem_locator'
            PathPatterns = @(
                '^Service/filesystem/'
                '^Middlewares/Third_Party/FatFs/'
                # 测试把 patches/fatfs 的补丁应用到 ff.c 副本后编译，并复制 ffconf.h 与 fatfs.h。
                '^patches/fatfs/'
                '^FATFS/'
                # 共用 FreeRTOS 桩，以及主机测试编译的 Platform/Service/APP 头（由 check-test-paths.ps1 发现）。
                '^Tests/stubs/freertos/'
                # 持锁统计依赖的周期计数器与 Service Log 的主机替身，以及编进测试的周期计数器头。
                '^Tests/stubs/lock_env/'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
                '^Platform/platform\.h$'
                '^Platform/flash/platform_flash\.h$'
                '^Platform/sd/platform_sd\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^APP/app_config\.h$'
                '^Tests/filesystem_locator/'
            )
        }
        @{
            Name = 'w25qxx'
            PathPatterns = @(
                '^Components/w25qxx/'
                '^Components/flash_ftl/'
                '^Adapters/bridge/flash_ftl_w25qxx/'
                '^Tests/w25qxx/'
            )
        }
        @{
            Name = 'gui_task'
            PathPatterns = @(
                '^APP/tasks/gui/'
                '^Service/gui/gui_service_input'
                '^Service/gui/gui_service_sleep'
                '^Service/gui/gui_service_config\.h$'
                '^Service/gui/gui_service\.(c|h)$'
                # gui_service_input 的测试编译到的 Platform/Service 聚合头与配置头（由 check-test-paths.ps1 发现）。
                '^Platform/platform\.h$'
                '^Platform/lcd/platform_lcd\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/service\.h$'
                '^Tests/gui_task/'
            )
        }
        @{
            Name = 'music_gui'
            PathPatterns = @(
                '^APP/apps/music/gui/'
                '^Service/gui/main/queue/'
                '^Service/gui/main/transport/'
                # gui_music_transport 的 reducer 测试依赖播放合同。
                '^APP/tasks/playback/playback_contract\.h$'
                # 曲名客户与窗口行测试按界面数据格子的类型构造结果（music_cells.h，带 music_config.h、ui_cell.h 与曲目显示名容量，#218）。
                '^APP/apps/music/music_(?:cells|config)\.h$'
                '^APP/apps/music/catalog/catalog_name\.h$'
                '^APP/ui_cell/ui_cell\.h$'
                '^Tests/music_gui/'
            )
        }
        @{
            Name = 'gui_theme'
            PathPatterns = @(
                '^Service/gui/theme/'
                '^Service/gui/gui_service\.c$'
                '^Service/gui/gui_service\.h$'
                '^Service/gui/main/background/'
                '^Platform/platform\.h$'
                '^Platform/lcd/platform_lcd\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/service\.h$'
                '^Tests/gui_theme/'
            )
        }
        @{
            Name = 'gui_canvas'
            PathPatterns = @(
                '^Service/gui/canvas/'
                '^Service/gui/main/vinyl/'
                '^Service/service\.h$'
                '^Service/gui/gui_service\.h$'
                '^Tests/gui_canvas/'
            )
        }
        @{
            Name = 'gui_font'
            PathPatterns = @(
                '^Service/gui/font/'
                # 解码器按 Component 提供的分段定位读取 bin，并用 Component 的 Metadata 解码口径核对位图上限。
                '^Components/resource_pack/'
                '^Resources/fonts/'
                '^Tests/gui_font/'
            )
        }
        @{
            Name = 'resource_pack'
            PathPatterns = @(
                '^Components/resource_pack/'
                '^Resources/fonts/'
                '^Tests/resource_pack/'
            )
        }
        @{
            Name = 'audio_reader'
            PathPatterns = @(
                # 只有纯函数部分进主机测试（越过标签、失败结果加卷序号的分类、决定等多久、两行新日志的格式，#215）；
                # audio_reader_stream.c、audio_reader_task.c 与块池靠板级验收。
                '^APP/tasks/audio_reader/'
                # audio_reader_result.c 经 audio_reader_stream.h 带入的头文件，类型取自共用桩的头。
                '^Service/filesystem/filesystem_types\.h$'
                '^Service/service\.h$'
                '^Tests/stubs/freertos/'
                '^Tests/audio_reader/'
            )
        }
        @{
            Name = 'music_catalog'
            PathPatterns = @(
                '^APP/apps/music/catalog/'
                # 音乐注册进注册表的后台入口 music_background_step() 接曲库扫描单步（#220），测试经它驱动扫描。
                '^APP/apps/music/music_background\.[ch]$'
                # 曲库发布成功后叫醒前台任务一次（#218）：catalog.c 包含前台任务头，测试里用假的叫醒函数。
                '^APP/tasks/foreground/foreground_task(?:_config)?\.h$'
                # 扫描单步返回 `BACKGROUND_WAIT_FOREVER`（#220）：catalog.c 包含后台任务的配置头。
                '^APP/tasks/background/background_task(?:_config)?\.h$'
                # 按定位符打开的测试以假实现替换 Filesystem / Log Service 与周期计数器 Adapter，只依赖它们的公开头。
                '^Service/filesystem/filesystem_(?:file|directory)\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
                '^Service/service\.h$'
                '^Service/filesystem/filesystem_types\.h$'
                # 曲库读卷状态（volume_status 在测试里用假实现）并用静态互斥量做短锁（共用 FreeRTOS 桩）。
                '^APP/tasks/volume/volume_event\.h$'
                '^Tests/stubs/freertos/'
                '^Components/id3_reader/'
                '^Service/filesystem/filesystem_text\.[ch]$'
                # GBK 查表测试链接真实 cc936.c，并复制 FATFS/Target/ffconf.h。
                '^Middlewares/Third_Party/FatFs/src/(?:ff\.h|integer\.h|option/cc936\.c)$'
                '^FATFS/Target/ffconf\.h$'
                '^Tests/music_catalog/'
            )
        }
        @{
            Name = 'ui_cell'
            PathPatterns = @(
                '^APP/ui_cell/'
                # 真实 ui_cell.c 链接共用 FreeRTOS 桩（互斥锁、任务句柄）。
                '^Tests/stubs/freertos/'
                # 持锁统计（#218）依赖的周期计数器与 Service Log 的主机替身，以及编进测试的周期计数器头与 Service Log 头。
                '^Tests/stubs/lock_env/'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Tests/ui_cell/'
            )
        }
        @{
            # 前台任务（#218）：一圈的判断（foreground_loop，各应用 step 轮流调用、耗时记录与告警）与注册表的 boot_init 遍历，
            # 真实的 foreground_loop.c、app_table_boot.c；链接共用 FreeRTOS 桩与周期计数器、Service Log 替身。
            # 任务创建与入口（foreground_task.c）不进主机测试，由固件构建和上板核对。
            Name = 'foreground_task'
            PathPatterns = @(
                '^APP/tasks/foreground/'
                '^APP/apps/app_table(?:_boot)?\.[ch]$'
                '^Service/gui/gui_service\.h$'
                '^Platform/platform\.h$'
                '^Platform/lcd/platform_lcd\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Tests/stubs/freertos/'
                '^Tests/stubs/lock_env/'
                '^Tests/foreground_task/'
            )
        }
        @{
            # 音乐前台流程（#218）：覆盖、已读的行不重读、缓存命中不读文件、凑齐才发布、代次变化发空窗、失败不缓存、发布后重新发窗；
            # 真实的前台流程、行缓存、格子定义、ui_cell、显示延迟汇总和翻页钩子；曲库读者、曲目文本读取、读标签诊断、卷边沿、块池就绪数和
            # 前台叫醒函数在测试里用假实现，只依赖它们的公开头。窗口与曲名的真实并发、与音频读取争 SD 锁靠板级验收。
            Name = 'music_foreground'
            PathPatterns = @(
                '^APP/apps/music/foreground/'
                '^APP/apps/music/music_(?:cells|config|display_stats|hook_pageturn)\.[ch]$'
                '^APP/apps/music/catalog/catalog(?:_config|_name|_text|_track_text|_track_diag)?\.h$'
                '^APP/ui_cell/'
                '^APP/tasks/foreground/foreground_task(?:_config)?\.h$'
                '^APP/tasks/volume/volume_event\.h$'
                '^APP/tasks/audio_reader/audio_reader_(?:block|config)\.h$'
                '^Components/id3_reader/id3_reader\.h$'
                '^Service/filesystem/filesystem_types\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
                '^Tests/stubs/freertos/'
                '^Tests/stubs/lock_env/'
                '^Tests/music_foreground/'
            )
        }
        @{
            # 卷任务（#211）：对账表、卷事件、SD 部分与 Flash 启动流程，Filesystem Service 的入口用测试内的假实现替换，
            # 只依赖它们的公开头；链接共用 FreeRTOS 桩与 Service Log 替身。任务入口与 Flash 启动任务的创建
            # （volume_task.c、flash/volume_flash_task.c）不进主机测试，由固件构建和上板核对。
            Name = 'volume'
            PathPatterns = @(
                '^APP/tasks/volume/'
                '^APP/app_config\.h$'
                '^Service/filesystem/filesystem_(?:card|service|types)\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Tests/stubs/freertos/'
                '^Tests/stubs/lock_env/'
                '^Tests/volume/'
            )
        }
        @{
            # 后台任务（#213）：一圈的判断（background_loop）与 Flash 压力钩子作业，Filesystem Service 的文件入口和
            # Platform_Flash_GetVolumeDiagnostics 在测试里用假实现，只依赖它们的公开头；链接真实卷事件、共用 FreeRTOS 桩
            # 与 Service Log 替身。任务创建与入口（background_task.c）不进主机测试，由固件构建和上板核对。
            Name = 'background_task'
            PathPatterns = @(
                '^APP/tasks/background/'
                # 作业表从注册表的 `background_step` 拼起（#220）：background_jobs.h 包含注册表头，它再包含 Service/gui 的聚合头（输入类型）。
                '^APP/apps/app_table\.h$'
                '^Service/gui/gui_service\.h$'
                '^Platform/lcd/platform_lcd\.h$'
                '^APP/tasks/volume/volume_(?:event\.(?:c|h)|task_config\.h)$'
                '^APP/app_config\.h$'
                '^Platform/platform\.h$'
                '^Platform/flash/platform_flash\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/filesystem/filesystem_(?:file|service|types)\.h$'
                '^Service/service\.h$'
                '^Service/log/log_service(?:_config)?\.h$'
                '^Tests/stubs/freertos/'
                '^Tests/stubs/lock_env/'
                '^Tests/background_task/'
            )
        }
        @{
            Name = 'playback_service'
            PathPatterns = @(
                '^Service/playback/'
                '^Middlewares/Third_Party/minimp3/'
                '^Platform/platform\.h$'
                '^Platform/audio/platform_audio\.h$'
                '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
                '^Service/service\.h$'
                '^Tests/playback_service/'
            )
        }
        @{
            Name = 'playback_task'
            PathPatterns = @(
                '^APP/tasks/playback/'
                '^Service/playback/playback_service\.h$'
                '^Service/playback/playback_service_config\.h$'
                '^Service/service\.h$'
                '^Tests/playback_task/'
            )
        }
        @{
            Name = 'monitor_warn'
            PathPatterns = @(
                '^APP/tasks/monitor/'
                '^Tests/monitor_warn/'
            )
        }
        @{
            # Python unittest 模块（不是 CMake 工程）：test-host.ps1 按 Kind 跑 TestDirectory 下的 test_*.py，
            # check-test-paths.ps1 不对它做 gcc 依赖检查。依赖 lameenc 的用例缺包时跳过；
            # HasSlowTests 的模块在 CHANGED 带 -SkipSlow，慢用例只在 FULL 跑。
            Name = 'board_test_card'
            Kind = 'Python'
            TestDirectory = 'Tools/board_test_card'
            HasSlowTests = $true
            PathPatterns = @(
                '^Tools/board_test_card/'
                # test_card_seek.py 逐字节核对提交进仓库的 Xing 精度夹具。
                '^Tests/playback_service/fixtures/xing/'
            )
        }
    )

    # 这些文件不会改变固件或验证器行为，可在 CHANGED 层不跑主机测试。
    NoHostTestPathPatterns = @(
        '^(?:docs|\.agents|\.codex|\.claude)/'
        '^(?:AGENTS|CLAUDE|GLOSSARY|README)\.md$'
        '^Tests/README\.md$'
        # PC GUI 模拟器只在 host 上运行，由 run-scenarios.ps1 单独回归，不进固件。
        '^Tools/gui_simulator/'
        '\.md$'
        '^\.gitignore$'
    )

    # 命中后 CHANGED 额外运行 Tools/gui_simulator/run-scenarios.ps1（界面像素回归）；
    # 与主机模块测试的选择互不影响。
    GuiScenarioPathPatterns = @(
        '^Service/gui/'
        '^Tools/gui_simulator/'
        '^Resources/imgs/'
        '^Middlewares/Third_Party/LVGL/'
        # 模拟器链接真实的 ResourcePack Component（Service/gui/font 的解码器依赖它）。
        '^Components/resource_pack/'
        # 模拟器同时编译这些 Service/gui 之外的头文件（替身实现其 Interface）。
        '^Service/service\.h$'
        '^Platform/platform\.h$'
        '^Platform/lcd/platform_lcd\.h$'
        '^Platform/touch/platform_touch\.h$'
        '^Adapters/cortex/cache/cortex_m7_dcache_adapter_config\.h$'
        # 模拟器编译真实 APP 注册表（含 boot_init 遍历）、音乐 GUI 分区、音乐界面数据格子定义与 ui_cell（含持锁统计）、显示延迟汇总和
        # 翻页钩子；fakes/music_sim.c 实现音乐前台流程（注册表的 foreground_step 与 boot_init）和前台叫醒函数，
        # fakes/playback_sim.c 实现播放合同头（FIFO 深度取播放任务配置），fakes/log_sim.c 实现 Service Log 头。
        '^APP/apps/music/gui/'
        '^APP/apps/music/music_(?:app|background|cells|config|display_stats|hook_pageturn)\.h$'
        '^APP/apps/music/music_(?:app|cells|config|display_stats|hook_pageturn)\.c$'
        '^APP/apps/music/foreground/music_foreground\.h$'
        '^APP/apps/app_table(?:_boot)?\.[ch]$'
        '^APP/ui_cell/'
        '^APP/tasks/foreground/foreground_task(?:_config)?\.h$'
        '^APP/tasks/audio_reader/audio_reader_(?:block|config)\.h$'
        '^APP/tasks/playback/playback_(?:contract|task_config)\.h$'
        '^Service/log/log_service(?:_config)?\.h$'
        # 假音乐前台流程的曲目显示名经真实的提取纯函数产生（模拟器编译 catalog_name.c）。
        '^APP/apps/music/catalog/catalog_name\.[ch]$'
        # 模拟器嵌入同一份字体 bin，fakes/resource_sim.c 实现字体查询头，fakes/cycle_counter_sim.c 实现周期计数器头。
        '^Resources/fonts/'
        '^Service/resource/resource_service\.h$'
        '^Adapters/cortex/cycle_counter/cortex_m7_cycle_counter_adapter\.h$'
        '^Service/filesystem/filesystem_types\.h$'
    )

    # 无法映射的可执行、构建或 Harness 变化保守回退到全部主机测试。
    FullHostTestPathPatterns = @(
        '^scripts/'
        '^\.githooks/'
        '^(?:CMakeLists\.txt|CMakePresets\.json)$'
        '^cmake/'
    )

    # 命中后，主机验证仍继续，但最终证据必须标记为需要上板。
    HardwareSensitivePathPatterns = @(
        '^Core/'
        '^Platform/'
        '^Adapters/'
        '^APP/'
        '^Service/'
        '^Components/(?:audio|axp2101|flash_ftl|ft6x36|led|sd|soft_i2c|st7789|w25qxx)/'
        '^FATFS/'
        '^Drivers/'
        '^USB_DEVICE/'
        '^FreeRTOS/'
        '^Middlewares/(?:ST|Third_Party/FreeRTOS)/'
        '^Tools/external_loader/(?:src|include|third_party|cmake)/'
        '^Tools/external_loader/CMakeLists\.txt$'
        '(^|/)startup_[^/]*\.s$'
        '\.ioc$'
        '\.ld$'
    )
}
