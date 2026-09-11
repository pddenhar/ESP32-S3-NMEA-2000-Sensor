cmake_minimum_required(VERSION 3.16)

# --- Custom Message Filter Start ---
# Suppress the strict ESP-IDF v6 component validation warnings
# to keep CLion's CMake log clean and readable.
function(message)
    list(JOIN ARGV " " FULL_MSG)
    if(ARGV0 STREQUAL "WARNING" AND FULL_MSG MATCHES "belongs to component")
        # Silently drop the validation warning
        return()
    endif()

    # Pass everything else to CMake's original _message command
    _message(${ARGV})
endfunction()
# --- Custom Message Filter End ---

# The NMEA2000 core library still declares cmake_minimum_required(VERSION 3.0),
# which CMake 4 refuses outright. Raise the compatibility floor on its behalf.
# Both forms are needed: the cache entry covers this configure, and the
# environment variable covers the separate `cmake -P` process that IDF spawns to
# scan component requirements, which does not inherit our cache.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Floor for third-party CMakeLists")
set(ENV{CMAKE_POLICY_VERSION_MINIMUM} 3.5)


set(IDF_TARGET esp32s3)
set(EXTRA_COMPONENT_DIRS "../../")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lvgl_example)
# Lock Target: Fail hard if target is not esp32s3
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "Target '${IDF_TARGET}' is not supported. This project is specific to esp32s3. Please run: idf.py
  set-target esp32s3")
endif()
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format=" APPEND)
