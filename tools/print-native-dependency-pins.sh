#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/native-dependency-pins.sh"

# This order is consumed by build-update-release.ps1.
printf '%s\n' "$ZMQ_VERSION" "$ZMQ_URL" "$ZMQ_SHA256" \
    "$LIGHTTPD_VERSION" "$LIGHTTPD_URL" "$LIGHTTPD_SHA256" "$MBEDTLS_VERSION"
