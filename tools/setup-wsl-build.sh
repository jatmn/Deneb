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
    apt-get install --no-install-recommends build-essential bzip2 ca-certificates cmake curl file git make pkg-config python3 tar xz-utils
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

sh "$(dirname "$0")/setup-mbedtls.sh" "$build_root/mbedtls-$MBEDTLS_VERSION-mipsel" \
    -DCMAKE_TOOLCHAIN_FILE="$repo/ui/cmake/mipsel-musl-toolchain.cmake" \
    -DMUSL_CROSS="$toolchain_root"

"$toolchain_root/bin/mipsel-linux-musl-gcc" --version
echo "Deneb Debian/Linux build prerequisites are ready."
