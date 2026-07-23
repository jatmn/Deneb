#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

# Build and audit a MIPS update package from a native Debian/Linux checkout.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${DENEB_BUILD_ROOT:-$repo_root/build/deneb-cross}
build_directory=build-musl
web_build_directory=build-musl
zmq_root="$build_root/zeromq-4.3.5"
lighttpd_root="$build_root/lighttpd-1.4.76"
release_channel=experimental
printsvc_stock_summary=
printsvc_native_summary=
declare -a printsvc_native_evidence_summaries=()
rebuild_zmq=false
rebuild_lighttpd=false

usage() {
    cat <<'EOF'
usage: tools/build-update-release.sh [options]

Build and audit a Deneb MIPS update package on Debian/Linux.

Options:
  --build-directory DIR              Component build directory (default: build-musl)
  --web-build-directory DIR          Web build directory (default: build-musl)
  --zmq-root DIR                     ZeroMQ source/build directory
  --lighttpd-root DIR                lighttpd source/build directory
  --release-channel CHANNEL          experimental, nightly, or stable
  --printsvc-stock-summary FILE      Required for nightly/stable builds
  --printsvc-native-summary FILE     Required for nightly/stable builds
  --printsvc-native-evidence-summary FILE
                                      May be supplied more than once
  --rebuild-zmq                      Download and build pinned ZeroMQ
  --rebuild-lighttpd                 Download pinned lighttpd source
  -h, --help                         Show this help
EOF
}

die() { echo "$*" >&2; exit 1; }

while [ "$#" -gt 0 ]; do
    case "$1" in
        --build-directory) build_directory=${2:?missing value}; shift 2 ;;
        --web-build-directory) web_build_directory=${2:?missing value}; shift 2 ;;
        --zmq-root) zmq_root=${2:?missing value}; shift 2 ;;
        --lighttpd-root) lighttpd_root=${2:?missing value}; shift 2 ;;
        --release-channel) release_channel=${2:?missing value}; shift 2 ;;
        --printsvc-stock-summary) printsvc_stock_summary=$(realpath "${2:?missing value}"); shift 2 ;;
        --printsvc-native-summary) printsvc_native_summary=$(realpath "${2:?missing value}"); shift 2 ;;
        --printsvc-native-evidence-summary) printsvc_native_evidence_summaries+=("$(realpath "${2:?missing value}")"); shift 2 ;;
        --rebuild-zmq) rebuild_zmq=true; shift ;;
        --rebuild-lighttpd) rebuild_lighttpd=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
done

case "$release_channel" in experimental|nightly|stable) ;; *) die "Invalid release channel: $release_channel" ;; esac
if [ "$release_channel" != experimental ] && { [ -z "$printsvc_stock_summary" ] || [ -z "$printsvc_native_summary" ]; }; then
    die "Non-experimental release builds require --printsvc-stock-summary and --printsvc-native-summary."
fi

toolchain_root=${DENEB_TOOLCHAIN_ROOT:-$build_root/mipsel-linux-musl-cross}
toolchain="$toolchain_root/bin/mipsel-linux-musl-gcc"
zmq_lib="$zmq_root/build-musl/lib/libzmq.a"
zmq_include="$zmq_root/include"
lighttpd_version=1.4.76
lighttpd_binary="$lighttpd_root/build-musl-static/build/lighttpd"
zmq_sha256=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43
lighttpd_sha256=8cbf4296e373cfd0cedfe9d978760b5b05c58fdc4048b4e2bcaf0a61ac8f5011

[ -x "$toolchain" ] || die "Missing musl cross-compiler. Run bash tools/setup-linux-build.sh first."

