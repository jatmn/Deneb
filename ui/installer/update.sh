#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Deneb Touchscreen UI installer.
# Deploys via the stock USB firmware update mechanism.
# Replaces the stock Python/Cygnus menu with the native LVGL UI.

set -eu

DENEB_HOME="/home/deneb"
DENEB_BACKUP_DIR="${DENEB_HOME}/backups/deneb-ui"
DENEB_REBOOT_SCHEDULED=0

log() {
    logger -t deneb-ui "$*"
    echo "deneb-ui: $*"
}

reboot_now() {
    sync
    ubus call system reboot >/dev/null 2>&1 && exit 0
    /sbin/reboot >/dev/null 2>&1 && exit 0
    /bin/busybox reboot -f >/dev/null 2>&1 && exit 0
    echo b > /proc/sysrq-trigger
}

schedule_reboot() {
    if [ "${DENEB_REBOOT_SCHEDULED}" = "1" ]; then
        return
    fi

    DENEB_REBOOT_SCHEDULED=1
    log "scheduling reboot watchdog"
    (
        sleep 8
        reboot_now
    ) >/dev/null 2>&1 &
}

# Validate required files exist in the update package
validate_package() {
    local missing=0
    for f in deneb-ui deneb-ui.init en.json; do
        if [ ! -f "/tmp/update/${f}" ]; then
            log "ERROR: missing required file: ${f}"
            missing=1
        fi
    done
    [ "$missing" -eq 0 ] || exit 1
}

# Backup files we're about to replace
backup_stock() {
    mkdir -p "${DENEB_BACKUP_DIR}"

    # Backup stock menu init script
    if [ -f /etc/init.d/menu ] && [ ! -f "${DENEB_BACKUP_DIR}/menu.init.orig" ]; then
        cp /etc/init.d/menu "${DENEB_BACKUP_DIR}/menu.init.orig"
        log "backed up /etc/init.d/menu"
    fi
}

# Install the Deneb UI binary
install_binary() {
    cp /tmp/update/deneb-ui /usr/bin/deneb-ui
    chmod 0755 /usr/bin/deneb-ui
    log "installed deneb-ui to /usr/bin/deneb-ui"
}

# Install locale files
install_locales() {
    mkdir -p /etc/deneb/locales
    for f in /tmp/update/*.json; do
        [ -f "$f" ] || continue
        local name
        name=$(basename "$f")
        cp "$f" "/etc/deneb/locales/${name}"
        log "installed locale: ${name}"
    done
}

# Install init script and swap stock menu for Deneb UI on next boot
install_init() {
    # Install Deneb UI init script
    cp /tmp/update/deneb-ui.init /etc/init.d/deneb-ui
    chmod 0755 /etc/init.d/deneb-ui

    # Disable stock Cygnus menu for the next boot. Do not stop it here:
    # this script is launched from the stock update UI, and killing that
    # process mid-update can leave the printer stuck on "updating firmware".
    if [ -f /etc/init.d/menu ]; then
        /etc/init.d/menu disable 2>/dev/null || true
        log "disabled stock menu service for next boot"
    fi

    # Enable Deneb UI service
    /etc/init.d/deneb-ui enable
    log "enabled deneb-ui service"

    # Set default language via UCI
    if ! uci -q get deneb.system.language >/dev/null 2>&1; then
        uci -q set deneb.system=system 2>/dev/null || true
        uci -q set deneb.system.language='en' 2>/dev/null || true
        uci -q commit deneb 2>/dev/null || true
    fi
}

# Install locale and init even if binary wasn't updated
install_config() {
    install_locales
    install_init
}

# Main
log "starting Deneb Touchscreen UI installation"
trap schedule_reboot EXIT

validate_package
backup_stock
install_binary
install_config

log "installation complete"
schedule_reboot
sleep 2
reboot_now
