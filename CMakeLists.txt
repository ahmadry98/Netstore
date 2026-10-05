cmake_minimum_required(VERSION 3.20)

project(
    NetStore
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wpedantic>
)

add_library(netstore_core
    src/command_executor.cpp
    src/command_parser.cpp
    src/key_value_store.cpp
)

target_include_directories(
    netstore_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(netstore_core PUBLIC cxx_std_20)
set_target_properties(netstore_core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(netstore src/main.cpp)
target_link_libraries(netstore PRIVATE netstore_core)
set_target_properties(netstore PROPERTIES CXX_EXTENSIONS OFF)

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)

    add_executable(key_value_store_test
        tests/key_value_store_test.cpp
    )

    target_link_libraries(
        key_value_store_test
        PRIVATE
            netstore_core
            Catch2::Catch2WithMain
    )

    add_executable(command_parser_test
        tests/command_parser_test.cpp
    )

    target_link_libraries(
        command_parser_test
        PRIVATE
            netstore_core
            Catch2::Catch2WithMain
    )

    add_executable(command_executor_test
        tests/command_executor_test.cpp
    )

    target_link_libraries(
        command_executor_test
        PRIVATE
            netstore_core
            Catch2::Catch2WithMain
    )

    set_target_properties(
        key_value_store_test
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    set_target_properties(
        command_parser_test
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    set_target_properties(
        command_executor_test
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    catch_discover_tests(key_value_store_test DISCOVERY_MODE PRE_TEST)
    catch_discover_tests(command_parser_test DISCOVERY_MODE PRE_TEST)
    catch_discover_tests(command_executor_test DISCOVERY_MODE PRE_TEST)

    add_test(
        NAME netstore_cli_test
        COMMAND
            ${CMAKE_COMMAND}
            -DNETSTORE_EXECUTABLE=$<TARGET_FILE:netstore>
            -DTEST_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_input.txt
            -DEXPECTED_OUTPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_expected.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cli_test.cmake
    )
endif()