if "$rebuild_zmq"; then
    [[ "$zmq_root" =~ /zeromq-4\.3\.5/?$ ]] || die "Refusing to rebuild libzmq outside a zeromq-4.3.5 leaf directory: $zmq_root"
    mkdir -p "$zmq_root"
    rm -rf "$zmq_root"/*
    curl --fail --location -o "$zmq_root.tar.gz" https://github.com/zeromq/libzmq/releases/download/v4.3.5/zeromq-4.3.5.tar.gz
    printf '%s  %s\n' "$zmq_sha256" "$zmq_root.tar.gz" | sha256sum -c -
    tar xzf "$zmq_root.tar.gz" -C "$zmq_root" --strip-components=1
    rm -f "$zmq_root.tar.gz"
    cmake -S "$zmq_root" -B "$zmq_root/build-musl" -DCMAKE_TOOLCHAIN_FILE="$repo_root/ui/cmake/mipsel-musl-toolchain.cmake" -DMUSL_CROSS="$toolchain_root" -DCMAKE_BUILD_TYPE=MinSizeRel -DWITH_LIBSODIUM=OFF -DZMQ_BUILD_TESTS=OFF -DWITH_DOCS=OFF -DBUILD_SHARED=OFF -DBUILD_STATIC=ON
    cmake --build "$zmq_root/build-musl" --parallel
fi
[ -f "$zmq_lib" ] && [ -d "$zmq_include" ] || die "Missing musl libzmq at $zmq_lib. Re-run with --rebuild-zmq."

if "$rebuild_lighttpd"; then
    [[ "$lighttpd_root" =~ /lighttpd-1\.4\.76/?$ ]] || die "Refusing to rebuild lighttpd outside a lighttpd-1.4.76 leaf directory: $lighttpd_root"
    mkdir -p "$lighttpd_root"
    rm -rf "$lighttpd_root"/*
    curl --fail --location -o "$lighttpd_root.tar.xz" "https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$lighttpd_version.tar.xz"
    printf '%s  %s\n' "$lighttpd_sha256" "$lighttpd_root.tar.xz" | sha256sum -c -
    tar xf "$lighttpd_root.tar.xz" -C "$lighttpd_root" --strip-components=1
    rm -f "$lighttpd_root.tar.xz"
fi

configure_component() {
    local source=$1 build=$2 toolchain_file=$3
    shift 3
    if [ -f "$build/CMakeCache.txt" ]; then
        grep -Fq "CMAKE_C_COMPILER:FILEPATH=$toolchain" "$build/CMakeCache.txt" || die "Existing build directory is not configured for the pinned mipsel musl compiler: $build"
        cmake -S "$source" -B "$build" -DMUSL_CROSS="$toolchain_root" -DCMAKE_BUILD_TYPE=MinSizeRel "$@" -DZMQ_INCLUDE_DIR="$zmq_include"
    else
        cmake -S "$source" -B "$build" -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" -DMUSL_CROSS="$toolchain_root" -DCMAKE_BUILD_TYPE=MinSizeRel "$@" -DZMQ_INCLUDE_DIR="$zmq_include"
    fi
    cmake --build "$build" --parallel
}

configure_component "$repo_root/web" "$repo_root/web/$web_build_directory" "$repo_root/web/cmake/mipsel-musl-toolchain.cmake" "-DZMQ_STATIC_PATH=$zmq_lib"
configure_component "$repo_root/printsvc" "$repo_root/printsvc/$build_directory" "$repo_root/ui/cmake/mipsel-musl-toolchain.cmake" "-DZMQ_STATIC_PATH=$zmq_lib"
test -x "$repo_root/printsvc/$build_directory/deneb-printsvc"

[ -d "$lighttpd_root/src" ] || die "Missing lighttpd source at $lighttpd_root; re-run with --rebuild-lighttpd."
printf '%s\n' 'PLUGIN_INIT(mod_access)' 'PLUGIN_INIT(mod_alias)' 'PLUGIN_INIT(mod_indexfile)' 'PLUGIN_INIT(mod_staticfile)' 'PLUGIN_INIT(mod_setenv)' 'PLUGIN_INIT(mod_proxy)' > "$lighttpd_root/src/plugin-static.h"
if [ -f "$lighttpd_root/build-musl-static/CMakeCache.txt" ]; then
    grep -Fq "CMAKE_C_COMPILER:FILEPATH=$toolchain" "$lighttpd_root/build-musl-static/CMakeCache.txt" || die "Existing lighttpd build directory is not configured for the pinned mipsel musl compiler. Rebuild it with --rebuild-lighttpd."
else
    cmake -S "$lighttpd_root" -B "$lighttpd_root/build-musl-static" -DCMAKE_TOOLCHAIN_FILE="$repo_root/ui/cmake/mipsel-musl-toolchain.cmake" -DMUSL_CROSS="$toolchain_root" -DCMAKE_BUILD_TYPE=MinSizeRel -DBUILD_STATIC=ON -DWITH_PCRE=OFF -DWITH_PCRE2=OFF -DWITH_ZLIB=OFF -DWITH_BZIP=OFF -DWITH_BROTLI=OFF -DWITH_ZSTD=OFF -DWITH_OPENSSL=OFF -DWITH_MBEDTLS=OFF -DWITH_WOLFSSL=OFF -DWITH_GNUTLS=OFF
fi
cmake --build "$lighttpd_root/build-musl-static" --target lighttpd --parallel
test -x "$lighttpd_binary"

configure_component "$repo_root/dfsvc" "$repo_root/dfsvc/$build_directory" "$repo_root/ui/cmake/mipsel-musl-toolchain.cmake" "-DZMQ_STATIC_PATH=$zmq_lib" "-DMBEDTLS_ROOT=$build_root/mbedtls-2.28.8-mipsel"
configure_component "$repo_root/ui" "$repo_root/ui/$build_directory" "$repo_root/ui/cmake/mipsel-musl-toolchain.cmake" "-DZMQ_LIBRARY=$zmq_lib"

package_env=("DENEB_RELEASE_CHANNEL=$release_channel" "STRIP=$toolchain_root/bin/mipsel-linux-musl-strip")
[ -z "$printsvc_stock_summary" ] || package_env+=("DENEB_PRINTSVC_STOCK_SUMMARY=$printsvc_stock_summary")
[ -z "$printsvc_native_summary" ] || package_env+=("DENEB_PRINTSVC_NATIVE_SUMMARY=$printsvc_native_summary")
[ "${#printsvc_native_evidence_summaries[@]}" -eq 0 ] || package_env+=("DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES=${printsvc_native_evidence_summaries[*]}")
(cd "$repo_root" && env "${package_env[@]}" bash ui/build-package.sh "ui/$build_directory/deneb-ui" "web/$web_build_directory/deneb-api" "$lighttpd_binary" "web/$web_build_directory/deneb-mdns" "printsvc/$build_directory/deneb-printsvc" "dfsvc/$build_directory/deneb-dfsvc")

package_version=${DENEB_PACKAGE_VERSION_OVERRIDE:-$(git -C "$repo_root" describe --tags --always 2>/dev/null || echo dev)}
package="$repo_root/dist/Deneb_Update_$package_version.deneb"
test -s "$package" || die "No Deneb update package found: $package"
package_files=$(mktemp)
cleanup() { rm -f "$package_files"; rm -rf /tmp/deneb-release-smoke-selftest; }
trap cleanup EXIT
tar tf "$package" > "$package_files"
for required in update.sh deneb-printsvc deneb-dfsvc deneb-printsvc.init digitalfactory.init deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-stability deneb-active-physical-soak-runner deneb-printsvc-stock-baseline deneb-printsvc-cli-selftest deneb-printsvc-init-selftest deneb-printsvc-release-gate-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-integration-audit deneb-printsvc-integration-audit-selftest deneb-runtime-inventory; do
    grep -Eq "(^|/)$required$" "$package_files" || die "Package is missing required file: $required"
done
! grep -Eq '(^|/)deneb-df-bridge$' "$package_files" || die "Package contains obsolete deneb-df-bridge"
! grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' "$package_files" || die "Python driver artifact found in release package"
tar -xOf "$package" manifest.txt | grep -Eq "^channel: $release_channel$"
tar -xOf "$package" manifest.txt | grep -Eq '^native_printsvc: experimental$'
tar -xOf "$package" manifest.txt | grep -Fxq 'native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction'
mkdir -p /tmp/deneb-release-smoke-selftest
tar xf "$package" -C /tmp/deneb-release-smoke-selftest
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-smoke-selftest
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-stability --selftest
DENEB_REPO_ROOT="$repo_root" sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-release-gate-selftest
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit --archive "$package"
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-native-audit-selftest
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-integration-audit --archive "$package"
sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-integration-audit-selftest
DENEB_REPO_ROOT=/tmp/deneb-release-smoke-selftest DENEB_PRINTSVC_INIT=/tmp/deneb-release-smoke-selftest/deneb-printsvc.init DENEB_INSTALLER=/tmp/deneb-release-smoke-selftest/update.sh sh /tmp/deneb-release-smoke-selftest/deneb-printsvc-init-selftest
printf 'Verified native-only print service package: %s\n' "$package"
