cmake_minimum_required(VERSION 2.8.3)
project(snapcast C)

option(WITH_ASAN "Enable ASAN (address sanitizer)" OFF)

find_path(JSON_C_INCLUDE_DIR NAMES json-c/json.h HINTS ${_JSON_C_INCLUDE_DIRS})
find_library(JSON_C_LIBRARIES NAMES json-c HINTS ${_JSON_C_LIBRARY_DIRS})

set(JSON_C_CFLAGS_OTHER "${_JSON_C_CFLAGS_OTHER}" CACHE STRING "Additional compiler flags for json-c")
set(JSON_C_LDFLAGS_OTHER "${_JSON_C_LDFLAGS_OTHER}" CACHE STRING "Additional linker flags for json-c")
find_package(ALSA REQUIRED)

link_libraries (m)
link_libraries (soxr)
link_libraries (opus)
link_libraries (rubberband)

if (ALSA_FOUND)
	include_directories(${ALSA_INCLUDE_DIRS})
endif(ALSA_FOUND)

# find_package_handle_standard_args(JSON_C REQUIRED_VARS JSON_C_LIBRARIES JSON_C_INCLUDE_DIR)
# mark_as_advanced(JSON_C_INCLUDE_DIR JSON_C_LIBRARIES JSON_C_CFLAGS_OTHER JSON_C_LDFLAGS_OTHER)


# see #68
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fcommon")

if (WITH_ASAN)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
endif (WITH_ASAN)

set(CMAKE_MODULE_PATH ${snapcastc_SOURCE_DIR})

add_subdirectory(src)
