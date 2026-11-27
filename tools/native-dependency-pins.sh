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

MBEDTLS_VERSION=2.28.8
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$MBEDTLS_VERSION.tar.gz"
MBEDTLS_SHA256=4fef7de0d8d542510d726d643350acb3cdb9dc76ad45611b59c9aa08372b4213

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
            printf '%s\n' "$MBEDTLS_VERSION" "$MBEDTLS_URL" "$MBEDTLS_SHA256"
            sha256sum lib/libmbedtls.a lib/libmbedcrypto.a lib/libmbedx509.a include/mbedtls/*.h
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

native_dependency_verified() (
    [ -f "$2/deneb-source.receipt" ] || exit 1
    expected=$(native_dependency_fingerprint "$1" "$2" 2>/dev/null) || exit 1
    recorded=$(cat "$2/deneb-source.receipt") || exit 1
    [ "$expected" = "$recorded" ]
)
