# SPDX-License-Identifier: MPL-2.0
#
# Deneb API Server - CMake Build System
# Implements UltiMaker REST API v1 for Cura LAN compatibility.

cmake_minimum_required(VERSION 3.12)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR
        "Visual Studio/MSVC generators are not supported for deneb-api. "
        "Use WSL/Linux with gcc or the MIPS musl toolchain instead."
    )
endif()

project(deneb-api C)

if(MSVC)
    message(FATAL_ERROR
        "MSVC/Visual Studio builds are not supported for deneb-api. "
        "Use WSL/Linux with gcc or the MIPS musl toolchain instead. "
        "For a host verification build from Windows, run through WSL, e.g. "
        "convert the repository path with wslpath, then run "
        "cmake -S web -B /tmp/deneb-web-build -DBUILD_HOST_STUB=ON && cmake --build /tmp/deneb-web-build"
    )
endif()

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
    src/api_multipart.c
    src/api_http.c
    src/api_printer.c
    src/api_print_job.c
    src/api_system.c
    src/api_materials.c
    src/api_cluster.c
    src/api_auth.c
    src/api_deneb.c
    src/backend_zmq.c
    src/json_writer.c
    src/md5.c
    ../common/print/buildplate_level.c
    ../common/print/command_format.c
    ../common/print/diagnostics_export.c
    ../common/print/frame_light.c
    ../common/print/gcode_command.c
    ../common/print/json_field.c
    ../common/print/json_file.c
    ../common/print/json_string.c
    ../common/print/manual_motion.c
    ../common/print/material_catalog.c
    ../common/print/material_workflow.c
    ../common/print/printer_identity.c
    ../common/print/print_backend_route.c
    ../common/print/pending_job_dispatch.c
    ../common/print/pending_job_file.c
    ../common/print/print_history.c
    ../common/print/print_job_file.c
    ../common/print/print_job_summary.c
    ../common/print/print_profile.c
    ../common/print/print_state_rules.c
    ../common/print/status_payload.c
    ../common/print/system_language.c
    ../printsvc/src/pending_job.c
    ../printsvc/src/pending_job_registration.c
    ../printsvc/src/print_control.c
)

add_executable(deneb-api ${DENEB_API_SOURCES})
target_include_directories(deneb-api PRIVATE src ../common/print ../printsvc/src)

add_executable(deneb-mdns src/mdns_advertiser.c)
target_compile_definitions(deneb-mdns PRIVATE DENEB_VERSION="${GIT_VERSION}")

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

install(TARGETS deneb-api deneb-mdns DESTINATION /usr/bin)
