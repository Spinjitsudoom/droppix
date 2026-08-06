# Build-time version resolver (run via `cmake -P`, not at configure time) so the
# embedded string tracks the *current* checkout on every build, not just when CMake
# last ran. Writes OUT only when the value changes, so a stable checkout doesn't force
# version.cpp to recompile each build.
#
#   cmake -DSRC_DIR=<repo subdir> -DOUT=<generated header> -P GenerateVersion.cmake
#
# Falls back to a static string when git (or the .git dir) is unavailable — e.g. a
# release tarball — so the header always defines DROPPIX_VERSION.

if(NOT DEFINED SRC_DIR OR NOT DEFINED OUT)
  message(FATAL_ERROR "GenerateVersion.cmake requires -DSRC_DIR=<dir> -DOUT=<file>")
endif()

set(_version "0.1.0")  # fallback: no git / no tags (matches the base tag)
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
    WORKING_DIRECTORY ${SRC_DIR}
    OUTPUT_VARIABLE _desc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _rc)
  if(_rc EQUAL 0 AND NOT _desc STREQUAL "")
    set(_version "${_desc}")
  endif()
endif()

set(_content "#pragma once\n#define DROPPIX_VERSION \"${_version}\"\n")
set(_old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" _old)
endif()
if(NOT _old STREQUAL "${_content}")
  file(WRITE "${OUT}" "${_content}")
  message(STATUS "droppix version: ${_version}")
endif()
