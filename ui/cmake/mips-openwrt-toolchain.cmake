# SPDX-License-Identifier: MPL-2.0
#
# CMake toolchain file for cross-compiling to MT7688 MIPS (OpenWrt).
#
# Usage:
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mips-openwrt-toolchain.cmake
#
# Prerequisites:
#   OpenWrt SDK or musl-cross toolchain installed.
#   Adjust OPENWRT_STAGING_DIR below to your setup.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)

# OpenWrt toolchain paths - adjust to your SDK location
# Typical: openwrt/staging_dir/toolchain-mips_24kc_gcc-*-musl
set(OPENWRT_STAGING_DIR "$ENV{HOME}/openwrt-sdk/staging_dir/toolchain-mips_24kc_gcc-12.3.0_musl"
    CACHE PATH "OpenWrt staging directory")

set(CMAKE_C_COMPILER "${OPENWRT_STAGING_DIR}/bin/mips-openwrt-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${OPENWRT_STAGING_DIR}/bin/mips-openwrt-linux-musl-g++")

# Sysroot for headers and libraries
set(CMAKE_SYSROOT "${OPENWRT_STAGING_DIR}")
set(CMAKE_FIND_ROOT_PATH "${OPENWRT_STAGING_DIR}")

# Search behavior: only search staging dir for libraries/headers,
# but use host for programs (cmake, make, etc.)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# MT7688 tuning
set(CMAKE_C_FLAGS_INIT "-march=mips24kc -mtune=mips24kc -msoft-float")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -Wl,-z,relro")

# Cross-compilation markers
set(CROSS_COMPILING TRUE)
