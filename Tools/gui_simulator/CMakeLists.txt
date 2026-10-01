# GUI 模拟器（Windows host，MinGW-w64 + SDL2）
#
# 在 PC 上运行真实的 LVGL v8.3.11 与 Service/gui（含手写界面 view/），
# 只把 Platform LCD、Platform Touch 与 FreeRTOS 任务通知替换为 SDL 替身。
# 本工程独立于固件构建，不进入目标固件。

cmake_minimum_required(VERSION 3.22)

project(gui_simulator LANGUAGES C)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT WIN32)
    message(FATAL_ERROR "gui_simulator 目前只支持 Windows 上的 MinGW-w64 GCC")
endif()

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
cmake_path(NORMAL_PATH REPO_ROOT)

# ---------------------------------------------------------------------------
# SDL2：libsdl-org 官方 MinGW 开发包，固定版本与 SHA256（与 GitHub Release 公布值一致）
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(sdl2_mingw
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
    URL_HASH SHA256=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(sdl2_mingw)

set(SDL2_ROOT "${sdl2_mingw_SOURCE_DIR}/x86_64-w64-mingw32")

add_library(sdl2_sim INTERFACE)
target_include_directories(sdl2_sim INTERFACE "${SDL2_ROOT}/include/SDL2")
target_link_libraries(sdl2_sim INTERFACE
    mingw32
    "${SDL2_ROOT}/lib/libSDL2main.a"
    "${SDL2_ROOT}/lib/libSDL2.dll.a"
)

# ---------------------------------------------------------------------------
# LVGL：直接编译 vendor 源码；配置经 sim_lv_conf.h 包装产品 lv_conf.h，
# 只覆盖 host 无法使用的选项（DMA2D、链接段属性）。
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/Middlewares/Third_Party/LVGL/src/*.c"
)

add_library(lvgl_sim STATIC ${LVGL_SOURCES})
target_include_directories(lvgl_sim PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/config"
    "${REPO_ROOT}"
    "${REPO_ROOT}/Middlewares/Third_Party/LVGL"
    "${REPO_ROOT}/Middlewares/Third_Party/LVGL/src"
)
target_compile_definitions(lvgl_sim PUBLIC LV_CONF_PATH=sim_lv_conf.h)
target_compile_options(lvgl_sim PRIVATE -w)

# ---------------------------------------------------------------------------
# 模拟器本体：真实 Service/gui + SDL 替身
# ---------------------------------------------------------------------------
file(GLOB_RECURSE SERVICE_GUI_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/Service/gui/*.c"
)
# 壁纸像素区只在固件中由链接脚本放进 SDRAM 资源区；模拟器由 fakes/resource_sim.c 提供。
list(FILTER SERVICE_GUI_SOURCES EXCLUDE REGEX "gui_service_view_wallpaper_region\\.c$")

add_executable(gui_simulator
    sim_main.c
    sim_clock.c
    sim_display.c
    sim_script.c
    fakes/platform_lcd_sim.c
    fakes/platform_touch_sim.c
    fakes/freertos_sim.c
    fakes/resource_sim.c
    ${SERVICE_GUI_SOURCES}
)

# shim/ 必须排在仓库根之前：Service/gui 以仓库相对路径包含 FreeRTOS 头，
# 由此解析到替身而不是 vendor 内核头。
target_include_directories(gui_simulator PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/shim"
    "${REPO_ROOT}"
)
target_compile_features(gui_simulator PRIVATE c_std_11)
target_compile_options(gui_simulator PRIVATE -Wall -Wextra)
target_link_libraries(gui_simulator PRIVATE lvgl_sim sdl2_sim)

add_custom_command(TARGET gui_simulator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_ROOT}/bin/SDL2.dll"
        "$<TARGET_FILE_DIR:gui_simulator>"
)
