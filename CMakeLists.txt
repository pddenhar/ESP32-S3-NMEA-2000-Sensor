cmake_minimum_required(VERSION 3.22)

# The NMEA2000 core library still declares cmake_minimum_required(VERSION 3.0),
# which CMake 4 refuses outright. Raise the compatibility floor on its behalf.
# Both forms are needed: the cache entry covers this configure, and the
# environment variable covers the separate `cmake -P` process that IDF spawns to
# scan component requirements, which does not inherit our cache.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Floor for third-party CMakeLists")
set(ENV{CMAKE_POLICY_VERSION_MINIMUM} 3.5)


set(IDF_TARGET esp32s3)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nmea-2k-rudder)

# Lock Target: Fail hard if target is not esp32s3
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "Target '${IDF_TARGET}' is not supported. This project is specific to esp32s3. Please run: idf.py
  set-target esp32s3")
endif()
