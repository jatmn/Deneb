# SPDX-License-Identifier: MPL-2.0
# Pinned source archives and receipts for the release builds.

MIPS_TOOLCHAIN_RELEASE=25.12.6
MIPS_TOOLCHAIN_REVISION=981c978eff636fc7d81f0ed17b00e2a6317c949a
MIPS_TOOLCHAIN_GCC_VERSION=14.3.0
MIPS_TOOLCHAIN_MUSL_VERSION=1.2.5
MIPS_TOOLCHAIN_TARGET=mipsel-openwrt-linux-musl
MIPS_TOOLCHAIN_ARCHIVE="openwrt-toolchain-$MIPS_TOOLCHAIN_RELEASE-ramips-mt76x8_gcc-${MIPS_TOOLCHAIN_GCC_VERSION}_musl.Linux-x86_64"
MIPS_TOOLCHAIN_MEMBER="toolchain-mipsel_24kc_gcc-${MIPS_TOOLCHAIN_GCC_VERSION}_musl"
MIPS_TOOLCHAIN_URL="https://downloads.openwrt.org/releases/$MIPS_TOOLCHAIN_RELEASE/targets/ramips/mt76x8/$MIPS_TOOLCHAIN_ARCHIVE.tar.zst"
MIPS_TOOLCHAIN_SHA256=628559363d3e41da6cc298ed0a27b81fbc3843a2758228c7697861defd324da3

MUSL_URL="https://musl.libc.org/releases/musl-$MIPS_TOOLCHAIN_MUSL_VERSION.tar.gz"
MUSL_SHA256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-$MIPS_TOOLCHAIN_GCC_VERSION/gcc-$MIPS_TOOLCHAIN_GCC_VERSION.tar.xz"
GCC_SHA256=e0dc77297625631ac8e50fa92fffefe899a4eb702592da5c32ef04e2293aca3a

ZMQ_VERSION=4.3.5
ZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v$ZMQ_VERSION/zeromq-$ZMQ_VERSION.tar.gz"
ZMQ_SHA256=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43

MBEDTLS_VERSION=4.3.0
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
MBEDTLS_SHA256=1c87cba094cb40f097ebe0eba440a74939b71dab4e1d0d566bc90749ff381d18
TF_PSA_CRYPTO_VERSION=1.3.0

LIGHTTPD_VERSION=1.4.85
LIGHTTPD_URL="https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$LIGHTTPD_VERSION.tar.xz"
LIGHTTPD_SHA256=18de51b393bac4a6827879e1a7ff377c169e414bae92cd245091d80fc2601d13

