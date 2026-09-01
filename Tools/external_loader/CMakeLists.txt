cmake_minimum_required(VERSION 3.22)

project(w25q256_stm32h743zg_loader C)

add_executable(W25Q256_STM32H743ZG
    src/loader_dev_info.c
    src/loader_geometry.c
    src/loader_main.c
    src/loader_w25q256.c
    third_party/stm32h7_hal/src/stm32h7xx_hal.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_cortex.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_gpio.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_mdma.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_pwr.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_pwr_ex.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_qspi.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_rcc.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_rcc_ex.c
    third_party/stm32h7_hal/src/system_stm32h7xx.c)

target_compile_definitions(W25Q256_STM32H743ZG PRIVATE STM32H743xx CORE_CM7)
target_include_directories(W25Q256_STM32H743ZG PRIVATE
    include
    third_party/cmsis
    third_party/stm32h7_hal/inc)
target_compile_options(W25Q256_STM32H743ZG PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -ffreestanding
    -fdata-sections
    -ffunction-sections
    -fno-builtin
    -fno-common
    -Wall
    -Wextra
    -Werror)
target_link_options(W25Q256_STM32H743ZG PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -nostartfiles
    -Wl,-e,Init
    -Wl,--gc-sections
    -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/ldscript.ld)
set_target_properties(W25Q256_STM32H743ZG PROPERTIES SUFFIX ".stldr")

set(LOADER_VENDOR_SOURCES
    third_party/stm32h7_hal/src/stm32h7xx_hal.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_cortex.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_gpio.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_mdma.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_pwr.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_pwr_ex.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_qspi.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_rcc.c
    third_party/stm32h7_hal/src/stm32h7xx_hal_rcc_ex.c
    third_party/stm32h7_hal/src/system_stm32h7xx.c)
set_source_files_properties(${LOADER_VENDOR_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-error;-Wno-misleading-indentation;-Wno-unused-parameter")
