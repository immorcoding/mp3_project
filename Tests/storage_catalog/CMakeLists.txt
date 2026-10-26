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
