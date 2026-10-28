cmake_minimum_required(VERSION 3.22)

project(storage_catalog_tests LANGUAGES C)

enable_testing()

add_executable(storage_catalog_tests
    test_storage_catalog_name.c
    ../../APP/tasks/storage/catalog/storage_catalog_name.c
)

target_include_directories(storage_catalog_tests PRIVATE ../..)
target_compile_features(storage_catalog_tests PRIVATE c_std_11)
target_compile_options(storage_catalog_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME storage_catalog_name COMMAND storage_catalog_tests)

# 复制实际 FatFs 配置，仅移除不参与查表算法的 MCU/BSP 头文件依赖；GBK 查表链接真实 cc936.c。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../FATFS/Target/ffconf.h" HOST_FFCONF)
foreach(HEADER main.h stm32h7xx_hal.h bsp_driver_sd.h)
    string(REPLACE "#include \"${HEADER}\"" "" HOST_FFCONF "${HOST_FFCONF}")
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/ffconf.h" "${HOST_FFCONF}")

add_executable(storage_catalog_text_tests
    test_storage_catalog_text.c
    ../../APP/tasks/storage/catalog/storage_catalog_text.c
    ../../APP/tasks/storage/catalog/storage_catalog_name.c
    ../../Components/id3_reader/id3_reader.c
    ../../Middlewares/Third_Party/FatFs/src/option/cc936.c
)

target_include_directories(storage_catalog_text_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../..
    ../../FATFS/Target ../../Middlewares/Third_Party/FatFs/src)
target_compile_features(storage_catalog_text_tests PRIVATE c_std_11)
target_compile_options(storage_catalog_text_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
# cc936.c 是 Vendor 代码，不对它启用 -Werror 警告集。
set_source_files_properties(../../Middlewares/Third_Party/FatFs/src/option/cc936.c
    PROPERTIES COMPILE_OPTIONS "-w")

add_test(NAME storage_catalog_text COMMAND storage_catalog_text_tests)
