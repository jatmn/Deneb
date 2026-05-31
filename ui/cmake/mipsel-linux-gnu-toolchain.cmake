# SPDX-License-Identifier: MPL-2.0
#
# CMake toolchain for MIPS little-endian cross-compilation
# Target: MT7688 MIPS 24KEc (OpenWrt / Onion Omega2+) - little-endian
#
# Usage from WSL:
#   cd /mnt/c/temp/Deneb/ui
#   mkdir build-mips && cd build-mips
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mipsel-linux-gnu-toolchain.cmake \
#            -DCMAKE_BUILD_TYPE=MinSizeRel
#   make -j$(nproc)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

set(CMAKE_C_COMPILER mipsel-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER mipsel-linux-gnu-g++)
set(CMAKE_AR mipsel-linux-gnu-ar)
set(CMAKE_RANLIB mipsel-linux-gnu-ranlib)
set(CMAKE_STRIP mipsel-linux-gnu-strip)

# MT7688: MIPS 24KEc, little-endian, soft-float
set(CMAKE_C_FLAGS_INIT "-march=24kec -mtune=24kec")
set(CMAKE_C_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
