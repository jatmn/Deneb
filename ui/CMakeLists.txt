# SPDX-License-Identifier: MPL-2.0
#
# Deneb Touchscreen UI - CMake Build System (LVGL v9)

cmake_minimum_required(VERSION 3.12)
project(deneb-ui C)

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

# LVGL source
set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lib/lvgl" CACHE PATH "LVGL source directory")

if(NOT EXISTS "${LVGL_DIR}/src")
    message(FATAL_ERROR
        "LVGL not found at ${LVGL_DIR}\n"
        "Run: git submodule update --init ui/lib/lvgl\n"
        "Or set -DLVGL_DIR=/path/to/lvgl"
    )
endif()

# LVGL v9 sources. Keep this intentionally narrow: compiling the entire LVGL
# tree pulls in every optional backend, codec, driver, and widget even when
# lv_conf.h disables them at preprocessor time.
set(LVGL_SOURCE_GLOBS
    "${LVGL_DIR}/src/*.c"
    "${LVGL_DIR}/src/core/*.c"
    "${LVGL_DIR}/src/display/*.c"
    "${LVGL_DIR}/src/draw/*.c"
    "${LVGL_DIR}/src/draw/convert/*.c"
    "${LVGL_DIR}/src/draw/sw/*.c"
    "${LVGL_DIR}/src/draw/sw/blend/*.c"
    "${LVGL_DIR}/src/font/lv_font.c"
    "${LVGL_DIR}/src/font/lv_font_montserrat_12.c"
    "${LVGL_DIR}/src/font/lv_font_montserrat_14.c"
    "${LVGL_DIR}/src/font/lv_font_montserrat_16.c"
    "${LVGL_DIR}/src/font/fmt_txt/*.c"
    "${LVGL_DIR}/src/indev/lv_indev.c"
    "${LVGL_DIR}/src/indev/lv_indev_scroll.c"
    "${LVGL_DIR}/src/layouts/lv_layout.c"
    "${LVGL_DIR}/src/layouts/flex/*.c"
    "${LVGL_DIR}/src/libs/bin_decoder/*.c"
    "${LVGL_DIR}/src/misc/*.c"
    "${LVGL_DIR}/src/misc/cache/*.c"
    "${LVGL_DIR}/src/misc/cache/class/lv_cache_lru_rb.c"
    "${LVGL_DIR}/src/misc/cache/class/lv_cache_lru_ll.c"
    "${LVGL_DIR}/src/misc/cache/instance/lv_image_cache.c"
    "${LVGL_DIR}/src/misc/cache/instance/lv_image_header_cache.c"
    "${LVGL_DIR}/src/osal/lv_os.c"
    "${LVGL_DIR}/src/osal/lv_os_none.c"
    "${LVGL_DIR}/src/stdlib/*.c"
    "${LVGL_DIR}/src/stdlib/builtin/*.c"
    "${LVGL_DIR}/src/themes/lv_theme.c"
    "${LVGL_DIR}/src/themes/default/*.c"
    "${LVGL_DIR}/src/tick/*.c"
    "${LVGL_DIR}/src/widgets/bar/*.c"
    "${LVGL_DIR}/src/widgets/button/*.c"
    "${LVGL_DIR}/src/widgets/label/*.c"
    "${LVGL_DIR}/src/widgets/slider/*.c"
)

set(LVGL_SOURCES)
foreach(pattern IN LISTS LVGL_SOURCE_GLOBS)
    file(GLOB matched_sources "${pattern}")
    list(APPEND LVGL_SOURCES ${matched_sources})
endforeach()
list(REMOVE_DUPLICATES LVGL_SOURCES)
list(REMOVE_ITEM LVGL_SOURCES
    "${LVGL_DIR}/src/core/lv_obj_id_builtin.c"
    "${LVGL_DIR}/src/core/lv_obj_property.c"
    "${LVGL_DIR}/src/draw/lv_draw_3d.c"
    "${LVGL_DIR}/src/draw/lv_draw_arc.c"
    "${LVGL_DIR}/src/draw/lv_draw_triangle.c"
    "${LVGL_DIR}/src/draw/lv_draw_vector.c"
)
list(LENGTH LVGL_SOURCES LVGL_SOURCE_COUNT)

# Option: use stub backend (no ZMQ dependency) for host/testing builds
option(BACKEND_COMM_STUB "Use stub backend (no ZMQ)" OFF)

# Platform-specific driver selection
if(WIN32 OR MINGW OR MSYS OR BACKEND_COMM_STUB)
    set(DRIVER_SOURCES
        src/drivers/fb_driver_stub.c
        src/drivers/touch_driver_stub.c
    )
    add_definitions(-DBACKEND_COMM_STUB)
    message(STATUS "Using stub drivers and backend")
