cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME mp3_project)

# Include toolchain file
include("cmake/gcc-arm-none-eabi.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(Middlewares/Third_Party/FreeRTOS)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Recursively collect reusable, hardware-independent Components. CONFIGURE_DEPENDS
# asks CMake to reconfigure when a matching .c file is added or removed.
# Keep temporary/test .c files outside these directories, otherwise they become
# part of the firmware automatically.
file(GLOB_RECURSE COMPONENT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Components/*.c"
)

# Collect hardware/vendor-specific implementations of Component-owned interfaces.
file(GLOB_RECURSE ADAPTER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Adapters/*.c"
)

# Collect product/PCB composition and product-facing hardware capabilities.
file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/*.c"
)

# Collect CubeMX USB Device glue sources. These files remain generator-owned;
# custom code must stay inside CubeMX USER CODE regions so regeneration preserves it.
file(GLOB_RECURSE USB_DEVICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/App/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/USB_DEVICE/Target/*.c"
)

# Attach discovered project-owned sources plus the explicit application entry.
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${COMPONENT_SOURCES}
    ${ADAPTER_SOURCES}
    ${PLATFORM_SOURCES}
    ${USB_DEVICE_SOURCES}

    APP/app.c
)

# Expose only the project root. Source files therefore include project modules
# with unambiguous full paths such as
# "Components/axp2101/axp2101.h" instead of adding
# every leaf directory and relying on short, potentially colliding file names.
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}

    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    stm32cubemx
    # Add user defined libraries
    freertos_kernel
)

# Convert the linked ELF into formats used by common programming paths:
# - Intel HEX keeps address metadata;
# - raw BIN contains only bytes and needs an externally supplied flash address.
# $<TARGET_FILE...> generator expressions keep this valid for all build presets.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex

    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin

    COMMENT "Generating HEX and BIN files"
    VERBATIM
)
