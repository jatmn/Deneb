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
    src/df_bridge.c
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
    ../common/print/lodepng_standalone_alloc.c
    ../common/print/manual_motion.c
    ../common/print/material_catalog.c
    ../common/print/material_workflow.c
    ../common/print/printer_identity.c
    ../common/print/print_backend_route.c
    ../common/print/pending_job_dispatch.c
    ../common/print/pending_job_file.c
    ../common/print/print_action_rules.c
    ../common/print/print_history.c
    ../common/print/print_action_dispatch.c
    ../common/print/print_job_file.c
    ../common/print/print_job_summary.c
    ../common/print/print_profile.c
    ../common/print/print_state_rules.c
    ../common/print/print_string.c
    ../common/print/print_timing_rules.c
    ../common/print/printer_status_response.c
    ../common/print/status_payload.c
    ../common/print/status_state.c
    ../common/print/system_language.c
    ../ui/lib/lvgl/src/libs/lodepng/lodepng.c
    ../printsvc/src/pending_job.c
    ../printsvc/src/pending_job_registration.c
    ../printsvc/src/print_control.c
)

set_source_files_properties(
    ../ui/lib/lvgl/src/libs/lodepng/lodepng.c
    PROPERTIES
    COMPILE_DEFINITIONS "LODEPNG_NO_COMPILE_PNG;LODEPNG_NO_COMPILE_ENCODER;LODEPNG_NO_COMPILE_DISK;LODEPNG_NO_COMPILE_ERROR_TEXT"
)

add_executable(deneb-api ${DENEB_API_SOURCES})
target_include_directories(deneb-api PRIVATE
    src
    ../common/print
    ../printsvc/src
    ../ui/config
    ../ui/lib/lvgl
    ../ui/lib/lvgl/include
    ../ui/lib/lvgl/src
    ../ui/lib/lvgl/src/libs/lodepng
)
target_compile_definitions(deneb-api PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_DISABLE_API_MAPPING
)

# Host stub build (no ZMQ)
option(BUILD_HOST_STUB "Build with stub ZMQ for host testing" OFF)

add_executable(deneb-mdns src/mdns_advertiser.c)
target_compile_definitions(deneb-mdns PRIVATE DENEB_VERSION="${GIT_VERSION}")

# ZMQ (static link for MIPS target)
if(NOT BUILD_HOST_STUB)
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
endif()

target_link_libraries(deneb-api PRIVATE m pthread crypt)

if(BUILD_HOST_STUB)
    target_compile_definitions(deneb-api PRIVATE BACKEND_ZMQ_STUB)
endif()

install(TARGETS deneb-api deneb-mdns DESTINATION /usr/bin)

# Host-only connection regressions exercise production server/client code.
if(NOT CMAKE_CROSSCOMPILING)
enable_testing()
add_executable(deneb-sse-tests tests/test_sse.c)
target_include_directories(deneb-sse-tests PRIVATE src ../common/print)
target_compile_options(deneb-sse-tests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(deneb-sse-tests PRIVATE -Wl,--gc-sections)
add_test(NAME deneb-sse-tests COMMAND deneb-sse-tests)
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME deneb-browser-sse-tests
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_api_sse.cjs")
endif()

find_path(ZMQ_TEST_INCLUDE_DIR zmq.h)
find_library(ZMQ_TEST_LIBRARY zmq)
if(ZMQ_TEST_INCLUDE_DIR AND ZMQ_TEST_LIBRARY)
    foreach(client ui web)
        add_executable(deneb-${client}-rpc-tests tests/test_backend_rpc.c
            ../common/print/command_format.c ../common/print/json_field.c)
        target_include_directories(deneb-${client}-rpc-tests PRIVATE
            src ../ui/src ../common/print "${ZMQ_TEST_INCLUDE_DIR}")
        target_compile_options(deneb-${client}-rpc-tests PRIVATE -ffunction-sections -fdata-sections)
        target_link_options(deneb-${client}-rpc-tests PRIVATE -Wl,--gc-sections)
        target_link_libraries(deneb-${client}-rpc-tests PRIVATE "${ZMQ_TEST_LIBRARY}" pthread)
        if(client STREQUAL "web")
            target_compile_definitions(deneb-${client}-rpc-tests PRIVATE TEST_WEB_BACKEND)
        endif()
        add_test(NAME deneb-${client}-rpc-tests COMMAND deneb-${client}-rpc-tests)
    endforeach()
endif()
if(ZMQ_TEST_INCLUDE_DIR)
    add_executable(deneb-df-bridge-tests tests/test_df_bridge.c)
    target_include_directories(deneb-df-bridge-tests PRIVATE src "${ZMQ_TEST_INCLUDE_DIR}")
    target_compile_options(deneb-df-bridge-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-df-bridge-tests PRIVATE -Wl,--gc-sections)
    add_test(NAME deneb-df-bridge-tests COMMAND deneb-df-bridge-tests)
endif()

endif()
