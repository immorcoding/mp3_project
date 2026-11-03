cmake_minimum_required(VERSION 3.22)

project(filesystem_locator_tests LANGUAGES C)

enable_testing()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(FATFS_SRC "${REPO_ROOT}/Middlewares/Third_Party/FatFs/src")
file(GLOB FATFS_PATCHES CONFIGURE_DEPENDS "${REPO_ROOT}/patches/fatfs/*.patch")
list(SORT FATFS_PATCHES)
if(NOT FATFS_PATCHES)
    message(FATAL_ERROR "patches/fatfs 下没有补丁文件")
endif()

# 复制实际 FatFs 配置，仅移除不参与 FatFs 算法的 MCU/BSP 头文件依赖。
file(READ "${REPO_ROOT}/FATFS/Target/ffconf.h" HOST_FFCONF)
foreach(HEADER main.h stm32h7xx_hal.h bsp_driver_sd.h)
    string(REPLACE "#include \"${HEADER}\"" "" HOST_FFCONF "${HOST_FFCONF}")
endforeach()
# 锁表满的用例（按定位符打开与按路径打开共用锁表）需要一张小到能被填满的表：固定为 2，与产品的 _FS_LOCK 无关。
# 产品把 _FS_LOCK 调到 4（#191）后文件槽也是 4 个，锁表和文件槽会同时用尽，测不出“锁表满”这一条。
string(REGEX REPLACE "#define _FS_LOCK +[0-9]+" "#define _FS_LOCK 2" HOST_FFCONF "${HOST_FFCONF}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/ffconf.h" "${HOST_FFCONF}")

# 主机复用生成卷对象声明，只移除无关 SD BSP 的 HAL 头依赖。
file(READ "${REPO_ROOT}/FATFS/App/fatfs.h" HOST_FATFS_HEADER)
string(REPLACE "#include \"sd_diskio.h\"" "" HOST_FATFS_HEADER "${HOST_FATFS_HEADER}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_config/FATFS/App")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/host_config/FATFS/App/fatfs.h" "${HOST_FATFS_HEADER}")

# 把 patches/fatfs 的补丁应用到构建目录里的 ff.c 副本；工作树里的 ff.c 是否已打补丁不影响本测试。
# 已含该补丁则跳过；换行统一成 LF 再应用（补丁以 LF 存放）；GIT_CEILING_DIRECTORIES 让 git 不把构建目录当作仓库内的子目录，
# 否则补丁路径会被当成“仓库根相对路径”而跳过。
find_package(Git REQUIRED)
set(PATCHED_ROOT "${CMAKE_CURRENT_BINARY_DIR}/patched")
set(PATCHED_FF "${PATCHED_ROOT}/Middlewares/Third_Party/FatFs/src/ff.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FATFS_SRC}/ff.c" ${FATFS_PATCHES})
file(READ "${FATFS_SRC}/ff.c" FF_SOURCE)
string(REPLACE "\r\n" "\n" FF_SOURCE "${FF_SOURCE}")
file(WRITE "${PATCHED_FF}" "${FF_SOURCE}")
foreach(PATCH IN LISTS FATFS_PATCHES)
    # 工作树里的 ff.c 若已经打过这个补丁（用户提交 Vendor 改动之后），就不再重复应用。
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
                "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH}"
        WORKING_DIRECTORY "${PATCHED_ROOT}"
        RESULT_VARIABLE ALREADY_APPLIED
        OUTPUT_QUIET ERROR_QUIET)
    if(ALREADY_APPLIED EQUAL 0)
        continue()
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
                "${GIT_EXECUTABLE}" apply "${PATCH}"
        WORKING_DIRECTORY "${PATCHED_ROOT}"
        RESULT_VARIABLE PATCH_RESULT
        OUTPUT_VARIABLE PATCH_OUTPUT
        ERROR_VARIABLE PATCH_OUTPUT)
    if(NOT PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "FatFs 补丁 ${PATCH} 无法应用到仓库的 ff.c：${PATCH_OUTPUT}")
    endif()
endforeach()

add_executable(filesystem_locator_tests
    test_filesystem_locator.c
    ram_disk.c
    ../../Service/filesystem/filesystem_service.c
    ../../Service/filesystem/filesystem_file.c
    ../../Service/filesystem/filesystem_file_locator.c
    ../../Service/filesystem/filesystem_directory.c
    ../../Service/filesystem/filesystem_path.c
    ../../Service/filesystem/filesystem_handle.c
    ../../Service/filesystem/filesystem_lock.c
    ../../Service/filesystem/filesystem_status.c
    ../flash_ftl/stubs/filesystem_lock_host.c
    "${PATCHED_FF}"
    "${FATFS_SRC}/ff_gen_drv.c"
    "${FATFS_SRC}/diskio.c"
    "${FATFS_SRC}/option/cc936.c")

target_include_directories(filesystem_locator_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/host_config" ../flash_ftl/stubs ../..
    ../../FATFS/Target "${FATFS_SRC}")
target_compile_features(filesystem_locator_tests PRIVATE c_std_11)
target_compile_options(filesystem_locator_tests PRIVATE -O2 -UNDEBUG)
# Vendor 代码（含打过补丁的 ff.c）不启用本仓库的警告集。
set_source_files_properties("${PATCHED_FF}"
    "${FATFS_SRC}/ff_gen_drv.c" "${FATFS_SRC}/diskio.c" "${FATFS_SRC}/option/cc936.c"
    PROPERTIES COMPILE_OPTIONS "-w")

add_test(NAME filesystem_locator_behavior COMMAND filesystem_locator_tests)
