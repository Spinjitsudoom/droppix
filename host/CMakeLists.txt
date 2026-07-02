cmake_minimum_required(VERSION 3.20)
project(droppix LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Locate the system libevdi runtime library (header is vendored).
find_library(EVDI_LIB NAMES evdi PATHS /usr/lib64 /lib64 REQUIRED)
message(STATUS "Using libevdi: ${EVDI_LIB}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# Core library: sources are added by later tasks.
add_library(droppix_core
  src/edid.cpp
  src/cvt.cpp
  src/virtual_display.cpp
  src/capturer.cpp
  src/png_writer.cpp
  src/protocol.cpp
  src/software_encoder.cpp
  src/test_pattern_source.cpp
  src/evdi_frame_source.cpp
  src/transport_server.cpp
  src/stream_daemon.cpp
  src/stat_accumulator.cpp
  src/stats_json.cpp
  src/input_map.cpp
  src/monitor_geometry.cpp
  src/input_injector.cpp
  src/mdns_browse.cpp
  src/usb_scan.cpp
  src/audio_streamer.cpp
)
target_include_directories(droppix_core PUBLIC
  src
  third_party/evdi
  third_party/stb
)
target_link_libraries(droppix_core PUBLIC ${EVDI_LIB} PkgConfig::FFMPEG OpenSSL::SSL OpenSSL::Crypto)

add_executable(droppix_spike src/spike_main.cpp)
target_link_libraries(droppix_spike PRIVATE droppix_core)

add_executable(droppix_stream src/stream_main.cpp)
target_link_libraries(droppix_stream PRIVATE droppix_core)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
  add_executable(droppix_gui
    gui/main.cpp
    gui/main_window.cpp
    gui/stats_parser.cpp
    gui/args_builder.cpp
  )
  target_include_directories(droppix_gui PRIVATE gui src)
  target_link_libraries(droppix_gui PRIVATE Qt6::Widgets Qt6::Network)
  target_sources(droppix_gui PRIVATE
    gui/profile_store.cpp
    gui/stream_controller.cpp
    gui/adb_manager.cpp
    gui/mdns_advertiser.cpp
    gui/mdns_browser.cpp
    gui/usb_client_scanner.cpp
    gui/approved_store.cpp
    gui/cert_manager.cpp
    gui/audio_sink.cpp
    gui/settings_dialog.cpp
    gui/resources.qrc
    src/mdns_browse.cpp
    src/usb_scan.cpp
  )
  target_link_libraries(droppix_gui PRIVATE OpenSSL::SSL OpenSSL::Crypto)
else()
  message(STATUS "Qt6 not found; skipping droppix_gui")
endif()

# --- Tests (GoogleTest via FetchContent) ---
# Off for offline/packaged builds (e.g. Flatpak), where FetchContent can't reach the network.
option(DROPPIX_BUILD_TESTS "Build unit tests (needs network for GoogleTest)" ON)
if(DROPPIX_BUILD_TESTS)
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
  tests/test_software_encoder.cpp
  tests/test_test_pattern_source.cpp
  tests/test_transport_server.cpp
  tests/test_stat_accumulator.cpp
  tests/test_stats_json.cpp
  tests/test_stats_parser.cpp
  tests/test_args_builder.cpp
  tests/test_cvt.cpp
  tests/test_input_map.cpp
  tests/test_monitor_geometry.cpp
  tests/test_orientation.cpp
  tests/test_audio_streamer.cpp
  tests/test_approval.cpp
  tests/test_wake.cpp
  tests/test_mdns_browse.cpp
  tests/test_usb_scan.cpp
  tests/test_pairing_code.cpp
  tests/test_audio_sink.cpp
  gui/stats_parser.cpp
  gui/args_builder.cpp
  gui/audio_sink.cpp
)
target_include_directories(droppix_tests PRIVATE gui)
target_link_libraries(droppix_tests PRIVATE droppix_core GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(droppix_tests)

if(Qt6_FOUND)
  find_package(Qt6 COMPONENTS Test QUIET)
  add_executable(droppix_gui_tests
    gui/tests/test_profile_store.cpp
    gui/profile_store.cpp
    gui/tests/test_approved_store.cpp
    gui/approved_store.cpp
  )
  target_include_directories(droppix_gui_tests PRIVATE gui)
  target_link_libraries(droppix_gui_tests PRIVATE Qt6::Widgets GTest::gtest_main)
  gtest_discover_tests(droppix_gui_tests)
endif()
endif()  # DROPPIX_BUILD_TESTS
