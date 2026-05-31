# SPDX-License-Identifier: MPL-2.0
#
# CMake toolchain for MIPS cross-compilation (Debian gcc-mips-linux-gnu)
# Target: MT7688 MIPS 24KEc (OpenWrt / Onion Omega2+)
#
# Usage from WSL:
#   cd /mnt/c/temp/Deneb/ui
#   mkdir build-mips && cd build-mips
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/mips-linux-gnu-toolchain.cmake \
#            -DCMAKE_BUILD_TYPE=MinSizeRel
#   make -j$(nproc)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)

set(CMAKE_C_COMPILER mips-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER mips-linux-gnu-g++)
set(CMAKE_AR mips-linux-gnu-ar)
set(CMAKE_RANLIB mips-linux-gnu-ranlib)
set(CMAKE_STRIP mips-linux-gnu-strip)

# MT7688 tuning: MIPS 24KEc with hardware float support
set(CMAKE_C_FLAGS_INIT "-march=24kec -mtune=24kec -msoft-float -EL")
set(CMAKE_C_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -static")

# Static linking by default (no musl/glibc mismatch on target)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
