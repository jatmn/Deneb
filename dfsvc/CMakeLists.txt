# SPDX-License-Identifier: MPL-2.0
#
# Deneb native Digital Factory connector.

cmake_minimum_required(VERSION 3.12)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "Visual Studio/MSVC generators are not supported for deneb-dfsvc.")
endif()

project(deneb-dfsvc C)

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
endif()

set(MBEDTLS_ROOT "" CACHE PATH "Path to static Mbed TLS 4.1 install")
set(ZMQ_STATIC_PATH "" CACHE PATH "Path to static libzmq.a")
set(ZMQ_INCLUDE_DIR "" CACHE PATH "Path to directory containing zmq.h")

add_executable(deneb-dfsvc
    src/main.c
    ../common/print/print_job_file.c
    ../common/print/print_state_rules.c
    ../common/print/print_string.c
    ../common/lodepng/lodepng.c)

set_source_files_properties(
    ../common/lodepng/lodepng.c
    PROPERTIES
    COMPILE_DEFINITIONS "LODEPNG_NO_COMPILE_PNG;LODEPNG_NO_COMPILE_ENCODER;LODEPNG_NO_COMPILE_DISK;LODEPNG_NO_COMPILE_ERROR_TEXT"
)

target_compile_definitions(deneb-dfsvc PRIVATE
    DENEB_VERSION="${GIT_VERSION}")
target_include_directories(deneb-dfsvc PRIVATE
    src
    ../common/print
    ../common/lodepng)

if(ZMQ_INCLUDE_DIR)
    target_include_directories(deneb-dfsvc PRIVATE ${ZMQ_INCLUDE_DIR})
endif()
if(ZMQ_STATIC_PATH AND EXISTS "${ZMQ_STATIC_PATH}")
    target_link_libraries(deneb-dfsvc PRIVATE ${ZMQ_STATIC_PATH} stdc++)
else()
    find_library(ZMQ_LIB zmq)
    if(ZMQ_LIB)
        target_link_libraries(deneb-dfsvc PRIVATE ${ZMQ_LIB})
    else()
        message(FATAL_ERROR "libzmq not found. Set ZMQ_STATIC_PATH.")
    endif()
endif()

if(MBEDTLS_ROOT)
    target_include_directories(deneb-dfsvc PRIVATE "${MBEDTLS_ROOT}/include")
    target_link_libraries(deneb-dfsvc PRIVATE
        "${MBEDTLS_ROOT}/lib/libmbedtls.a"
        "${MBEDTLS_ROOT}/lib/libmbedx509.a"
        "${MBEDTLS_ROOT}/lib/libtfpsacrypto.a")
else()
    find_package(MbedTLS 4.1 CONFIG REQUIRED)
    target_link_libraries(deneb-dfsvc PRIVATE MbedTLS::mbedtls)
endif()

target_link_libraries(deneb-dfsvc PRIVATE m pthread)

install(TARGETS deneb-dfsvc DESTINATION /usr/bin)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    foreach(test IN ITEMS reconnect tls ingestion)
        set(test_target deneb-df-${test}-tests)
        add_executable(${test_target} tests/test_${test}.c)
        target_include_directories(${test_target} PRIVATE
            $<TARGET_PROPERTY:deneb-dfsvc,INCLUDE_DIRECTORIES>)
        target_compile_definitions(${test_target} PRIVATE
            $<TARGET_PROPERTY:deneb-dfsvc,COMPILE_DEFINITIONS>)
        # Assert checks also drive fixture operations in Release builds.
        target_compile_options(${test_target} PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
        target_link_options(${test_target} PRIVATE -Wl,--gc-sections)
        target_link_libraries(${test_target} PRIVATE
            $<TARGET_PROPERTY:deneb-dfsvc,LINK_LIBRARIES>)
    endforeach()
    add_test(NAME deneb-df-reconnect-tests COMMAND deneb-df-reconnect-tests)
    add_test(NAME deneb-df-tls-tests COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_tls.sh"
        $<TARGET_FILE:deneb-df-tls-tests>)
    set_tests_properties(deneb-df-tls-tests PROPERTIES TIMEOUT 15)
    add_test(NAME deneb-df-ingestion-tests COMMAND deneb-df-ingestion-tests)
    set_tests_properties(deneb-df-ingestion-tests PROPERTIES TIMEOUT 10)
endif()
