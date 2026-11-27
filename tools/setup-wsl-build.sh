#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/native-dependency-pins.sh"

if [ "${DENEB_SKIP_SYSTEM_PACKAGES:-0}" != 1 ] && [ "$(id -u)" -ne 0 ]; then
    echo "Run this WSL setup as root." >&2
    exit 1
fi

repo=${1:-}
if [ -z "$repo" ] || [ ! -f "$repo/ui/cmake/mipsel-musl-toolchain.cmake" ]; then
    echo "usage: $0 /absolute/path/to/Deneb" >&2
    exit 1
fi

build_root=${DENEB_BUILD_ROOT:-/root/deneb-build/openwrt-$MIPS_TOOLCHAIN_RELEASE}
toolchain_root=${DENEB_TOOLCHAIN_ROOT:-$build_root/$MIPS_TOOLCHAIN_MEMBER}
toolchain="$toolchain_root/bin/$MIPS_TOOLCHAIN_TARGET-gcc"
export STAGING_DIR="$toolchain_root"

if [ "${DENEB_SKIP_SYSTEM_PACKAGES:-0}" != 1 ]; then
    apt-get update
    apt-get install --no-install-recommends build-essential ca-certificates cmake curl file git make pkg-config python3 tar xz-utils zstd
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

if [ ! -x "$toolchain" ]; then
    if [ -e "$toolchain_root" ]; then
        echo "Refusing to overwrite incomplete $toolchain_root" >&2
        exit 1
    fi
    curl --fail --location --output "$work/toolchain.tar.zst" "$MIPS_TOOLCHAIN_URL"
    printf '%s  %s\n' "$MIPS_TOOLCHAIN_SHA256" "$work/toolchain.tar.zst" | sha256sum -c -
    tar xf "$work/toolchain.tar.zst" -C "$work"
    extracted="$work/$MIPS_TOOLCHAIN_ARCHIVE/$MIPS_TOOLCHAIN_MEMBER"
    record_native_dependency mips-toolchain "$extracted"
    mkdir -p "$(dirname "$toolchain_root")"
    mv -T "$extracted" "$toolchain_root"
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

"$toolchain" --version
test -f "$prefix/lib/libmbedtls.a"
test -f "$prefix/lib/libmbedx509.a"
test -f "$prefix/lib/libmbedcrypto.a"
echo "Deneb Debian/Linux build prerequisites are ready."
