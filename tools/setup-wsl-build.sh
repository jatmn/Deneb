#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/native-dependency-pins.sh"

TOOLCHAIN_URL=https://musl.cc/mipsel-linux-musl-cross.tgz
TOOLCHAIN_SHA256=82626533bf7e677c225e7cbedf1d5b0d6bc60c3daaf28249e54f0eb805d89b13

if [ "${DENEB_SKIP_SYSTEM_PACKAGES:-0}" != 1 ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run this WSL setup as root." >&2
    exit 1
fi

repo=${1:-}
if [ -z "$repo" ] || [ ! -f "$repo/ui/cmake/mipsel-musl-toolchain.cmake" ]; then
    echo "usage: $0 /absolute/path/to/Deneb" >&2
    exit 1
fi

toolchain_root=${DENEB_TOOLCHAIN_ROOT:-/root/mipsel-linux-musl-cross}
build_root=${DENEB_BUILD_ROOT:-/root/deneb-build}

if [ "${DENEB_SKIP_SYSTEM_PACKAGES:-0}" != 1 ]; then
    apt-get update
    apt-get install --no-install-recommends build-essential ca-certificates cmake curl file git make pkg-config python3 tar xz-utils
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ ! -x "$toolchain_root/bin/mipsel-linux-musl-gcc" ]; then
    if [ -e "$toolchain_root" ]; then
        echo "Refusing to overwrite incomplete $toolchain_root" >&2
        exit 1
    fi
    curl --fail --location --output "$work/toolchain.tgz" "$TOOLCHAIN_URL"
    printf '%s  %s\n' "$TOOLCHAIN_SHA256" "$work/toolchain.tgz" | sha256sum -c -
    tar xzf "$work/toolchain.tgz" -C "$work"
    mkdir -p "$(dirname "$toolchain_root")"
    mv "$work/mipsel-linux-musl-cross" "$toolchain_root"
fi

mbedtls_source="$build_root/mbedtls-$MBEDTLS_VERSION"
mbedtls_build="$mbedtls_source-build"
prefix="$mbedtls_source-mipsel"
if [ ! -f "$prefix/lib/libmbedtls.a" ]; then
    for path in "$mbedtls_source" "$mbedtls_build" "$prefix"; do
        if [ -e "$path" ]; then
            echo "Refusing to overwrite incomplete dependency path: $path" >&2
            exit 1
        fi
    done
    mkdir -p "$build_root"
    curl --fail --location --output "$work/mbedtls.tar.gz" "$MBEDTLS_URL"
    printf '%s  %s\n' "$MBEDTLS_SHA256" "$work/mbedtls.tar.gz" | sha256sum -c -
    tar xzf "$work/mbedtls.tar.gz" -C "$build_root"
    cmake -S "$mbedtls_source" -B "$mbedtls_build" \
      -DCMAKE_TOOLCHAIN_FILE="$repo/ui/cmake/mipsel-musl-toolchain.cmake" \
      -DMUSL_CROSS="$toolchain_root" \
      -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX="$prefix" \
      -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
      -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON
    cmake --build "$mbedtls_build" --parallel
    cmake --install "$mbedtls_build"
    record_native_dependency mbedtls "$prefix"
fi

"$toolchain_root/bin/mipsel-linux-musl-gcc" --version
test -f "$prefix/lib/libmbedtls.a"
test -f "$prefix/lib/libmbedx509.a"
test -f "$prefix/lib/libmbedcrypto.a"
echo "Deneb Debian/Linux build prerequisites are ready."