# Store receipts only after checking the source archive and building its outputs.
# Relative paths let an unchanged dependency tree move between build roots.
native_dependency_fingerprint() (
    cd -- "$2" || exit 1
    export LC_ALL=C
    case "$1" in
        mips-toolchain)
            printf '%s\n' "$MIPS_TOOLCHAIN_URL" "$MIPS_TOOLCHAIN_SHA256"
            sha256sum info.mk "bin/$MIPS_TOOLCHAIN_TARGET-gcc.bin" "bin/$MIPS_TOOLCHAIN_TARGET-g++.bin" \
                "bin/$MIPS_TOOLCHAIN_TARGET-wrapper.sh" \
                lib/libc.a lib/libstdc++.a lib/libssp_nonshared.a \
                "lib/gcc/$MIPS_TOOLCHAIN_TARGET/$MIPS_TOOLCHAIN_GCC_VERSION/libgcc.a" \
                "lib/gcc/$MIPS_TOOLCHAIN_TARGET/$MIPS_TOOLCHAIN_GCC_VERSION/libgcc_eh.a"
            ;;
        libzmq)
            printf '%s\n' "$ZMQ_VERSION" "$ZMQ_URL" "$ZMQ_SHA256"
            sha256sum build-musl/lib/libzmq.a include/zmq.h
            ;;
        lighttpd)
            printf '%s\n' "$LIGHTTPD_VERSION" "$LIGHTTPD_URL" "$LIGHTTPD_SHA256"
            sha256sum build-musl-static/build/lighttpd
            ;;
        mbedtls)
            printf '%s\n' "$MBEDTLS_VERSION" "$MBEDTLS_URL" "$MBEDTLS_SHA256" "$TF_PSA_CRYPTO_VERSION"
            sha256sum lib/libmbedtls.a lib/libmbedx509.a lib/libtfpsacrypto.a \
                include/mbedtls/*.h include/psa/*.h include/tf-psa-crypto/*.h
            ;;
        *) return 1 ;;
    esac
)

record_native_dependency() (
    receipt=$(mktemp "$2/deneb-source.XXXXXX") || exit 1
    trap 'rm -f -- "$receipt"' EXIT
    trap 'exit 1' HUP INT TERM
    native_dependency_fingerprint "$1" "$2" > "$receipt" &&
        mv -fT -- "$receipt" "$2/deneb-source.receipt"
)

verified_native_dependency_receipt() (
    [ -f "$2/deneb-source.receipt" ] || exit 1
    expected=$(native_dependency_fingerprint "$1" "$2" 2>/dev/null) || exit 1
    recorded=$(cat "$2/deneb-source.receipt") || exit 1
    [ "$expected" = "$recorded" ] || exit 1
    printf '%s\n' "$recorded"
)

native_dependency_verified() {
    verified_native_dependency_receipt "$@" >/dev/null
}

mips_toolchain_key() (
    receipt=$(verified_native_dependency_receipt mips-toolchain "$1") || exit 1
    checksum=$(printf '%s\n' "$receipt" | sha256sum) || exit 1
    printf '%s\n' "${checksum%% *}"
)

mips_runtime_fingerprint() (
    checksum=$(sha256sum < "$2") || exit 1
    printf '%s\n%s\n' "$1" "${checksum%% *}"
)

mips_runtime_output_verified() (
    [ -f "$2.deneb-runtime.receipt" ] && [ -f "$3" ] || exit 1
    expected=$(mips_runtime_fingerprint "$1" "$3") || exit 1
    recorded=$(cat "$2.deneb-runtime.receipt") || exit 1
    [ "$expected" = "$recorded" ]
)

# Check the selected C compiler for the CMake version that owns the active cache.
mips_uses_pinned_compiler() (
    mips_cache_dir=$1
    mips_expected_compiler=$2
    [ -f "$mips_cache_dir/CMakeCache.txt" ] || exit 1
    mips_version=$(awk -F= '
        /^CMAKE_CACHE_MAJOR_VERSION:INTERNAL=[0-9]+$/ { major = $2 }
        /^CMAKE_CACHE_MINOR_VERSION:INTERNAL=[0-9]+$/ { minor = $2 }
        /^CMAKE_CACHE_PATCH_VERSION:INTERNAL=[0-9]+$/ { patch = $2 }
        END { if (major != "" && minor != "" && patch != "") print major "." minor "." patch }
    ' "$mips_cache_dir/CMakeCache.txt")
    [ -n "$mips_version" ] || exit 1
    mips_active_version=$(cmake --version | sed -n '1s/^cmake version //p') || exit 1
    [ -n "$mips_active_version" ] || exit 1
    # A different CMake version can load a stale compiler record on configure.
    if [ "$mips_version" != "$mips_active_version" ]; then
        printf 'CMake version mismatch in %s: configured with %s, running %s. Use CMake %s to reuse it.\n' \
            "$mips_cache_dir" "$mips_version" "$mips_active_version" "$mips_version" >&2
        exit 1
    fi
    mips_record="$mips_cache_dir/CMakeFiles/$mips_version/CMakeCCompiler.cmake"
    [ -f "$mips_record" ] || exit 1
    mips_compiler=$(sed -n 's/^set(CMAKE_C_COMPILER "\(.*\)")$/\1/p' "$mips_record") || exit 1
    case "$mips_compiler" in
        /*) ;;
        *) exit 1 ;;
    esac
    # OpenWrt selects the driver from the invoked name and the sysroot from
    # the resolved wrapper path.
    [ "${mips_compiler##*-}" = "${mips_expected_compiler##*-}" ] || exit 1
    mips_compiler=$(readlink -e -- "$mips_compiler") || exit 1
    mips_expected_bin=$(readlink -e -- "${mips_expected_compiler%/*}") || exit 1
    [ "$mips_compiler" = "$mips_expected_bin/$MIPS_TOOLCHAIN_TARGET-wrapper.sh" ]
)

mips_release_build() (
    mips_toolchain_root=$1
    mips_build_dir=$2
    mips_target=$3
    shift 3

    # CMake need not track archives that the compiler links implicitly.
    # Discard only the final link outputs when their proof is missing or stale.
    mips_compiler="$mips_toolchain_root/bin/$MIPS_TOOLCHAIN_TARGET-gcc"
    mips_key=
    if mips_uses_pinned_compiler "$mips_build_dir" "$mips_compiler"; then
        mips_key=$(mips_toolchain_key "$mips_toolchain_root") || mips_key=
    fi
    for binary do
        mips_output="$mips_build_dir/$binary"
        if [ -z "$mips_key" ]; then
            rm -f -- "$mips_output.deneb-runtime.receipt" || exit 1
        elif ! mips_runtime_output_verified "$mips_key" "$mips_output" "$mips_output"; then
            rm -f -- "$mips_output" "$mips_output.deneb-runtime.receipt" || exit 1
        fi
    done

    if [ -n "$mips_target" ]; then
        cmake --build "$mips_build_dir" --target "$mips_target" --parallel "$(nproc)" || exit "$?"
    else
        cmake --build "$mips_build_dir" --parallel "$(nproc)" || exit "$?"
    fi

    [ -n "$mips_key" ] || exit 0
    if ! mips_uses_pinned_compiler "$mips_build_dir" "$mips_compiler" ||
        [ "$(mips_toolchain_key "$mips_toolchain_root")" != "$mips_key" ]; then
        for binary do
            rm -f -- "$mips_build_dir/$binary.deneb-runtime.receipt" || exit 1
        done
        exit 0
    fi
    for binary do
        mips_output="$mips_build_dir/$binary"
        (
            output_receipt=$(mktemp "$mips_output.deneb-runtime.XXXXXX") || exit 1
            trap 'rm -f -- "$output_receipt"' EXIT
            trap 'exit 1' HUP INT TERM
            mips_runtime_fingerprint "$mips_key" "$mips_output" > "$output_receipt" &&
                mv -fT -- "$output_receipt" "$mips_output.deneb-runtime.receipt"
        ) || exit 1
    done
)
