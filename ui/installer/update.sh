#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Deneb Touchscreen UI installer.
# Deploys via the Deneb USB package updater after bootstrap.
# Replaces the stock Python/Cygnus menu with the native LVGL UI.

set -eu

DENEB_HOME="/home/deneb"
DENEB_BACKUP_DIR="${DENEB_HOME}/backups/deneb-ui"
DENEB_REBOOT_SCHEDULED=0
STOCK_MENU_WAS_ENABLED=0

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

    if [ -x /etc/init.d/menu ] && /etc/init.d/menu enabled >/dev/null 2>&1; then
        STOCK_MENU_WAS_ENABLED=1
    fi
}

# Install the Deneb UI binary
install_binary() {
    cp /tmp/update/deneb-ui /usr/bin/deneb-ui
    chmod 0755 /usr/bin/deneb-ui
    log "installed deneb-ui to /usr/bin/deneb-ui"

    ln -sf /usr/bin/deneb-ui /usr/bin/deneb-df-bridge
    log "installed deneb-df-bridge symlink to /usr/bin/deneb-ui"
}

patch_stock_coordinator() {
    local command_file="/home/cygnus/coordinator/companion/printer_service_command.py"
    local backup_file="${DENEB_BACKUP_DIR}/printer_service_command.py.orig"

    # The stock CommandWriter registers a ZMQ POLLOUT watch before it has
    # queued work. Writable sockets wake immediately, which can pin
    # coordinator.py after Deneb updates/restarts.
    if [ ! -f "${command_file}" ]; then
        log "stock coordinator command writer not found; skipping coordinator patch"
        return
    fi

    if [ ! -f "${backup_file}" ]; then
        cp "${command_file}" "${backup_file}"
        log "backed up stock coordinator command writer"
    fi

    python3 - <<'PY'
from pathlib import Path

path = Path("/home/cygnus/coordinator/companion/printer_service_command.py")
src = path.read_text()
changed = False

add_watch = "        self.getManager().getSpinner().addWatch(self._socket, self._onPollIn, self._onPollOut, None)\n"
add_watch_patched = (
    add_watch
    + "        self.getManager().getSpinner().setPollState(self._socket, zmq.POLLOUT, False)\n"
)
if add_watch_patched not in src:
    if add_watch not in src:
        raise SystemExit("missing coordinator addWatch pattern")
    src = src.replace(add_watch, add_watch_patched, 1)
    changed = True

empty_queue = "        if len(self._send_queue) == 0:\n            return\n"
empty_queue_patched = (
    "        if len(self._send_queue) == 0:\n"
    "            self.getManager().getSpinner().setPollState(self._socket, zmq.POLLOUT, False)\n"
    "            return\n"
)
if empty_queue_patched not in src:
    if empty_queue not in src:
        raise SystemExit("missing coordinator empty-queue pattern")
    src = src.replace(empty_queue, empty_queue_patched, 1)
    changed = True

if changed:
    path.write_text(src)
PY
    log "patched stock coordinator command poll state"
}

