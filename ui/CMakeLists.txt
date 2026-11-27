# SPDX-License-Identifier: MPL-2.0
#
# Deneb Touchscreen UI - CMake Build System (LVGL v9)

cmake_minimum_required(VERSION 3.12)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR
        "Visual Studio/MSVC generators are not supported for deneb-ui. "
        "Use the WSL/Linux host build or the MIPS musl release build instead."
    )
endif()

project(deneb-ui C)

if(MSVC)
    message(FATAL_ERROR
        "MSVC/Visual Studio builds are not supported for deneb-ui. "
        "Use the WSL/Linux host build or the MIPS musl release build instead. "
        "From Windows, run tools/build-ui-host.ps1 for host verification."
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

# LVGL source
set(LVGL_DIR "${CMAKE_SOURCE_DIR}/lib/lvgl" CACHE PATH "LVGL source directory")
get_filename_component(LVGL_DIR "${LVGL_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT EXISTS "${LVGL_DIR}/src")
    message(FATAL_ERROR
        "LVGL not found at ${LVGL_DIR}\n"
        "Run: git submodule update --init ui/lib/lvgl\n"
        "Or set -DLVGL_DIR=/path/to/lvgl"
    )
endif()

set(LODEPNG_DIR "${CMAKE_SOURCE_DIR}/../common/lodepng")

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
    "${LVGL_DIR}/src/fs/lv_fs.c"
    "${LVGL_DIR}/src/fs/lv_fs_posix.c"
    "${LVGL_DIR}/src/image/lv_bin_decoder.c"
    "${LVGL_DIR}/src/image/lv_image_decoder.c"
    "${LVGL_DIR}/src/image/lv_lodepng.c"
    "${LVGL_DIR}/src/indev/lv_indev.c"
    "${LVGL_DIR}/src/indev/lv_indev_scroll.c"
    "${LVGL_DIR}/src/layouts/lv_layout.c"
    "${LVGL_DIR}/src/layouts/flex/*.c"
    "${LVGL_DIR}/src/logging/lv_log.c"
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
    "${LVGL_DIR}/src/widgets/image/*.c"
    "${LVGL_DIR}/src/widgets/label/*.c"
    "${LVGL_DIR}/src/widgets/slider/*.c"
    "${LVGL_DIR}/src/widgets/switch/*.c"
)

set(LVGL_SOURCES)
foreach(pattern IN LISTS LVGL_SOURCE_GLOBS)
    file(GLOB matched_sources "${pattern}")
    list(APPEND LVGL_SOURCES ${matched_sources})
endforeach()
list(REMOVE_DUPLICATES LVGL_SOURCES)

# Apply the v9.6 fixes to copies of the sources used by the pinned build.
# A custom LVGL checkout can target a different revision.
get_filename_component(LVGL_SELECTED_REAL "${LVGL_DIR}" REALPATH)
get_filename_component(LVGL_PINNED_REAL "${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl" REALPATH)
if(LVGL_SELECTED_REAL STREQUAL LVGL_PINNED_REAL)
    find_program(LVGL_PATCH_PROGRAM NAMES patch NO_CMAKE_FIND_ROOT_PATH)
    if(NOT LVGL_PATCH_PROGRAM)
        message(FATAL_ERROR "The host patch utility is required to build with Deneb's pinned LVGL sources.")
    endif()
    set(LVGL_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/lvgl-v9.6-code-scanning.patch")
    set(LVGL_PATCH_INPUT "${CMAKE_CURRENT_BINARY_DIR}/lvgl-patch-input")
    set(LVGL_PATCH_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lvgl-patched")
    set(LVGL_PATCH_SOURCES
        src/core/lv_refr.c
        src/draw/lv_draw_buf.c
        src/draw/sw/lv_draw_sw_arc.c
        src/draw/sw/lv_draw_sw_box_shadow.c
        src/draw/sw/lv_draw_sw_img.c
        src/fs/lv_fs.c
        src/misc/lv_array.c
        src/misc/lv_text.c
        src/themes/default/lv_theme_default.c
    )
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LVGL_PATCH}")
    foreach(source IN LISTS LVGL_PATCH_SOURCES)
        set(original "${LVGL_DIR}/${source}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
        file(READ "${original}" contents)
        string(REPLACE "\r\n" "\n" contents "${contents}")
        file(WRITE "${LVGL_PATCH_INPUT}/${source}" "${contents}")
    endforeach()
    # Apply the patch to the staged files with exact context matching.
    execute_process(
        COMMAND "${LVGL_PATCH_PROGRAM}" -f -F 0 -p1 -i "${LVGL_PATCH}"
        WORKING_DIRECTORY "${LVGL_PATCH_INPUT}"
        RESULT_VARIABLE LVGL_PATCH_RESULT
        OUTPUT_VARIABLE LVGL_PATCH_LOG
        ERROR_VARIABLE LVGL_PATCH_LOG
    )
    if(NOT LVGL_PATCH_RESULT STREQUAL "0")
        message(FATAL_ERROR "Could not apply the Deneb LVGL patch: ${LVGL_PATCH_LOG}")
    endif()
    foreach(source IN LISTS LVGL_PATCH_SOURCES)
        set(original "${LVGL_DIR}/${source}")
        set(patched "${LVGL_PATCH_OUTPUT}/${source}")
        configure_file("${LVGL_PATCH_INPUT}/${source}" "${patched}" COPYONLY)
        get_filename_component(original_dir "${original}" DIRECTORY)
        set_source_files_properties("${patched}" PROPERTIES INCLUDE_DIRECTORIES "${original_dir}")
        list(REMOVE_ITEM LVGL_SOURCES "${original}")
        list(APPEND LVGL_SOURCES "${patched}")
    endforeach()
endif()

# Let a failed rounded-border mask allocation reach LVGL's recovery branch.
# Keep the pinned checkout clean and leave other assertions enabled.
set(LVGL_BORDER_SOURCE "${LVGL_DIR}/src/draw/sw/lv_draw_sw_border.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${LVGL_BORDER_SOURCE}")
file(READ "${LVGL_BORDER_SOURCE}" LVGL_BORDER_TEXT)
string(REPLACE "\r\n" "\n" LVGL_BORDER_TEXT "${LVGL_BORDER_TEXT}")
string(REPLACE "    LV_ASSERT_MALLOC(mask_buf);\n    if(!mask_buf) {"
               "    if(!mask_buf) {"
       LVGL_BORDER_FIXED "${LVGL_BORDER_TEXT}")
if(NOT LVGL_BORDER_FIXED STREQUAL LVGL_BORDER_TEXT)
    set(LVGL_BORDER_COPY "${CMAKE_CURRENT_BINARY_DIR}/lv_draw_sw_border.c")
    file(GENERATE OUTPUT "${LVGL_BORDER_COPY}" CONTENT "${LVGL_BORDER_FIXED}")
    list(REMOVE_ITEM LVGL_SOURCES "${LVGL_BORDER_SOURCE}")
    list(APPEND LVGL_SOURCES "${LVGL_BORDER_COPY}")
    set_source_files_properties("${LVGL_BORDER_COPY}" PROPERTIES
        INCLUDE_DIRECTORIES "${LVGL_DIR}/src/draw/sw")
endif()

# The LVGL adapter includes its bundled header by relative path.
set_source_files_properties(
    "${LVGL_DIR}/src/image/lv_lodepng.c"
    PROPERTIES COMPILE_OPTIONS "-include;${LODEPNG_DIR}/lodepng.h"
)
set_source_files_properties(
    "${LODEPNG_DIR}/lodepng.c"
    PROPERTIES COMPILE_DEFINITIONS DENEB_LODEPNG_LVGL
)

list(REMOVE_ITEM LVGL_SOURCES
    "${LVGL_DIR}/src/core/lv_obj_id_builtin.c"
    "${LVGL_DIR}/src/core/lv_obj_property.c"
    "${LVGL_DIR}/src/draw/lv_draw_3d.c"
    "${LVGL_DIR}/src/draw/lv_draw_triangle.c"
    "${LVGL_DIR}/src/draw/lv_draw_vector.c"
)

if(NOT WIN32)
    # lv_conf.h enables LV_USE_EVDEV, and lv_init.c calls lv_evdev_deinit()
    # even when Deneb's own touch driver is stubbed for host testing.
    list(APPEND LVGL_SOURCES
        "${LVGL_DIR}/src/drivers/evdev/lv_evdev.c"
    )
endif()

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
    set(DRIVER_SOURCES
        src/drivers/fb_driver.c
        src/drivers/touch_driver.c
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
    src/assets/deneb_logo_116.c
    src/assets/ic_nozzle_16.c
    src/assets/ic_bed_16.c
    src/screens/screen_print.c
    src/screens/screen_print_conflict.c
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
    ../common/print/background_action.c
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
    ../common/print/print_profile.c
    ../common/print/print_state_rules.c
    ../common/print/print_string.c
    ../common/print/print_timing_rules.c
    ../common/print/status_payload.c
    ../common/print/status_state.c
    ../common/print/system_language.c
    ${DRIVER_SOURCES}
)

set(DENEB_UI_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/config       # lv_conf.h
    ${CMAKE_SOURCE_DIR}/src          # project headers
    ${CMAKE_SOURCE_DIR}/../common/print
    ${LVGL_DIR}/src                  # LVGL internal headers
    ${LODEPNG_DIR}
    ${LVGL_DIR}/include              # LVGL v9 public headers
    ${LVGL_DIR}                      # lvgl.h
)

add_library(deneb-lvgl OBJECT ${LVGL_SOURCES} "${LODEPNG_DIR}/lodepng.c")
target_compile_definitions(deneb-lvgl PUBLIC LV_DISABLE_API_MAPPING LODEPNG_NO_COMPILE_ENCODER)
target_include_directories(deneb-lvgl PRIVATE ${DENEB_UI_INCLUDE_DIRS})

add_executable(deneb-ui ${DENEB_SOURCES})
target_include_directories(deneb-ui PRIVATE ${DENEB_UI_INCLUDE_DIRS})
target_link_libraries(deneb-ui PRIVATE deneb-lvgl m pthread)

if(BACKEND_COMM_STUB AND NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(deneb-touchscreen-motion-tests
        tests/test_motion_workflows.c
        ../common/print/buildplate_level.c
        ../common/print/gcode_command.c
        ../common/print/manual_motion.c
        ../common/print/material_workflow.c
        ../common/print/print_state_rules.c
        ../common/print/status_state.c)
    target_include_directories(deneb-touchscreen-motion-tests PRIVATE
        $<TARGET_PROPERTY:deneb-ui,INCLUDE_DIRECTORIES>)
    target_compile_definitions(deneb-touchscreen-motion-tests PRIVATE LV_DISABLE_API_MAPPING)
    target_compile_options(deneb-touchscreen-motion-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-touchscreen-motion-tests PRIVATE -Wl,--gc-sections)
    target_link_libraries(deneb-touchscreen-motion-tests PRIVATE m)
    add_test(NAME deneb-touchscreen-motion-tests COMMAND deneb-touchscreen-motion-tests)

    add_executable(deneb-ui-loop-tests tests/test_main_loop.c)
    target_include_directories(deneb-ui-loop-tests PRIVATE
        $<TARGET_PROPERTY:deneb-ui,INCLUDE_DIRECTORIES>)
    target_compile_definitions(deneb-ui-loop-tests PRIVATE LV_DISABLE_API_MAPPING)
    target_compile_options(deneb-ui-loop-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-ui-loop-tests PRIVATE -Wl,--gc-sections)
    add_test(NAME deneb-ui-loop-tests COMMAND deneb-ui-loop-tests)

    add_executable(deneb-locale-tests tests/test_locale.c src/locale.c)
    target_include_directories(deneb-locale-tests PRIVATE src)
    target_compile_definitions(deneb-locale-tests PRIVATE LOCALE_DIR=".")
    add_test(NAME deneb-locale-tests
        COMMAND deneb-locale-tests "${CMAKE_CURRENT_SOURCE_DIR}/locales")

    add_executable(deneb-network-locale-tests
        tests/test_network_locale.c src/locale.c src/wifi_setup.c src/eth_setup.c
        src/net_utils.c ../common/print/print_string.c)
    target_include_directories(deneb-network-locale-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_compile_definitions(deneb-network-locale-tests PRIVATE LV_DISABLE_API_MAPPING LOCALE_DIR=".")
    target_compile_options(deneb-network-locale-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-network-locale-tests PRIVATE -Wl,--gc-sections
        -Wl,--wrap=system -Wl,--wrap=net_find_usb_config -Wl,--wrap=net_read_command
        -Wl,--wrap=locale_get -Wl,--wrap=locale_format_s -Wl,--wrap=locale_format_ss)
    target_link_libraries(deneb-network-locale-tests PRIVATE pthread)
    add_test(NAME deneb-network-locale-tests COMMAND deneb-network-locale-tests)
    set_tests_properties(deneb-network-locale-tests PROPERTIES TIMEOUT 10)

    add_executable(deneb-digital-factory-status-tests
        tests/test_digital_factory_status.c src/locale.c)
    target_include_directories(deneb-digital-factory-status-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_link_libraries(deneb-digital-factory-status-tests PRIVATE deneb-lvgl m pthread)
    target_link_options(deneb-digital-factory-status-tests PRIVATE -Wl,--wrap=system)
    add_test(NAME deneb-digital-factory-status-tests COMMAND deneb-digital-factory-status-tests)
    set_tests_properties(deneb-digital-factory-status-tests PROPERTIES TIMEOUT 10)

    add_executable(deneb-lvgl-png-tests tests/test_lodepng.c)
    target_include_directories(deneb-lvgl-png-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_link_libraries(deneb-lvgl-png-tests PRIVATE deneb-lvgl m pthread)
    add_test(NAME deneb-lvgl-png-tests COMMAND deneb-lvgl-png-tests)

    add_executable(deneb-lvgl-compatibility-tests tests/test_lvgl_compatibility.c)
    target_include_directories(deneb-lvgl-compatibility-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_link_libraries(deneb-lvgl-compatibility-tests PRIVATE deneb-lvgl m pthread)
    add_test(NAME deneb-lvgl-compatibility-tests COMMAND deneb-lvgl-compatibility-tests)

    add_executable(deneb-lvgl-border-tests tests/test_border_allocation.c)
    target_include_directories(deneb-lvgl-border-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_link_libraries(deneb-lvgl-border-tests PRIVATE deneb-lvgl m pthread)
    target_link_options(deneb-lvgl-border-tests PRIVATE -Wl,--wrap=lv_malloc)
    add_test(NAME deneb-lvgl-border-tests COMMAND deneb-lvgl-border-tests)
    set_tests_properties(deneb-lvgl-border-tests PROPERTIES TIMEOUT 5)

    add_executable(deneb-framebuffer-tests tests/test_framebuffer.c src/drivers/fb_driver.c)
    target_include_directories(deneb-framebuffer-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_link_libraries(deneb-framebuffer-tests PRIVATE deneb-lvgl m pthread)
    target_link_options(deneb-framebuffer-tests PRIVATE
        -Wl,--wrap=open -Wl,--wrap=open64 -Wl,--wrap=ioctl
        -Wl,--wrap=mmap -Wl,--wrap=mmap64 -Wl,--wrap=msync -Wl,--wrap=munmap -Wl,--wrap=close)
    add_test(NAME deneb-framebuffer-tests COMMAND deneb-framebuffer-tests)

    add_executable(deneb-status-screen-tests
        tests/test_status_screen.c
        src/app_fonts.c
        src/screen_mgr.c
        src/assets/deneb_logo_116.c
        src/assets/ic_nozzle_16.c
        src/assets/ic_bed_16.c
        src/fonts/deneb_font_i18n_12.c
        src/fonts/deneb_font_i18n_14.c
        src/fonts/deneb_font_i18n_16.c
        ../common/print/print_state_rules.c
        ../common/print/print_string.c)
    target_include_directories(deneb-status-screen-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_compile_definitions(deneb-status-screen-tests PRIVATE
        DENEB_ACTIVE_THUMB_PATH="${CMAKE_CURRENT_BINARY_DIR}/status-test-thumbnail.rgb565")
    target_link_libraries(deneb-status-screen-tests PRIVATE deneb-lvgl m pthread)
    add_test(NAME deneb-status-screen-tests COMMAND deneb-status-screen-tests)

    add_executable(deneb-screen-presentation-tests
        tests/test_screen_presentation.c
        src/locale.c
        src/screen_mgr.c
        src/screens/screen_language.c
        src/screens/screen_material.c
        src/screens/screen_print.c
        ../common/print/gcode_command.c
        ../common/print/material_workflow.c
        ../common/print/print_job_file.c
        ../common/print/print_state_rules.c
        ../common/print/print_string.c
        ../common/print/status_state.c
        ../common/print/system_language.c)
    target_include_directories(deneb-screen-presentation-tests PRIVATE ${DENEB_UI_INCLUDE_DIRS})
    target_compile_definitions(deneb-screen-presentation-tests PRIVATE LOCALE_DIR=".")
    target_compile_options(deneb-screen-presentation-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(deneb-screen-presentation-tests PRIVATE -Wl,--gc-sections -Wl,--wrap=system)
    target_link_libraries(deneb-screen-presentation-tests PRIVATE deneb-lvgl m pthread)
    add_test(NAME deneb-screen-presentation-tests
        COMMAND deneb-screen-presentation-tests "${CMAKE_CURRENT_SOURCE_DIR}/locales")
    set_tests_properties(deneb-screen-presentation-tests PROPERTIES TIMEOUT 10)
endif()

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
