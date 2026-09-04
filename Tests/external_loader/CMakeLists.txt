cmake_minimum_required(VERSION 3.22)

project(external_loader_geometry_tests LANGUAGES C)

enable_testing()

add_executable(external_loader_geometry_tests
    ../../Tools/external_loader/tests/loader_geometry_test.c
    ../../Tools/external_loader/src/loader_geometry.c
)

target_include_directories(external_loader_geometry_tests PRIVATE
    ../../Tools/external_loader/include
)
target_compile_features(external_loader_geometry_tests PRIVATE c_std_11)
target_compile_options(external_loader_geometry_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME external_loader_geometry COMMAND external_loader_geometry_tests)
