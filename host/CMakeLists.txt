cmake_minimum_required(VERSION 3.20)
project(droppix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Locate the system libevdi runtime library (header is vendored).
find_library(EVDI_LIB NAMES evdi PATHS /usr/lib64 /lib64 REQUIRED)
message(STATUS "Using libevdi: ${EVDI_LIB}")

# Core library: sources are added by later tasks.
add_library(droppix_core
  src/edid.cpp
  src/virtual_display.cpp
  src/capturer.cpp
  src/png_writer.cpp
  src/protocol.cpp
)
target_include_directories(droppix_core PUBLIC
  src
  third_party/evdi
  third_party/stb
)
target_link_libraries(droppix_core PUBLIC ${EVDI_LIB})

add_executable(droppix_spike src/spike_main.cpp)
target_link_libraries(droppix_spike PRIVATE droppix_core)

# --- Tests (GoogleTest via FetchContent) ---
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(droppix_tests
  tests/test_smoke.cpp
  tests/test_edid.cpp
  tests/test_virtual_display.cpp
  tests/test_png_writer.cpp
  tests/test_protocol.cpp
)
target_link_libraries(droppix_tests PRIVATE droppix_core GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(droppix_tests)
