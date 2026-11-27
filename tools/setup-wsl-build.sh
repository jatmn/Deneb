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
    apt-get install --no-install-recommends build-essential bzip2 ca-certificates cmake curl file git make pkg-config python3 tar xz-utils zstd
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

sh "$(dirname "$0")/setup-mbedtls.sh" "$build_root/mbedtls-$MBEDTLS_VERSION-mipsel" \
    -DCMAKE_TOOLCHAIN_FILE="$repo/ui/cmake/mipsel-musl-toolchain.cmake" \
    -DMUSL_CROSS="$toolchain_root"

"$toolchain" --version
echo "Deneb Debian/Linux build prerequisites are ready."
