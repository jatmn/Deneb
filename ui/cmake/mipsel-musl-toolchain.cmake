# SPDX-License-Identifier: MPL-2.0
#
# CMake toolchain for MIPS musl cross-compilation
# Target: MT7688 MIPS 24KEc (OpenWrt / Onion Omega2+)
# Uses musl libc for smaller static binaries
#
# Usage from WSL:
#   cd /mnt/c/temp/Deneb/ui
#   mkdir build-musl && cd build-musl
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-musl-toolchain.cmake \
#            -DMUSL_CROSS=/path/to/toolchain-mipsel_24kc_gcc-14.3.0_musl \
#            -DCMAKE_BUILD_TYPE=MinSizeRel
#   make -j$(nproc)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

# Release wrappers pass the pinned OpenWrt toolchain root.
set(MUSL_CROSS "$ENV{MUSL_CROSS}" CACHE PATH "OpenWrt musl cross-compiler path")
if(NOT MUSL_CROSS)
    message(FATAL_ERROR "Set MUSL_CROSS to the OpenWrt MT76x8 toolchain root")
endif()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MUSL_CROSS)

set(CMAKE_C_COMPILER "${MUSL_CROSS}/bin/mipsel-openwrt-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${MUSL_CROSS}/bin/mipsel-openwrt-linux-musl-g++")
set(CMAKE_AR "${MUSL_CROSS}/bin/mipsel-openwrt-linux-musl-ar")
set(CMAKE_RANLIB "${MUSL_CROSS}/bin/mipsel-openwrt-linux-musl-ranlib")
set(CMAKE_STRIP "${MUSL_CROSS}/bin/mipsel-openwrt-linux-musl-strip")

# MT7688: MIPS 24KEc, little-endian, soft-float
set(CMAKE_C_FLAGS_INIT "-march=24kec -mtune=24kec -msoft-float -fno-pie")
set(CMAKE_CXX_FLAGS_INIT "-march=24kec -mtune=24kec -msoft-float -fno-pie")
set(CMAKE_C_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -static -no-pie")
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

# OpenWrt's static GCC specs omit the unwinder needed by the static C++ runtime.
set(CMAKE_C_STANDARD_LIBRARIES_INIT "-lgcc_eh")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-lgcc_eh")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
