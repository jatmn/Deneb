#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Exercise installer failure/success control flow with every device action mocked.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TASK_TMP=$(mktemp -d "${TMPDIR:-/tmp}/deneb-transitions.XXXXXX")
trap 'rm -rf "$TASK_TMP"' EXIT HUP INT TERM
for result in 0 1; do
    script="$TASK_TMP/install-$result.sh"
    {
        printf 'set -eu\n'
        printf 'log() { :; }\nvalidate_package() { return %s; }\n' "$result"
        for name in cleanup_tmp_deneb_packages backup_stock install_binary install_web_runtime \
            configure_digitalfactory_boot smoke_test_printsvc_tools install_motion_firmware_verify_cache \
            patch_motion_stack_boot_order smoke_test_printsvc_init_handoff smoke_test_binary \
            install_config restart_live_deneb_ui prune_stock_wifi_portal prune_stock_menu_ui \
            prune_stock_compile_all sleep; do
            printf '%s() { :; }\n' "$name"
        done
        printf 'schedule_reboot() { touch "%s/reboot-%s"; }\n' "$TASK_TMP" "$result"
        printf 'reboot_now() { touch "%s/reboot-%s"; }\n' "$TASK_TMP" "$result"
        sed -n '/^# Main/,$p' "$ROOT/ui/installer/update.sh"
    } > "$script"
    if sh "$script"; then
        [ "$result" -eq 0 ] || exit 1
    else
        [ "$result" -eq 1 ] || exit 1
    fi
done
[ -f "$TASK_TMP/reboot-0" ]
[ ! -e "$TASK_TMP/reboot-1" ]
printf '%s\n' 'Installer transition tests passed'
