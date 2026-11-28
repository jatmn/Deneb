#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/native-dependency-pins.sh"

prefix=${1:?usage: $0 INSTALL_PREFIX [CMAKE_OPTIONS ...]}
shift
source_dir="$prefix-source"
build_dir="$prefix-build"

if native_dependency_verified mbedtls "$prefix"; then
    exit 0
fi
# Replace this script's own earlier install after a pin change, but only with
# positive proof: the receipt names the official Mbed TLS archive for its own
# recorded version, and every file hash it lists still matches. Any other
# unverified path, such as another dependency's prefix or a damaged install,
# stays a hard stop.
stale_mbedtls_install() (
    receipt="$prefix/deneb-source.receipt"
    [ -f "$receipt" ] || exit 1
    recorded=$(sed -n 1p "$receipt")
    url=$(sed -n 2p "$receipt")
    [ -n "$recorded" ] && [ "$recorded" != "$MBEDTLS_VERSION" ] || exit 1
    case "$recorded" in *[!0-9.]*) exit 1 ;; esac
    [ "$url" = "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$recorded/mbedtls-$recorded.tar.bz2" ] || exit 1
    hashes=$(sed -n '5,$p' "$receipt")
    [ -n "$hashes" ] || exit 1
    cd -- "$prefix" && printf '%s\n' "$hashes" | sha256sum -c --quiet --strict - >/dev/null 2>&1
)
if stale_mbedtls_install; then
    printf 'Replacing Mbed TLS %s at %s with pinned %s\n' \
        "$(sed -n 1p "$prefix/deneb-source.receipt")" "$prefix" "$MBEDTLS_VERSION" >&2
    rm -rf -- "$prefix" "$source_dir" "$build_dir"
fi
for path in "$source_dir" "$build_dir" "$prefix"; do
    if [ -e "$path" ]; then
        printf 'Refusing to overwrite incomplete or unverified Mbed TLS path: %s\n' "$path" >&2
        exit 1
    fi
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
archive="$work/mbedtls.tar.bz2"
curl --fail --location --output "$archive" "$MBEDTLS_URL"
printf '%s  %s\n' "$MBEDTLS_SHA256" "$archive" | sha256sum -c -
mkdir -p "$source_dir"
tar xf "$archive" -C "$source_dir" --strip-components=1
cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_LIBDIR=lib \
    -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
    -DUSE_SHARED_TF_PSA_CRYPTO_LIBRARY=OFF -DUSE_STATIC_TF_PSA_CRYPTO_LIBRARY=ON "$@"
cmake --build "$build_dir" --parallel
cmake --install "$build_dir"
record_native_dependency mbedtls "$prefix"
