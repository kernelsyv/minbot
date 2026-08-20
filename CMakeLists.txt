cmake_minimum_required(VERSION 3.20)

project(minbot LANGUAGES CXX)

option(MINBOT_BUILD_TESTS "Build minbot tests" ON)

add_library(minbot_core
    src/protocol.cpp
    src/status_client.cpp
)

target_include_directories(minbot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(minbot_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(minbot_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(minbot_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(minbot_core PUBLIC ws2_32)
endif()

add_executable(minbot src/main.cpp)
target_link_libraries(minbot PRIVATE minbot_core)

if(MINBOT_BUILD_TESTS)
    include(CTest)

    add_executable(minbot_protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(minbot_protocol_tests PRIVATE minbot_core)

    add_test(NAME protocol COMMAND minbot_protocol_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME status_integration
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/status_integration.py
                $<TARGET_FILE:minbot>
        )
    endif()
endif()
