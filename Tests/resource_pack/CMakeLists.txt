cmake_minimum_required(VERSION 3.22)

project(resource_pack_component_tests LANGUAGES C)

enable_testing()

add_executable(resource_pack_component_tests
    test_resource_pack.c
    ../../Components/resource_pack/resource_pack.c
    ../../Components/resource_pack/resource_pack_binary.c
    ../../Components/resource_pack/resource_pack_image.c
)

target_include_directories(resource_pack_component_tests PRIVATE ../..)
target_compile_features(resource_pack_component_tests PRIVATE c_std_11)
target_compile_options(resource_pack_component_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

add_test(NAME resource_pack_protocol_validation COMMAND resource_pack_component_tests)