prune_stock_wifi_portal() {
    # Disable the WiFi AP at boot. The AP was used for the stock captive-portal
    # WiFi setup which is replaced by USB wifi.txt import in Deneb.
    # Only disable the AP - preserve existing STA config if WiFi is already set up.
    local sta_disabled
    sta_disabled=$(uci -q get wireless.sta.disabled 2>/dev/null || echo "1")

    uci set wireless.ap.disabled='1'
    uci set wireless.ap.hidden='1'
    # Only disable the radio if STA is also not configured
    if [ "$sta_disabled" = "1" ]; then
        local sta_ssid
        sta_ssid=$(uci -q get wireless.sta.ssid 2>/dev/null || echo "")
        if [ -z "$sta_ssid" ]; then
            uci set wireless.radio0.disabled='1'
            log "WiFi radio disabled (no WiFi config found)"
        fi
    fi
    uci commit wireless

    # Disable nodogsplash captive portal
    /etc/init.d/nodogsplash stop 2>/dev/null || true
    /etc/init.d/nodogsplash disable 2>/dev/null || true
    uci set nodogsplash.@nodogsplash[0].enabled='0' 2>/dev/null || true
    uci commit nodogsplash 2>/dev/null || true

    # Move stock WiFi portal files into Deneb backup storage so stock-menu
    # rollback can restore the captive-portal setup path.
    if [ -d /home/cygnus/wificonnect ]; then
        if [ ! -e "${DENEB_BACKUP_DIR}/wificonnect" ]; then
            mv /home/cygnus/wificonnect "${DENEB_BACKUP_DIR}/wificonnect"
            log "backed up stock wificonnect server"
        else
            rm -rf /home/cygnus/wificonnect
            log "removed duplicate stock wificonnect server"
        fi
    fi

    # Move nodogsplash web assets if present.
    if [ -d /etc/nodogsplash/htdocs ]; then
        mkdir -p "${DENEB_BACKUP_DIR}/nodogsplash"
        if [ ! -e "${DENEB_BACKUP_DIR}/nodogsplash/htdocs" ]; then
            mv /etc/nodogsplash/htdocs "${DENEB_BACKUP_DIR}/nodogsplash/htdocs"
            log "backed up nodogsplash web assets"
        else
            rm -rf /etc/nodogsplash/htdocs
            log "removed duplicate nodogsplash web assets"
        fi
    fi

    log "stock WiFi AP portal disabled"
}

prune_stock_menu_ui() {
    local menu_dir="/home/cygnus/menu"

    if [ ! -d "${menu_dir}" ]; then
        log "stock menu directory not found; skipping stock UI prune"
        return
    fi

    # Keep menu_settings.py and machine_config.json. Coordinator, file handling,
    # firmware update handling, and utility modules still import those constants.
    # The removed files are the dormant touchscreen implementation that Deneb
    # replaces with /usr/bin/deneb-ui.
    rm -rf \
        "${menu_dir}/executor.py" \
        "${menu_dir}/controldialog.py" \
        "${menu_dir}/machine.py" \
        "${menu_dir}/pylvgl.py" \
        "${menu_dir}/screen.py" \
        "${menu_dir}/style.py" \
        "${menu_dir}/gui_companion" \
        "${menu_dir}/helpers" \
        "${menu_dir}/img" \
        "${menu_dir}/navigator" \
        "${menu_dir}/screens" \
        "${menu_dir}/templates" \
        "${menu_dir}/ui_elements"

    find "${menu_dir}" -type d -name __pycache__ -prune -exec rm -rf {} + 2>/dev/null || true
    log "pruned stock Python touchscreen UI; retained shared menu_settings"
}

rollback_to_stock_menu() {
    log "rolling back to stock menu service"
    /etc/init.d/deneb-ui stop 2>/dev/null || true
    /etc/init.d/deneb-ui disable 2>/dev/null || true

    if [ -d "${DENEB_BACKUP_DIR}/wificonnect" ] && [ ! -e /home/cygnus/wificonnect ]; then
        mv "${DENEB_BACKUP_DIR}/wificonnect" /home/cygnus/wificonnect
        log "restored stock wificonnect server"
    fi
    if [ -d "${DENEB_BACKUP_DIR}/nodogsplash/htdocs" ] && [ ! -e /etc/nodogsplash/htdocs ]; then
        mkdir -p /etc/nodogsplash
        mv "${DENEB_BACKUP_DIR}/nodogsplash/htdocs" /etc/nodogsplash/htdocs
        log "restored nodogsplash web assets"
    fi
    /etc/init.d/nodogsplash enable 2>/dev/null || true

    if [ -x /etc/init.d/menu ]; then
        /etc/init.d/menu enable 2>/dev/null || true
        /etc/init.d/menu start 2>/dev/null || true
    fi
}

smoke_test_binary() {
    if ! /usr/bin/deneb-ui --smoke-test >/tmp/deneb-ui-smoke.log 2>&1; then
        log "ERROR: deneb-ui smoke test failed"
        cat /tmp/deneb-ui-smoke.log 2>/dev/null || true
        rollback_to_stock_menu
        exit 1
    fi
    log "deneb-ui smoke test passed"
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
patch_stock_coordinator
smoke_test_binary
install_config
prune_stock_wifi_portal
prune_stock_menu_ui

log "installation complete"
schedule_reboot
sleep 2
reboot_now
