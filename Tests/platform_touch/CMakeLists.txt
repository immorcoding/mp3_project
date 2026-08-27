cmake_minimum_required(VERSION 3.22)

project(platform_touch_behavior_test LANGUAGES C)

include(CTest)

add_executable(platform_touch_behavior_test
    test_platform_touch.c
    ../../Platform/touch/platform_touch.c
)

target_compile_features(platform_touch_behavior_test PRIVATE c_std_11)

target_include_directories(platform_touch_behavior_test PRIVATE
    fakes
    ../..
)

add_test(
    NAME platform_touch_behavior
    COMMAND platform_touch_behavior_test
)
