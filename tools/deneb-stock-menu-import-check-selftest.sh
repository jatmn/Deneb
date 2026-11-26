#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
fixture=$(mktemp -d "${TMPDIR:-/tmp}/deneb-stock-menu-import-check.XXXXXX")
trap 'rm -rf -- "$fixture"' EXIT
trap 'exit 1' HUP INT TERM

run_check() {
    if ! output=$(bash "$script_dir/deneb-stock-menu-import-check.sh" --repo-root "$1" 2>&1); then
        printf 'Scanner failed for %s\n%s\n' "$1" "$output" >&2
        exit 1
    fi
}

expect_line() {
    if ! printf '%s\n' "$output" | grep -Fxq -- "$1"; then
        printf 'Missing output line: %s\n%s\n' "$1" "$output" >&2
        exit 1
    fi
}

for root in "$fixture/Stock workspace" "$fixture/Stock [6.6]" "$fixture/Stock\workspace"; do
    stock="$root/rootfs/home/cygnus"
    mkdir -p "$stock/coordinator" "$stock/menu"
    printf '%s\n' 'from cygnus.menu.menu_settings import MACHINE_CONFIG' > "$stock/coordinator/stock consumer.py"
    printf '%s\n' 'from cygnus.menu.menu_settings import MAX_NUMBER_LVGL_OBJECT' > "$stock/menu/screen.py"

    run_check "$root"
    expect_line '  SURVIVING: coordinator/stock consumer.py'
    expect_line '  PRUNED-UI: menu/screen.py'
    expect_line '  INFO: Constant still required by surviving services: menu_settings.MACHINE_CONFIG'
    expect_line '  INFO: Constant only used by pruned UI (candidate for future removal): menu_settings.MAX_NUMBER_LVGL_OBJECT'
    expect_line '    - MACHINE_CONFIG'
    expect_line '=== Check complete ==='
done

root="$fixture/Stock workspace"
stock="$root/rootfs/home/cygnus"
rm -- "$stock/coordinator/stock consumer.py"
run_check "$root"
expect_line '  WARNING: No surviving import of menu_settings constants found.'
expect_line '  INFO: Constant only used by pruned UI (candidate for future removal): menu_settings.MAX_NUMBER_LVGL_OBJECT'
expect_line '    (none — retained for defensive/import-chain compatibility)'
expect_line '=== Check complete ==='

rm -- "$stock/menu/screen.py"
printf '%s\n' 'from cygnus.menu.menu_settings import MACHINE_CONFIG' > "$stock/coordinator/stock consumer.py"
run_check "$root"
expect_line '  INFO: Constant still required by surviving services: menu_settings.MACHINE_CONFIG'
expect_line '    - MACHINE_CONFIG'
expect_line '=== Check complete ==='

printf '%s\n' 'Stock menu import check selftest passed.'
