# SPDX-License-Identifier: MPL-2.0
# Pinned source archives and receipts for the release builds.

ZMQ_VERSION=4.3.5
ZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v$ZMQ_VERSION/zeromq-$ZMQ_VERSION.tar.gz"
ZMQ_SHA256=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43

MBEDTLS_VERSION=2.28.8
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$MBEDTLS_VERSION.tar.gz"
MBEDTLS_SHA256=4fef7de0d8d542510d726d643350acb3cdb9dc76ad45611b59c9aa08372b4213

LIGHTTPD_VERSION=1.4.76
LIGHTTPD_URL="https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$LIGHTTPD_VERSION.tar.xz"
LIGHTTPD_SHA256=8cbf4296e373cfd0cedfe9d978760b5b05c58fdc4048b4e2bcaf0a61ac8f5011

# Store receipts only after checking the source archive and building its outputs.
# Relative paths let an unchanged dependency tree move between build roots.
native_dependency_fingerprint() (
    cd -- "$2" || exit 1
    export LC_ALL=C
    case "$1" in
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
