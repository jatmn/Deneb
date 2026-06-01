# SPDX-License-Identifier: MPL-2.0
#
# Deneb API Server - CMake Build System
# Implements UltiMaker REST API v1 for Cura LAN compatibility.

cmake_minimum_required(VERSION 3.12)
project(deneb-api C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Version
if(EXISTS "${CMAKE_SOURCE_DIR}/../.git")
    execute_process(
        COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_VERSION)
    set(GIT_VERSION "0.1.0-dev")
endif()
add_definitions(-DDENEB_VERSION="${GIT_VERSION}")

# Optimization
if(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(-Os -ffunction-sections -fdata-sections)
    if(NOT WIN32)
        add_link_options(-Wl,--gc-sections)
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -ffunction-sections -fdata-sections)
    if(NOT WIN32)
        add_link_options(-Wl,--gc-sections)
    endif()
endif()

set(DENEB_API_SOURCES
    src/main.c
    src/api_http.c
    src/api_printer.c
    src/api_print_job.c
    src/api_system.c
    src/api_materials.c
    src/api_auth.c
    src/api_deneb.c
    src/backend_zmq.c
    src/json_writer.c
    src/md5.c
)

add_executable(deneb-api ${DENEB_API_SOURCES})
target_include_directories(deneb-api PRIVATE src)

# ZMQ (static link for MIPS target)
set(ZMQ_STATIC_PATH "" CACHE PATH "Path to static libzmq.a")
set(ZMQ_INCLUDE_DIR "" CACHE PATH "Path to directory containing zmq.h")
if(ZMQ_STATIC_PATH AND EXISTS "${ZMQ_STATIC_PATH}")
    target_link_libraries(deneb-api PRIVATE ${ZMQ_STATIC_PATH} stdc++)
    if(ZMQ_INCLUDE_DIR)
        target_include_directories(deneb-api PRIVATE ${ZMQ_INCLUDE_DIR})
    endif()
else()
    find_library(ZMQ_LIB zmq)
    if(ZMQ_LIB)
        target_link_libraries(deneb-api PRIVATE ${ZMQ_LIB})
    else()
        message(WARNING "libzmq not found. Set -DZMQ_STATIC_PATH=/path/to/libzmq.a")
    endif()
endif()

target_link_libraries(deneb-api PRIVATE m pthread dl crypt)

# Host stub build (no ZMQ)
option(BUILD_HOST_STUB "Build with stub ZMQ for host testing" OFF)
if(BUILD_HOST_STUB)
    target_compile_definitions(deneb-api PRIVATE BACKEND_ZMQ_STUB)
endif()

install(TARGETS deneb-api DESTINATION /usr/bin)
