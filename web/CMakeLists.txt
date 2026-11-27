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
    ../common/lodepng/lodepng.c
    ../printsvc/src/pending_job.c
    ../printsvc/src/pending_job_registration.c
    ../printsvc/src/print_control.c
)

set_source_files_properties(
    ../common/lodepng/lodepng.c
    PROPERTIES
    COMPILE_DEFINITIONS "LODEPNG_NO_COMPILE_PNG;LODEPNG_NO_COMPILE_ENCODER;LODEPNG_NO_COMPILE_DISK;LODEPNG_NO_COMPILE_ERROR_TEXT"
)

set(DENEB_API_INCLUDE_DIRS
    src
    ../common/print
    ../printsvc/src
    ../common/lodepng
)
add_executable(deneb-api ${DENEB_API_SOURCES})
target_include_directories(deneb-api PRIVATE ${DENEB_API_INCLUDE_DIRS})

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
add_executable(deneb-mdns-tests tests/test_mdns.c)
target_compile_options(deneb-mdns-tests PRIVATE -UNDEBUG)
foreach(function popen fopen getifaddrs freeifaddrs if_nametoindex inet_ntop
                 socket bind fcntl close setsockopt clock_gettime select recvfrom sendto)
    target_link_options(deneb-mdns-tests PRIVATE "-Wl,--wrap=${function}")
endforeach()
add_test(NAME deneb-mdns-tests COMMAND deneb-mdns-tests)
set_tests_properties(deneb-mdns-tests PROPERTIES TIMEOUT 5)
add_executable(deneb-sse-tests tests/test_sse.c)
target_include_directories(deneb-sse-tests PRIVATE src ../common/print)
target_compile_options(deneb-sse-tests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(deneb-sse-tests PRIVATE -Wl,--gc-sections)
add_test(NAME deneb-sse-tests COMMAND deneb-sse-tests)
find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME deneb-browser-sse-tests
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_api_sse.cjs")
    add_test(NAME deneb-browser-motion-tests
        COMMAND "${NODE_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_ui_motion.cjs")
    if(BUILD_HOST_STUB)
        # Use the normal API code with an isolated, per-test working directory.
        add_executable(deneb-api-upload-fixture ${DENEB_API_SOURCES} tests/small_send_buffer.c)
        target_link_options(deneb-api-upload-fixture PRIVATE
            -Wl,--wrap=accept4 -Wl,--wrap=backend_zmq_get_status_event)
        target_include_directories(deneb-api-upload-fixture PRIVATE ${DENEB_API_INCLUDE_DIRS})
        target_compile_definitions(deneb-api-upload-fixture PRIVATE
            BACKEND_ZMQ_STUB
            DENEB_API_SOCKET_PATH="api.sock"
            DENEB_API_AUTH_CONFIG_PATH="web-auth.conf"
            DENEB_API_LOCALE_DIR="locales"
            DENEB_CLUSTER_UCI_CONFIG_DIR="config"
            DENEB_CLUSTER_UCI_DELTA_DIR="delta"
            DENEB_CLUSTER_UCI_OVERRIDE_DIR="override"
            DENEB_UPLOAD_TEMP_DIR="tmp"
            DENEB_PRINT_JOB_SPOOL_DIR="spool"
            DENEB_PRINT_HISTORY_PATH="tmp/deneb-print-history.json"
            DENEB_PENDING_JOB_PATH="tmp/deneb-cluster-print-job.json"
            DENEB_ACTIVE_THUMB_PATH="deneb-active-thumb.rgb565")
        target_link_libraries(deneb-api-upload-fixture PRIVATE m pthread crypt)
        add_test(NAME deneb-cluster-configuration-tests COMMAND "${NODE_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_cluster_configuration.cjs"
            "$<TARGET_FILE:deneb-api-upload-fixture>")
        set_tests_properties(deneb-cluster-configuration-tests PROPERTIES TIMEOUT 20)

        find_program(LIGHTTPD_EXECUTABLE lighttpd)
        find_program(UNZIP_EXECUTABLE unzip)
        if(LIGHTTPD_EXECUTABLE AND UNZIP_EXECUTABLE)
            add_executable(deneb-upload-digest-fixture tests/upload_digest_fixture.c src/md5.c)
            target_include_directories(deneb-upload-digest-fixture PRIVATE src)
            add_test(NAME deneb-proxy-upload-tests COMMAND "${NODE_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_proxy_upload.cjs"
                "$<TARGET_FILE:deneb-api-upload-fixture>" "${LIGHTTPD_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/lighttpd.conf" "${CMAKE_COMMAND}" "${UNZIP_EXECUTABLE}"
                "$<TARGET_FILE:deneb-upload-digest-fixture>")
            set_tests_properties(deneb-proxy-upload-tests PROPERTIES TIMEOUT 20)
        endif()

        add_executable(deneb-print-history-fixture tests/print_history_fixture.c
            ../common/print/print_history.c ../common/print/json_string.c)
        target_include_directories(deneb-print-history-fixture PRIVATE ../common/print)
        target_compile_definitions(deneb-print-history-fixture PRIVATE
            DENEB_PRINT_HISTORY_PATH="tmp/deneb-print-history.json")
        target_link_options(deneb-print-history-fixture PRIVATE -Wl,--wrap=rename)
        add_test(NAME deneb-print-history-tests COMMAND "${NODE_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/test_print_history.cjs"
            "$<TARGET_FILE:deneb-api-upload-fixture>" "$<TARGET_FILE:deneb-print-history-fixture>")
        set_tests_properties(deneb-print-history-tests PROPERTIES TIMEOUT 20)
    endif()
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
            # The status projection regression drives the real subscription path.
            target_sources(deneb-${client}-rpc-tests PRIVATE
                ../common/print/json_string.c
                ../common/print/pending_job_file.c
                ../common/print/print_backend_route.c
                ../common/print/print_history.c
                ../common/print/print_state_rules.c
                ../common/print/print_string.c
                ../common/print/print_timing_rules.c
                ../common/print/printer_status_response.c
                ../common/print/status_payload.c
                ../common/print/status_state.c)
        endif()
        add_test(NAME deneb-${client}-rpc-tests COMMAND deneb-${client}-rpc-tests)
        set_tests_properties(deneb-${client}-rpc-tests PROPERTIES TIMEOUT 10)
    endforeach()
endif()
if(ZMQ_TEST_INCLUDE_DIR AND ZMQ_TEST_LIBRARY)
    add_executable(deneb-df-bridge-tests tests/test_df_bridge.c)
    target_include_directories(deneb-df-bridge-tests PRIVATE src "${ZMQ_TEST_INCLUDE_DIR}")
    target_compile_options(deneb-df-bridge-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-df-bridge-tests PRIVATE -Wl,--gc-sections)
    target_link_libraries(deneb-df-bridge-tests PRIVATE "${ZMQ_TEST_LIBRARY}")
    add_test(NAME deneb-df-bridge-tests COMMAND deneb-df-bridge-tests)
endif()

endif()
