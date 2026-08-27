# CMakeLists.txt — host unit tests for the eClock firmware.
#
# Build with the MinGW-w64 GCC toolchain:
#   cmake -S firmware/test -B firmware/test/build -G Ninja
#   cmake --build firmware/test/build
#   ctest --test-dir firmware/test/build --output-on-failure
#   cmake --build firmware/test/build --target coverage
#
# Design:
#   * Each test .cpp #includes clock_harness.h -> #includes main.cpp, so the
#     firmware's file-scoped `static` state and functions are reachable in that
#     translation unit WITHOUT modifying the core code.
#   * Mocks (Arduino/mbed/BLE/Print), the vendored Adafruit_GFX, and the PNG
#     helper are compiled once into `eclock_mock` (no coverage flags).
#   * Each test executable is built with --coverage. gcovr merges the per-binary
#     .gcda data; the preprocessor's #line markers attribute main.cpp's lines
#     correctly, so coverage is reported against src/main.cpp.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.16)
project(eclock_tests CXX)

include(CTest)    # required for ctest to discover add_test() targets

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------------------
# Paths (relative to firmware/test/)
# ---------------------------------------------------------------------------
set(ECLOCK_FW_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/..")              # firmware/
set(ECLOCK_SRC_DIR   "${ECLOCK_FW_DIR}/src")
set(ECLOCK_INC_DIR   "${ECLOCK_FW_DIR}/include")
set(ECLOCK_MOCKS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mocks")
set(ECLOCK_HARN_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/harness")
set(ECLOCK_GFX_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Adafruit_GFX")
set(ECLOCK_STB_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")

# ---------------------------------------------------------------------------
# GoogleTest via FetchContent (network required at first configure).
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
# Silence googletest's own warnings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Global include dirs (ORDER MATTERS: mocks before third_party).
# ---------------------------------------------------------------------------
include_directories(
    ${ECLOCK_MOCKS_DIR}
    ${ECLOCK_HARN_DIR}
    ${ECLOCK_SRC_DIR}
    ${ECLOCK_INC_DIR}
    ${ECLOCK_GFX_DIR}          # Adafruit_GFX.h, gfxfont.h, and Fonts/...
    ${ECLOCK_STB_DIR}
)

# The firmware defines this branch (mbed core) on host; see main.cpp's #error.
# ARDUINO must be defined BEFORE Adafruit_GFX.h's `#if ARDUINO >= 100` guard
# (evaluated at the top of the header, before it includes Arduino.h), so it is
# a compile definition here rather than relying on Arduino.h.
add_compile_definitions(ECLOCK_CORE_MBED=1 ARDUINO=100)

# ---------------------------------------------------------------------------
# eclock_mock — shared library of mocks + vendored GFX + PNG helper.
# Compiled WITHOUT coverage so the coverage number is main.cpp-only.
# ---------------------------------------------------------------------------
add_library(eclock_mock STATIC
    ${ECLOCK_MOCKS_DIR}/Arduino.cpp
    ${ECLOCK_MOCKS_DIR}/mbed.cpp
    ${ECLOCK_MOCKS_DIR}/ArduinoBLE.cpp
    ${ECLOCK_MOCKS_DIR}/Print.cpp
    ${ECLOCK_GFX_DIR}/Adafruit_GFX.cpp
    ${ECLOCK_HARN_DIR}/png_dump.cpp
    ${ECLOCK_SRC_DIR}/clock_logic.cpp     # pure logic, compiled for host tests too
)
target_include_directories(eclock_mock PUBLIC ${ECLOCK_MOCKS_DIR} ${ECLOCK_HARN_DIR})

# ---------------------------------------------------------------------------
# Test registration helper.
# ---------------------------------------------------------------------------
function(eclock_test NAME)
    add_executable(${NAME} ${ARGN})
    target_link_libraries(${NAME} PRIVATE eclock_mock gtest_main)
    # Coverage only on the test TU (which contains main.cpp).
    target_compile_options(${NAME} PRIVATE --coverage)
    target_link_options(${NAME} PRIVATE --coverage)
    # Run tests from firmware/test/ so output/ paths map consistently.
    add_test(NAME ${NAME}
             COMMAND ${NAME}
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endfunction()

# ---------------------------------------------------------------------------
# Test executables. Add each test file here.
# ---------------------------------------------------------------------------
eclock_test(test_clock_logic tests/test_clock_logic.cpp)
eclock_test(test_smoke      tests/test_smoke.cpp)
eclock_test(test_setup      tests/test_setup.cpp)
eclock_test(test_time       tests/test_time.cpp)
eclock_test(test_state      tests/test_state_machine.cpp)
eclock_test(test_battery    tests/test_battery.cpp)
eclock_test(test_display    tests/test_display.cpp)
eclock_test(test_sleep      tests/test_sleep.cpp)

# ---------------------------------------------------------------------------
# Coverage target (gcovr must be on PATH).
# ---------------------------------------------------------------------------
find_program(GCOVR_EXE gcovr)

if(GCOVR_EXE)
    set(GCOV_FILTER ".*main\\.cpp$")
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/output/coverage
        COMMAND ${GCOVR_EXE} -r ${CMAKE_CURRENT_SOURCE_DIR}
                --filter ${GCOV_FILTER}
                --html --html-details
                -o ${CMAKE_CURRENT_BINARY_DIR}/output/coverage/index.html
        COMMAND ${GCOVR_EXE} -r ${CMAKE_CURRENT_SOURCE_DIR}
                --filter ${GCOV_FILTER}
                --txt
        COMMENT "Generating coverage report (main.cpp only)"
    )
else()
    message(WARNING "gcovr not found; 'coverage' target disabled.")
endif()
