#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT
trap 'exit 1' HUP INT TERM
mkdir "$fixture/trusted" "$fixture/untrusted"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 1 \
    -subj /CN=localhost -addext subjectAltName=DNS:localhost \
    -keyout "$fixture/server.key" -out "$fixture/trusted/server.crt"
"$1" "$fixture/trusted/server.crt" "$fixture/server.key" "$fixture/trusted" "$fixture/untrusted"
