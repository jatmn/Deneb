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
#            -DCMAKE_BUILD_TYPE=MinSizeRel
#   make -j$(nproc)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

# musl cross-compiler from musl.cc
set(MUSL_CROSS "$ENV{HOME}/mipsel-linux-musl-cross" CACHE PATH "musl cross-compiler path")

set(CMAKE_C_COMPILER "${MUSL_CROSS}/bin/mipsel-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${MUSL_CROSS}/bin/mipsel-linux-musl-g++")
set(CMAKE_AR "${MUSL_CROSS}/bin/mipsel-linux-musl-ar")
set(CMAKE_RANLIB "${MUSL_CROSS}/bin/mipsel-linux-musl-ranlib")
set(CMAKE_STRIP "${MUSL_CROSS}/bin/mipsel-linux-musl-strip")

# MT7688: MIPS 24KEc, little-endian, soft-float (musl default)
set(CMAKE_C_FLAGS_INIT "-march=24kec -mtune=24kec")
set(CMAKE_C_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
