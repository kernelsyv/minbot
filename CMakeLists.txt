cmake_minimum_required(VERSION 3.20)

project(minbot LANGUAGES C CXX)

option(MINBOT_BUILD_TESTS "Build minbot tests" ON)

find_package(ZLIB 1.2.13 QUIET)
if(ZLIB_FOUND)
    set(MINBOT_ZLIB_TARGET ZLIB::ZLIB)
else()
    include(FetchContent)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zlib
        URL https://zlib.net/fossils/zlib-1.3.2.tar.gz
        URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(zlib)
    set(MINBOT_ZLIB_TARGET ZLIB::ZLIBSTATIC)
endif()

add_library(minbot_core
    src/connection.cpp
    src/control.cpp
    src/game_client.cpp
    src/protocol.cpp
    src/status_client.cpp
)

target_include_directories(minbot_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(minbot_core PUBLIC cxx_std_20)
target_link_libraries(minbot_core PRIVATE ${MINBOT_ZLIB_TARGET})

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

find_package(Threads REQUIRED)
target_link_libraries(minbot_core PUBLIC Threads::Threads)

if(MINBOT_BUILD_TESTS)
    include(CTest)

    add_executable(minbot_protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(minbot_protocol_tests PRIVATE minbot_core)

    add_test(NAME protocol COMMAND minbot_protocol_tests)

    add_executable(minbot_control_tests tests/control_tests.cpp)
    target_link_libraries(minbot_control_tests PRIVATE minbot_core)

    add_test(NAME control COMMAND minbot_control_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME status_integration
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/status_integration.py
                $<TARGET_FILE:minbot>
        )
        add_test(
            NAME game_integration
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/game_integration.py
                $<TARGET_FILE:minbot>
        )
    endif()
endif()
