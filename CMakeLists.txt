cmake_minimum_required(VERSION 2.8.3)
project(snapcast C)

find_path(JSON_C_INCLUDE_DIR NAMES json-c/json.h HINTS ${_JSON_C_INCLUDE_DIRS})
find_library(JSON_C_LIBRARIES NAMES json-c HINTS ${_JSON_C_LIBRARY_DIRS})

set(JSON_C_CFLAGS_OTHER "${_JSON_C_CFLAGS_OTHER}" CACHE STRING "Additional compiler flags for json-c")
set(JSON_C_LDFLAGS_OTHER "${_JSON_C_LDFLAGS_OTHER}" CACHE STRING "Additional linker flags for json-c")
find_package(ALSA REQUIRED)


if (ALSA_FOUND)
	include_directories(${ALSA_INCLUDE_DIRS})
endif(ALSA_FOUND)

# find_package_handle_standard_args(JSON_C REQUIRED_VARS JSON_C_LIBRARIES JSON_C_INCLUDE_DIR)
# mark_as_advanced(JSON_C_INCLUDE_DIR JSON_C_LIBRARIES JSON_C_CFLAGS_OTHER JSON_C_LDFLAGS_OTHER)

set(CMAKE_MODULE_PATH ${snapcastc_SOURCE_DIR})

add_subdirectory(src)
