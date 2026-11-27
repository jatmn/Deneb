# SPDX-License-Identifier: MPL-2.0
# Pinned source archives and receipts for the release builds.

ZMQ_VERSION=4.3.5
ZMQ_URL="https://github.com/zeromq/libzmq/releases/download/v$ZMQ_VERSION/zeromq-$ZMQ_VERSION.tar.gz"
ZMQ_SHA256=6653ef5910f17954861fe72332e68b03ca6e4d9c7160eb3a8de5a5a913bfab43

MBEDTLS_VERSION=4.1.2
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBEDTLS_VERSION/mbedtls-$MBEDTLS_VERSION.tar.bz2"
MBEDTLS_SHA256=392215e5e4846df04e2f4c4516079303bd73edde837d8f3e68fb4d4d7ddf0126
TF_PSA_CRYPTO_VERSION=1.1.2

LIGHTTPD_VERSION=1.4.85
LIGHTTPD_URL="https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$LIGHTTPD_VERSION.tar.xz"
LIGHTTPD_SHA256=18de51b393bac4a6827879e1a7ff377c169e414bae92cd245091d80fc2601d13

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

native_dependency_verified() (
    [ -f "$2/deneb-source.receipt" ] || exit 1
    expected=$(native_dependency_fingerprint "$1" "$2" 2>/dev/null) || exit 1
    recorded=$(cat "$2/deneb-source.receipt") || exit 1
    [ "$expected" = "$recorded" ]
)
