#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Confirm DenebUM2CNetworkPrinting.curapackage has the Cura plugin layout.

set -euo pipefail

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

if [ "$#" -ne 1 ] || [ -z "$1" ]; then
    die "usage: tools/inspect-cura-plugin-package.sh PACKAGE.curapackage"
fi

package=$1
[ -f "$package" ] || die "Missing Cura package: $package"
command -v unzip >/dev/null 2>&1 || die "unzip is required to inspect the Cura package"

unzip -tqq "$package" || die "Cura package is not a readable zip: $package"
members=$(unzip -Z1 "$package")

case "$members" in
    *\\*) die "Cura package contains backslash entry names" ;;
esac

require_member() {
    printf '%s\n' "$members" | grep -Fxq "$1" || die "Cura package missing $1"
}

require_member package.json
require_member files/plugins/DenebUM2CNetworkPrinting/__init__.py
require_member files/plugins/DenebUM2CNetworkPrinting/plugin.json
require_member files/plugins/DenebUM2CNetworkPrinting/resources/definitions/deneb_ultimaker2_plus_connect.def.json

if printf '%s\n' "$members" | grep -q '__pycache__\|\.pyc$'; then
    die "Cura package contains Python cache files"
fi

if printf '%s\n' "$members" | grep -Fxq 'files/plugins/DenebUM2CNetworkPrinting/package.json'; then
    die "Cura package must keep package.json at the archive root"
fi

meta=$(unzip -p "$package" package.json | tr -d '[:space:]')
case "$meta" in
    *'"package_id":"DenebUM2CNetworkPrinting"'*) ;;
    *) die "package.json package_id must be DenebUM2CNetworkPrinting" ;;
esac
case "$meta" in
    *'"package_type":"plugin"'*) ;;
    *) die "package.json package_type must be plugin" ;;
esac

printf 'Cura package layout: PASS\n'
