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
    ../../Service/filesystem/flash/filesystem_flash_file.c
    ../../APP/tasks/storage/benchmark/storage_flash_file_benchmark.c)
target_include_directories(flash_file_benchmark_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" stubs ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_definitions(flash_file_benchmark_tests PRIVATE FTL_TEST_FATFS
    FTL_TEST_FILE_BENCHMARK STORAGE_FLASH_BENCHMARK_ENABLE=1
    STORAGE_FLASH_BENCHMARK_FILE_TOTAL_BYTES=16384)
target_compile_options(flash_file_benchmark_tests PRIVATE -UNDEBUG)
foreach(OP WriteFile ReadFile SyncFile CloseFile RemoveFlashFile)
    target_link_options(flash_file_benchmark_tests PRIVATE "-Wl,--wrap=Service_Filesystem_${OP}")
endforeach()
foreach(SCENARIO success exists nonowner short_write write_error sync_error read_error corrupt
        close_once close_error delete_error contract)
    add_test(NAME flash_file_${SCENARIO} COMMAND flash_file_benchmark_tests)
    set_tests_properties(flash_file_${SCENARIO} PROPERTIES
        ENVIRONMENT "FLASH_FILE_SCENARIO=${SCENARIO}")
endforeach()


