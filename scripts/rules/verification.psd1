@{
    AllHostModules = @(
        'external_loader'
        'flash_ftl'
        'gui_task'
        'gui_theme'
        'gui_canvas'
        'w25qxx'
        'resource_pack'
        'storage_catalog'
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
                '^FATFS/'
                '^Middlewares/Third_Party/FatFs/'
                '^Service/filesystem/'
                '^APP/tasks/storage/benchmark/'
                '^Tests/flash_ftl/'
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
                '^Service/gui/gui_service\.(c|h)$'
                '^Service/gui/main/queue/'
                '^Service/gui/main/transport/'
                '^Tests/gui_task/'
            )
        }
        @{
            Name = 'gui_theme'
            PathPatterns = @(
                '^Service/gui/theme/'
                '^Service/gui/gui_service\.c$'
                '^Service/gui/gui_service\.h$'
                '^Service/gui/main/background/'
                '^Tests/gui_theme/'
            )
        }
        @{
            Name = 'gui_canvas'
            PathPatterns = @(
                '^Service/gui/canvas/'
                '^Service/gui/main/vinyl/'
                '^Service/gui/gui_service\.h$'
                '^Tests/gui_canvas/'
            )
        }
        @{
            Name = 'resource_pack'
            PathPatterns = @(
                '^Components/resource_pack/'
                '^Tests/resource_pack/'
            )
        }
        @{
            Name = 'storage_catalog'
            PathPatterns = @(
                '^APP/tasks/storage/catalog/'
                '^Tests/storage_catalog/'
            )
        }
    )

    # 这些文件不会改变固件或验证器行为，可在 CHANGED 层不跑主机测试。
    NoHostTestPathPatterns = @(
        '^(?:docs|\.agents|\.codex)/'
        '^(?:AGENTS|CONTEXT|CURRENT|README)\.md$'
        '^Tests/README\.md$'
        '\.md$'
        '^\.gitignore$'
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
