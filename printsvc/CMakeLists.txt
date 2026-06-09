# SPDX-License-Identifier: MPL-2.0
#
# Deneb native print service.

cmake_minimum_required(VERSION 3.12)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR
        "Visual Studio/MSVC generators are not supported for deneb-printsvc. "
        "Use WSL/Linux with gcc or the MIPS musl toolchain instead."
    )
endif()

project(deneb-printsvc C)

if(MSVC)
    message(FATAL_ERROR
        "MSVC/Visual Studio builds are not supported for deneb-printsvc. "
        "Use WSL/Linux with gcc or the MIPS musl toolchain instead."
    )
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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

option(BUILD_HOST_STUB "Build without libzmq or serial device dependencies" OFF)

set(DENEB_PRINTSVC_LIB_SOURCES
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
    src/command.c
    src/command_audit.c
    src/command_dispatch.c
    src/command_reply.c
    src/crc.c
    src/diagnostics_log.c
    src/error_map.c
    src/flow_control.c
    src/gcode_control.c
    src/gcode_rewrite.c
    src/gcode_stream.c
    src/heater_wait.c
    src/ipc_frame.c
    src/job_control.c
    src/job_lifecycle.c
    src/job_streamer.c
    src/macro_control.c
    src/macro_registry.c
    src/macro_runner.c
    src/marlin_packet.c
    src/motion_firmware.c
    src/motion_observer.c
    src/motion_policy.c
    src/motion_runtime.c
    src/motion_send_error.c
    src/motion_sender.c
    src/pause_resume.c
    src/pause_resume_control.c
    src/pending_job.c
    src/pending_job_registration.c
    src/print_control.c
    src/runtime_diagnostics.c
    src/serial_transport.c
    src/sha256.c
    src/service.c
    src/service_command.c
    src/service_context.c
    src/status.c
    src/status_parser.c
)

add_library(deneb-printsvc-core STATIC ${DENEB_PRINTSVC_LIB_SOURCES})
target_include_directories(deneb-printsvc-core PUBLIC src ../common/print)
target_compile_definitions(deneb-printsvc-core PUBLIC DENEB_VERSION="${GIT_VERSION}")

add_executable(deneb-printsvc
    src/main.c
    src/ipc_zmq.c
)
target_link_libraries(deneb-printsvc PRIVATE deneb-printsvc-core)

if(BUILD_HOST_STUB)
    target_compile_definitions(deneb-printsvc PRIVATE DENEB_PRINTSVC_HOST_STUB)
else()
    set(ZMQ_STATIC_PATH "" CACHE PATH "Path to static libzmq.a")
    set(ZMQ_INCLUDE_DIR "" CACHE PATH "Path to directory containing zmq.h")
    if(ZMQ_STATIC_PATH AND EXISTS "${ZMQ_STATIC_PATH}")
        target_link_libraries(deneb-printsvc PRIVATE ${ZMQ_STATIC_PATH} stdc++)
        if(ZMQ_INCLUDE_DIR)
            target_include_directories(deneb-printsvc PRIVATE ${ZMQ_INCLUDE_DIR})
        endif()
    else()
        find_library(ZMQ_LIB zmq)
        if(ZMQ_LIB)
            target_link_libraries(deneb-printsvc PRIVATE ${ZMQ_LIB})
        else()
            message(WARNING "libzmq not found. Configure with -DBUILD_HOST_STUB=ON for host tests or provide libzmq for target builds.")
        endif()
    endif()
endif()

target_link_libraries(deneb-printsvc PRIVATE m pthread)

add_executable(deneb-printsvc-tests
    tests/test_printsvc.c
)
target_link_libraries(deneb-printsvc-tests PRIVATE deneb-printsvc-core)

enable_testing()
add_test(NAME deneb-printsvc-tests COMMAND deneb-printsvc-tests)

find_program(SH_EXECUTABLE sh)
if(SH_EXECUTABLE)
    add_test(
        NAME deneb-printsvc-smoke-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-smoke-selftest.sh"
    )
    add_test(
        NAME deneb-printsvc-cli-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-cli-selftest.sh"
                "$<TARGET_FILE:deneb-printsvc>"
    )
    add_test(
        NAME deneb-printsvc-init-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-init-selftest.sh"
    )
    add_test(
        NAME deneb-printsvc-depython-audit
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-native-audit.sh"
                --source
                "${CMAKE_SOURCE_DIR}/.."
    )
    add_test(
        NAME deneb-printsvc-native-audit-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-native-audit-selftest.sh"
    )
    add_test(
        NAME deneb-printsvc-integration-audit
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-integration-audit.sh"
                --source
                "${CMAKE_SOURCE_DIR}/.."
    )
    add_test(
        NAME deneb-printsvc-integration-audit-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-integration-audit-selftest.sh"
    )
    add_test(
        NAME deneb-printsvc-release-gate-selftest
        COMMAND "${SH_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/../tools/deneb-printsvc-release-gate-selftest.sh"
    )
else()
    message(WARNING "sh not found; deneb-printsvc shell selftests will not be registered with CTest.")
endif()

install(TARGETS deneb-printsvc DESTINATION /usr/bin)
