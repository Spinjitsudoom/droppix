cmake_minimum_required(VERSION 3.20)
project(droppix_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavutil libswscale)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Multimedia MultimediaWidgets)

# Core: the client-side counterpart to droppix_core, but decode-only (no evdi/uinput/
# encoder/desktop-backend — those are host-only). Reuses the wire codec + a couple of
# pure headers directly from host/src by relative path rather than restructuring the
# host's own CMakeLists.txt/droppix_core target (zero risk to the working host build).
add_library(droppix_client_core
  ../host/src/protocol.cpp
  src/client_socket_channel.cpp
  src/tls_trust.cpp
  src/transport_client.cpp
  src/video_decoder.cpp
  src/audio_player.cpp
  src/device_identity.cpp
  src/client_settings.cpp
)
target_include_directories(droppix_client_core PUBLIC
  src
  ../host/src   # protocol.h, byte_channel.h, pairing_code.h, stat_accumulator.h
)
target_link_libraries(droppix_client_core PUBLIC
  Qt6::Widgets Qt6::Network Qt6::Multimedia
  PkgConfig::FFMPEG OpenSSL::SSL OpenSSL::Crypto Threads::Threads
)

add_executable(droppix_client
  gui/main.cpp
  gui/main_window.cpp
  gui/connect_dialog.cpp
  gui/settings_dialog.cpp
  gui/video_widget.cpp
  gui/host_store.cpp
  gui/header_bar.cpp
  gui/idle_page.cpp
  gui/client_theme.cpp
  gui/resources.qrc
)
# ../host/gui for the shared design tokens (style.h + theme.h — header-only, no linking).
# The palette used to be COPIED into client/gui/style.h and had already drifted: the client
# painted its window in the host's *surface* colour. Same reuse-by-relative-path approach
# droppix_client_core already takes with ../host/src.
target_include_directories(droppix_client PRIVATE gui src ../host/gui)
target_link_libraries(droppix_client PRIVATE
  droppix_client_core Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::MultimediaWidgets
)

# --- Tests (GoogleTest via FetchContent) ---
option(DROPPIX_CLIENT_BUILD_TESTS "Build unit tests (needs network for GoogleTest)" ON)
if(DROPPIX_CLIENT_BUILD_TESTS)
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(droppix_client_tests
  tests/test_touch_normalizer.cpp
  tests/test_tls_trust.cpp
  tests/test_client_settings.cpp
  tests/test_video_decoder_flip.cpp
  src/tls_trust.cpp
  src/client_settings.cpp
  src/video_decoder.cpp
  ../host/src/protocol.cpp
)
target_include_directories(droppix_client_tests PRIVATE gui src ../host/src)
target_link_libraries(droppix_client_tests PRIVATE
  Qt6::Widgets Qt6::Network Qt6::Multimedia OpenSSL::SSL OpenSSL::Crypto
  PkgConfig::FFMPEG GTest::gtest_main Threads::Threads)
include(GoogleTest)
gtest_discover_tests(droppix_client_tests)
endif()
