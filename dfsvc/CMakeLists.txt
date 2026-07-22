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

set(MBEDTLS_ROOT "" CACHE PATH "Path to static mbedTLS install")
set(ZMQ_STATIC_PATH "" CACHE PATH "Path to static libzmq.a")
set(ZMQ_INCLUDE_DIR "" CACHE PATH "Path to directory containing zmq.h")

add_executable(deneb-dfsvc
    src/main.c
    ../common/print/print_job_file.c
    ../common/print/print_state_rules.c
    ../common/print/print_string.c
    ../common/print/lodepng_standalone_alloc.c
    ../ui/lib/lvgl/src/libs/lodepng/lodepng.c)

set_source_files_properties(
    ../ui/lib/lvgl/src/libs/lodepng/lodepng.c
    PROPERTIES
    COMPILE_DEFINITIONS "LODEPNG_NO_COMPILE_PNG;LODEPNG_NO_COMPILE_ENCODER;LODEPNG_NO_COMPILE_DISK;LODEPNG_NO_COMPILE_ERROR_TEXT"
)

target_compile_definitions(deneb-dfsvc PRIVATE
    DENEB_VERSION="${GIT_VERSION}"
    LV_CONF_INCLUDE_SIMPLE
    LV_DISABLE_API_MAPPING)
target_include_directories(deneb-dfsvc PRIVATE
    src
    ../common/print
    ../ui/config
    ../ui/lib/lvgl
    ../ui/lib/lvgl/include
    ../ui/lib/lvgl/src
    ../ui/lib/lvgl/src/libs/lodepng)

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
        "${MBEDTLS_ROOT}/lib/libmbedcrypto.a")
else()
    find_path(MBEDTLS_INCLUDE_DIR mbedtls/ssl.h)
    find_library(MBEDTLS_LIBRARY NAMES mbedtls)
    find_library(MBEDX509_LIBRARY NAMES mbedx509)
    find_library(MBEDCRYPTO_LIBRARY NAMES mbedcrypto)
    if(NOT MBEDTLS_INCLUDE_DIR OR
       NOT MBEDTLS_LIBRARY OR
       NOT MBEDX509_LIBRARY OR
       NOT MBEDCRYPTO_LIBRARY)
        message(FATAL_ERROR "mbedTLS headers or libraries not found. Set MBEDTLS_ROOT.")
    endif()
    target_include_directories(deneb-dfsvc PRIVATE ${MBEDTLS_INCLUDE_DIR})
    target_link_libraries(deneb-dfsvc PRIVATE
        ${MBEDTLS_LIBRARY} ${MBEDX509_LIBRARY} ${MBEDCRYPTO_LIBRARY})
endif()

target_link_libraries(deneb-dfsvc PRIVATE m pthread)

install(TARGETS deneb-dfsvc DESTINATION /usr/bin)
