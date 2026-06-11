#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Host-side Valgrind leak/error check for the native print service core.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$ROOT_DIR/printsvc/build-valgrind"
KEEP_BUILD=0

usage() {
    cat <<'USAGE'
Usage: deneb-printsvc-valgrind.sh [--build-dir DIR] [--keep-build]

Builds the host-stub native print service tests and runs deneb-printsvc-tests
under Valgrind Memcheck. This is intended for workstation/WSL leak triage, not
for the low-resource MIPS printer target.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-dir)
            [ "$#" -ge 2 ] || {
                echo "missing argument for --build-dir" >&2
                exit 2
            }
            BUILD_DIR=$2
            shift 2
            ;;
        --keep-build)
            KEEP_BUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required tool: $1" >&2
        exit 127
    fi
}

need_tool cmake
need_tool valgrind

if [ "$KEEP_BUILD" -eq 0 ]; then
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR/printsvc" -B "$BUILD_DIR" \
    -DBUILD_HOST_STUB=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-g -O0 -fno-omit-frame-pointer"
cmake --build "$BUILD_DIR" -j2

cd "$BUILD_DIR"
exec valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --error-exitcode=99 \
    ./deneb-printsvc-tests