else()
    list(APPEND LVGL_SOURCES
        "${LVGL_DIR}/src/drivers/evdev/lv_evdev.c"
    )
    set(DRIVER_SOURCES
        src/drivers/fb_driver.c
        src/drivers/touch_driver.c
    )
    set(DF_BRIDGE_SOURCES
        src/df-bridge/deneb-df-bridge.c
    )
    message(STATUS "Using real Linux drivers")
endif()

# Deneb UI sources
set(DENEB_SOURCES
    src/main.c
    src/app_fonts.c
    src/locale.c
    src/screen_mgr.c
    src/backend_comm.c
    src/net_utils.c
    src/wifi_setup.c
    src/eth_setup.c
    src/screens/screen_home.c
    src/screens/screen_status.c
    src/screens/screen_print.c
    src/screens/screen_maintenance.c
    src/screens/screen_material.c
    src/screens/screen_set_material.c
    src/screens/screen_settings.c
    src/screens/screen_language.c
    src/screens/screen_update.c
    src/screens/screen_network.c
    src/screens/screen_diagnostics.c
    src/screens/screen_level.c
    src/screens/screen_frame_lighting.c
    src/screens/screen_nozzle_size.c
    src/screens/screen_factory_reset.c
    src/screens/screen_digital_factory.c
    src/screens/screen_about.c
    src/screens/screen_jog.c
    src/screens/screen_temp.c
    src/screens/screen_error.c
    src/fonts/deneb_font_i18n_12.c
    src/fonts/deneb_font_i18n_14.c
    src/fonts/deneb_font_i18n_16.c
    ${DRIVER_SOURCES}
    ${DF_BRIDGE_SOURCES}
)

add_executable(deneb-ui ${DENEB_SOURCES} ${LVGL_SOURCES})

target_compile_definitions(deneb-ui PRIVATE LV_DISABLE_API_MAPPING)

target_include_directories(deneb-ui PRIVATE
    ${CMAKE_SOURCE_DIR}/config       # lv_conf.h
    ${CMAKE_SOURCE_DIR}/src          # project headers
    ${LVGL_DIR}/src                  # LVGL internal headers
    ${LVGL_DIR}/include              # LVGL v9 public headers
    ${LVGL_DIR}                      # lvgl.h
)

target_link_libraries(deneb-ui PRIVATE m pthread)

# Link ZMQ when not using stub
if(NOT BACKEND_COMM_STUB AND NOT WIN32)
    # Allow explicit ZMQ path (for cross-compilation)
    set(ZMQ_LIBRARY "" CACHE PATH "Path to libzmq.a")
    if(ZMQ_LIBRARY AND EXISTS "${ZMQ_LIBRARY}")
        target_link_libraries(deneb-ui PRIVATE ${ZMQ_LIBRARY} stdc++)
        # Also add include dir if specified
        set(ZMQ_INCLUDE_DIR "" CACHE PATH "Path to zmq.h")
        if(ZMQ_INCLUDE_DIR)
            target_include_directories(deneb-ui PRIVATE ${ZMQ_INCLUDE_DIR})
        endif()
        message(STATUS "Using explicit libzmq: ${ZMQ_LIBRARY}")
    else()
        find_library(ZMQ_LIB zmq)
        if(ZMQ_LIB)
            target_link_libraries(deneb-ui PRIVATE ${ZMQ_LIB})
            message(STATUS "Found libzmq: ${ZMQ_LIB}")
        else()
            find_library(ZMQ_LIB_STATIC libzmq.a PATHS /usr/lib /usr/local/lib /usr/lib/mipsel-linux-gnu)
            if(ZMQ_LIB_STATIC)
                target_link_libraries(deneb-ui PRIVATE ${ZMQ_LIB_STATIC})
                message(STATUS "Found static libzmq: ${ZMQ_LIB_STATIC}")
            else()
                message(WARNING "libzmq not found. Install libzmq-dev or pass -DZMQ_LIBRARY=/path/to/libzmq.a")
            endif()
        endif()
    endif()
endif()

install(TARGETS deneb-ui DESTINATION /usr/bin)

message(STATUS "Deneb UI build configuration:")
message(STATUS "  Version:    ${GIT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  LVGL dir:   ${LVGL_DIR}")
message(STATUS "  LVGL srcs:  ${LVGL_SOURCE_COUNT}")
message(STATUS "  Platform:   ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  Stub mode:  ${BACKEND_COMM_STUB}")
