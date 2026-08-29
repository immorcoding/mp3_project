cmake_minimum_required(VERSION 3.22)

project(w25qxx_component_tests LANGUAGES C)

enable_testing()

add_executable(w25qxx_component_tests
    test_w25qxx.c
    ../../Components/w25qxx/w25qxx.c
)

target_include_directories(w25qxx_component_tests PRIVATE
    ../..
)

add_test(
    NAME w25qxx_init_validates_expected_jedec_id
    COMMAND w25qxx_component_tests
)
