#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/native-dependency-pins.sh"

prefix=${1:?usage: $0 INSTALL_PREFIX [CMAKE_OPTIONS ...]}
shift
# Drop trailing slashes so build/ci-mbedtls/ names the same paths and siblings.
while [ "$prefix" != / ] && [ "${prefix%/}" != "$prefix" ]; do
    prefix=${prefix%/}
done
source_dir="$prefix-source"
build_dir="$prefix-build"

if native_dependency_verified mbedtls "$prefix"; then
    exit 0
fi
# Move this script's own earlier install aside after a pin change, but only
# with positive proof: the receipt names the official Mbed TLS archive for its
# own recorded version, and every file hash it lists still matches. Nothing is
# deleted, so a misidentified path is never lost. Any other unverified path,
# such as another dependency's prefix or a damaged install, stays a hard stop.
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
    cd -- "$prefix" || exit 1
    # The receipt must list exactly the files every Mbed TLS receipt covers, so
    # a truncated receipt cannot vouch for a partial install.
    for library in lib/libmbedtls.a lib/libmbedx509.a lib/libtfpsacrypto.a; do
        [ -f "$library" ] || exit 1
    done
    # An unmatched glob stays literal and so never matches the listed names.
    covered=$(printf '%s\n' lib/libmbedtls.a lib/libmbedx509.a lib/libtfpsacrypto.a \
        include/mbedtls/*.h include/psa/*.h include/tf-psa-crypto/*.h | LC_ALL=C sort)
    listed=$(printf '%s\n' "$hashes" | sed 's/^[0-9a-f]\{64\}  //' | LC_ALL=C sort)
    [ "$covered" = "$listed" ] || exit 1
    printf '%s\n' "$hashes" | sha256sum -c --quiet --strict - >/dev/null 2>&1
)
if stale_mbedtls_install; then
    previous=$(sed -n 1p "$prefix/deneb-source.receipt")
    # Where a rename fails with EXDEV, as on OverlayFS, mv copies and then
    # deletes, and a failed delete can leave the original partly removed.
    # Without --no-copy to forbid that, leave the earlier install untouched.
    if ! mv --help 2>/dev/null | grep -q -e '--no-copy'; then
        printf 'Cannot safely move Mbed TLS %s aside: this mv has no --no-copy option.\n' "$previous" >&2
        printf 'Move %s and any %s and %s aside or delete them, then run setup again.\n' \
            "$prefix" "$source_dir" "$build_dir" >&2
        exit 1
    fi
    for path in "$prefix" "$source_dir" "$build_dir"; do
        # Each move renames a real directory onto a claimed empty directory,
        # so a symlink, such as a prefix linked to the install, or any other
        # file cannot be moved. Stop before moving anything.
        if [ -L "$path" ] || { [ -e "$path" ] && ! [ -d "$path" ]; }; then
            printf 'Cannot move Mbed TLS %s aside: %s is not a directory.\n' "$previous" "$path" >&2
            printf 'Move %s and any %s and %s aside or delete them, then run setup again.\n' \
                "$prefix" "$source_dir" "$build_dir" >&2
            exit 1
        fi
        if [ -e "$path" ] &&
            { [ -e "$path.replaced-$previous" ] || [ -L "$path.replaced-$previous" ]; }; then
            printf 'Cannot move %s aside: %s already exists\n' "$path" "$path.replaced-$previous" >&2
            exit 1
        fi
    done
    # Rename directory $1 to $2 only if nothing else holds $2. mkdir claims $2
    # atomically on any filesystem, and a rename may then replace only that
    # empty directory: anything put inside it makes the rename fail. -T keeps
    # $1 from landing inside $2, and --no-copy leaves $1 in place when the
    # rename itself fails. Judge the result by the paths, not mv's status,
    # which can be an error after a rename that completed (as on NFS): the
    # move happened when $2 is now the very directory $1 was. Otherwise the
    # claim is released if still empty.
    move_noreplace() {
        identity=$(stat -c '%d:%i' -- "$1") || return 1
        mkdir -- "$2" || return 1
        mv -T --no-copy -- "$1" "$2" || :
        if [ "$(stat -c '%d:%i' -- "$2" 2>/dev/null)" = "$identity" ]; then
            return 0
        fi
        rmdir -- "$2" 2>/dev/null || :
        return 1
    }
    # Move each path aside; if one fails, put back the ones already moved so
    # the earlier install stays whole. Flags keep paths with spaces intact.
    # Print each path that could not be put back, and fail if there was one.
    restore_one() {
        if [ "$1" = 1 ] && ! move_noreplace "$2.replaced-$previous" "$2"; then
            printf 'Could not restore %s; it is still at %s.replaced-%s\n' "$2" "$2" "$previous" >&2
            return 1
        fi
    }
    restore_moved() {
        restored=0
        restore_one "${moved_build:-}" "$build_dir" || restored=1
        restore_one "${moved_source:-}" "$source_dir" || restored=1
        restore_one "${moved_prefix:-}" "$prefix" || restored=1
        return "$restored"
    }
    move_aside() {
        if [ -e "$1" ] || [ -L "$1" ]; then
            if ! move_noreplace "$1" "$1.replaced-$previous"; then
                if restore_moved; then
                    printf 'Could not move %s aside; left the earlier install in place\n' "$1" >&2
                else
                    printf 'Could not move %s aside, and the earlier install was only partly restored (see above)\n' "$1" >&2
                fi
                exit 1
            fi
            eval "moved_$2=1"
        fi
    }
    move_aside "$prefix" prefix
    move_aside "$source_dir" source
    move_aside "$build_dir" build
    printf 'Moved Mbed TLS %s aside to %s.replaced-%s; building pinned %s. Delete the old copy when no longer needed.\n' \
        "$previous" "$prefix" "$previous" "$MBEDTLS_VERSION" >&2
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
