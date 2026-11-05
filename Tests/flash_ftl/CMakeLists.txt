cmake_minimum_required(VERSION 3.22)
project(flash_ftl_tests LANGUAGES C)
enable_testing()
add_executable(flash_ftl_tests test_flash_ftl.c ../../Components/flash_ftl/flash_ftl.c)
target_include_directories(flash_ftl_tests PRIVATE ../..)
target_compile_features(flash_ftl_tests PRIVATE c_std_11)
target_compile_options(flash_ftl_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME flash_ftl_behavior COMMAND flash_ftl_tests)



# 复制实际配置，仅移除不参与 FatFs 算法的 MCU/BSP 头文件依赖。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/ffconf.h" HOST_FFCONF)
foreach(HEADER main.h stm32h7xx_hal.h bsp_driver_sd.h)
    string(REPLACE "#include \"${HEADER}\"" "" HOST_FFCONF "${HOST_FFCONF}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/ffconf.h" "${HOST_FFCONF}")
include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}/host_config")
# 使用工程实际 FatFs 配置与生成 Glue；硬件后端在 BSP 契约处替换。
add_executable(flash_ftl_fatfs_tests
    test_flash_ftl.c
    ../../Components/flash_ftl/flash_ftl.c
    ../../FATFS/Target/user_diskio.c
    ../../FATFS/Target/bsp_driver_user_diskio.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c
)
target_compile_definitions(flash_ftl_fatfs_tests PRIVATE FTL_TEST_FATFS)
target_include_directories(flash_ftl_fatfs_tests PRIVATE ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_options(flash_ftl_fatfs_tests PRIVATE -UNDEBUG)
add_test(NAME flash_ftl_fatfs_remount COMMAND flash_ftl_fatfs_tests)

# MinGW PE 的 weak 外部解析不同于 ARM ELF；此测试只验证默认实现行为。
# 强弱覆盖关系另由实际 ARM 固件链接符号核验。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/bsp_driver_user_diskio.c" HOST_FALLBACK)
string(REPLACE "__attribute__((weak))" "" HOST_FALLBACK "${HOST_FALLBACK}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/bsp_fallback.c" "${HOST_FALLBACK}")
add_executable(user_diskio_fallback_tests test_user_diskio_fallback.c
    ../../FATFS/Target/user_diskio.c "${CMAKE_CURRENT_BINARY_DIR}/host_config/bsp_fallback.c")
target_include_directories(user_diskio_fallback_tests PRIVATE ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_options(user_diskio_fallback_tests PRIVATE -UNDEBUG)
add_test(NAME user_diskio_safe_weak_fallback COMMAND user_diskio_fallback_tests)

# 主机复用生成卷对象声明，只移除无关 SD BSP 的 HAL 头依赖。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/App/fatfs.h" HOST_FATFS_HEADER)
string(REPLACE "#include \"sd_diskio.h\"" "" HOST_FATFS_HEADER "${HOST_FATFS_HEADER}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config/FATFS/App")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/FATFS/App/fatfs.h" "${HOST_FATFS_HEADER}")

# 文件基准复用真实 APP/Service/FatFs/USER/FTL，仅替换任务、日志与故障边界。
get_target_property(FILE_BENCH_SOURCES flash_ftl_fatfs_tests SOURCES)
add_executable(flash_file_benchmark_tests
    ${FILE_BENCH_SOURCES}
    test_flash_file_benchmark.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../../APP/tasks/storage/benchmark/storage_flash_file_benchmark.c)
target_include_directories(flash_file_benchmark_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_definitions(flash_file_benchmark_tests PRIVATE FTL_TEST_FATFS
    FTL_TEST_FILE_BENCHMARK STORAGE_FLASH_BENCHMARK_ENABLE=1
    STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES=16384)
target_compile_options(flash_file_benchmark_tests PRIVATE -UNDEBUG)
foreach(OP WriteFile ReadFile SyncFile CloseFile RemoveFile)
    target_link_options(flash_file_benchmark_tests PRIVATE "-Wl,--wrap=Service_Filesystem_${OP}")
endforeach()
foreach(SCENARIO success exists not_initialized short_write write_error sync_error read_error corrupt
        close_once close_error delete_error contract)
    add_test(NAME flash_file_${SCENARIO} COMMAND flash_file_benchmark_tests)
    set_tests_properties(flash_file_${SCENARIO} PROPERTIES
        ENVIRONMENT "FLASH_FILE_SCENARIO=${SCENARIO}")
endforeach()

add_executable(filesystem_service_tests
    test_filesystem_service.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c)
target_include_directories(filesystem_service_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../stubs/lock_env ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_options(filesystem_service_tests PRIVATE -UNDEBUG)
add_test(NAME filesystem_service_volume_aware COMMAND filesystem_service_tests)

# 完成等待：真实的 Service、SD/Flash 私有执行器、FatFs 与 USER DiskIO 胶水，加共用 FreeRTOS 桩；
# 只有 Platform SD/Flash 用测试内的替身（成功、超时、错误、迟到的旧信号、不依赖调用任务）。
add_executable(filesystem_completion_tests
    test_filesystem_completion.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../../Service/filesystem/sd/filesystem_sd_transfer.c
    ../../Service/filesystem/flash/filesystem_flash_transfer.c
    ../../Service/filesystem/flash/filesystem_flash_bsp.c
    ../../Service/filesystem/flash/filesystem_flash_access.c
    ../../FATFS/Target/user_diskio.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c)
target_include_directories(filesystem_completion_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(filesystem_completion_tests PRIVATE c_std_11)
target_compile_options(filesystem_completion_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
# Vendor 与 CubeMX 生成的胶水不启用本仓库的警告集。
set_source_files_properties(
    ../../FATFS/Target/user_diskio.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c
    PROPERTIES COMPILE_OPTIONS "-w")
add_test(NAME filesystem_completion_wait COMMAND filesystem_completion_tests)


# Release 构建的 Service（带 -DNDEBUG，与固件 Release 一致）：同一套用例，另加“不投统计日志”。
# Service 源码编进 OBJECT 库单独带 -DNDEBUG；测试文件仍用 -UNDEBUG，免得它自己的 assert 被编掉。
add_library(filesystem_service_release_objects OBJECT
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../../Service/filesystem/filesystem_text.c
    ../../Service/filesystem/flash/filesystem_flash_access.c)
target_include_directories(filesystem_service_release_objects BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(filesystem_service_release_objects PRIVATE c_std_11)
target_compile_options(filesystem_service_release_objects PRIVATE -Wall -Wextra -Werror -O2 -DNDEBUG)
add_executable(filesystem_lock_release_tests
    test_filesystem_lock.c
    $<TARGET_OBJECTS:filesystem_service_release_objects>
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c)
target_include_directories(filesystem_lock_release_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../stubs/lock_env ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(filesystem_lock_release_tests PRIVATE c_std_11)
target_compile_definitions(filesystem_lock_release_tests PRIVATE LOCK_TEST_SERVICE_RELEASE)
target_compile_options(filesystem_lock_release_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME filesystem_lock_release_behavior COMMAND filesystem_lock_release_tests)
# 断言在 Release 里也保留：独立进程里验证（统计用例只在 Debug 构建里有意义，不在此列）。
foreach(SCENARIO reenter_same_volume hold_one_volume_enter_other hold_table_enter_volume deny_list_overflow)
    add_test(NAME filesystem_lock_release_${SCENARIO} COMMAND filesystem_lock_release_tests)
    set_tests_properties(filesystem_lock_release_${SCENARIO} PROPERTIES
        ENVIRONMENT "FILESYSTEM_LOCK_SCENARIO=${SCENARIO}")
endforeach()

# 共用 FreeRTOS 桩（Tests/stubs/freertos）自身的行为：互斥锁、信号量、任务通知与脚本化钩子。
add_executable(freertos_stub_tests
    test_freertos_stub.c
    ../stubs/freertos/freertos_stub.c)
target_include_directories(freertos_stub_tests BEFORE PRIVATE ../stubs/freertos ../..)
target_compile_features(freertos_stub_tests PRIVATE c_std_11)
target_compile_options(freertos_stub_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME freertos_stub_behavior COMMAND freertos_stub_tests)

# 入口加锁：真实的 Service 与 FatFs，加共用 FreeRTOS 桩、周期计数器与 Service Log 的替身；RAM 盘与传输层替身替代硬件。
# 一个用例组（不设环境变量）跑所有不会 abort 的用例；会让进程 abort 的断言用例各在独立进程里跑（见下面的循环）。
add_executable(filesystem_lock_tests
    test_filesystem_lock.c
    ../stubs/freertos/freertos_stub.c
    ../stubs/lock_env/lock_env_cycle_counter.c
    ../stubs/lock_env/lock_env_log.c
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../../Service/filesystem/filesystem_text.c
    ../../Service/filesystem/flash/filesystem_flash_access.c
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c)
target_include_directories(filesystem_lock_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../stubs/freertos ../stubs/lock_env ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(filesystem_lock_tests PRIVATE c_std_11)
target_compile_options(filesystem_lock_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
set_source_files_properties(
    ../../Middlewares/Third_Party/FatFs/src/ff.c
    ../../Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
    ../../Middlewares/Third_Party/FatFs/src/diskio.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c
    PROPERTIES COMPILE_OPTIONS "-w")
add_test(NAME filesystem_lock_behavior COMMAND filesystem_lock_tests)
# 会让进程 abort 的断言各在独立进程里跑，环境变量选用例。
foreach(SCENARIO reenter_same_volume hold_one_volume_enter_other hold_table_enter_volume deny_list_overflow
        stats_report stats_long_hold stats_no_counter)
    add_test(NAME filesystem_lock_${SCENARIO} COMMAND filesystem_lock_tests)
    set_tests_properties(filesystem_lock_${SCENARIO} PROPERTIES
        ENVIRONMENT "FILESYSTEM_LOCK_SCENARIO=${SCENARIO}")
endforeach()
