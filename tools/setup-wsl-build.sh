#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

TOOLCHAIN_URL=https://musl.cc/mipsel-linux-musl-cross.tgz
TOOLCHAIN_SHA256=82626533bf7e677c225e7cbedf1d5b0d6bc60c3daaf28249e54f0eb805d89b13
MBEDTLS_URL=https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.8.tar.gz
MBEDTLS_SHA256=4fef7de0d8d542510d726d643350acb3cdb9dc76ad45611b59c9aa08372b4213

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this setup inside WSL as root (the PowerShell wrapper uses -u root)." >&2
    exit 1
fi

repo=${1:-}
if [ -z "$repo" ] || [ ! -f "$repo/ui/cmake/mipsel-musl-toolchain.cmake" ]; then
    echo "usage: $0 /absolute/wsl/path/to/Deneb" >&2
    exit 1
fi

apt-get update
apt-get install --no-install-recommends build-essential ca-certificates cmake curl file git make pkg-config tar xz-utils

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ ! -x /root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc ]; then
    if [ -e /root/mipsel-linux-musl-cross ]; then
        echo "Refusing to overwrite incomplete /root/mipsel-linux-musl-cross" >&2
        exit 1
    fi
    curl --fail --location --output "$work/toolchain.tgz" "$TOOLCHAIN_URL"
    printf '%s  %s\n' "$TOOLCHAIN_SHA256" "$work/toolchain.tgz" | sha256sum -c -
    tar xzf "$work/toolchain.tgz" -C "$work"
    mv "$work/mipsel-linux-musl-cross" /root/mipsel-linux-musl-cross
fi

prefix=/root/deneb-build/mbedtls-2.28.8-mipsel
if [ ! -f "$prefix/lib/libmbedtls.a" ]; then
    for path in /root/deneb-build/mbedtls-2.28.8 /root/deneb-build/mbedtls-2.28.8-build "$prefix"; do
        if [ -e "$path" ]; then
            echo "Refusing to overwrite incomplete dependency path: $path" >&2
            exit 1
        fi
    done
    mkdir -p /root/deneb-build
    curl --fail --location --output "$work/mbedtls.tar.gz" "$MBEDTLS_URL"
    printf '%s  %s\n' "$MBEDTLS_SHA256" "$work/mbedtls.tar.gz" | sha256sum -c -
    tar xzf "$work/mbedtls.tar.gz" -C /root/deneb-build
    cmake -S /root/deneb-build/mbedtls-2.28.8 -B /root/deneb-build/mbedtls-2.28.8-build \
      -DCMAKE_TOOLCHAIN_FILE="$repo/ui/cmake/mipsel-musl-toolchain.cmake" \
      -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX="$prefix" \
      -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
      -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON
    cmake --build /root/deneb-build/mbedtls-2.28.8-build --parallel
    cmake --install /root/deneb-build/mbedtls-2.28.8-build
fi

/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-gcc --version
test -f "$prefix/lib/libmbedtls.a"
test -f "$prefix/lib/libmbedx509.a"
test -f "$prefix/lib/libmbedcrypto.a"
echo "Deneb WSL build prerequisites are ready."
