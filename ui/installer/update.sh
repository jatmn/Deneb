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
    for f in deneb-ui deneb-ui.init deneb-api deneb-mdns deneb-printsvc deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-cli-selftest deneb-printsvc-init-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest lighttpd deneb-api.init deneb-web.init deneb-mdns.init deneb-printsvc.init lighttpd.conf manifest.txt en.json; do
        if [ ! -f "/tmp/update/${f}" ]; then
            log "ERROR: missing required file: ${f}"
            missing=1
        fi
    done
    if [ -f /tmp/update/manifest.txt ]; then
        if ! grep -Eq '^channel: (experimental|nightly|stable)$' /tmp/update/manifest.txt ||
           ! grep -Fx 'native_printsvc: experimental' /tmp/update/manifest.txt >/dev/null ||
           ! grep -Fx 'native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction' /tmp/update/manifest.txt >/dev/null; then
            log "ERROR: update manifest missing native print-service release gate"
            missing=1
        fi
    fi
    if [ ! -d /tmp/update/deneb-printsvc-macros ]; then
        log "ERROR: missing required directory: deneb-printsvc-macros"
        missing=1
    fi
    if [ ! -d /tmp/update/www ]; then
        log "ERROR: missing required directory: www"
        missing=1
    fi
    if find /tmp/update \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
        -print | grep . >/dev/null 2>&1; then
        log "ERROR: Python driver artifact found in update package"
        find /tmp/update \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
            -print 2>/dev/null || true
        missing=1
    fi
    if [ "$missing" -eq 0 ] &&
       ! /tmp/update/deneb-printsvc-native-audit --package-dir /tmp/update >/tmp/deneb-printsvc-native-audit.log 2>&1; then
        log "ERROR: native print-service de-Python audit failed"
        cat /tmp/deneb-printsvc-native-audit.log 2>/dev/null || true
        missing=1
    fi
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

install_web_runtime() {
    /etc/init.d/deneb-mdns stop 2>/dev/null || true
    /etc/init.d/deneb-web stop 2>/dev/null || true
    /etc/init.d/deneb-api stop 2>/dev/null || true

    cp /tmp/update/deneb-api /usr/bin/deneb-api
    chmod 0755 /usr/bin/deneb-api
    log "installed deneb-api to /usr/bin/deneb-api"

    cp /tmp/update/deneb-mdns /usr/bin/deneb-mdns
    chmod 0755 /usr/bin/deneb-mdns
    log "installed deneb-mdns to /usr/bin/deneb-mdns"

    cp /tmp/update/deneb-printsvc /usr/bin/deneb-printsvc
    chmod 0755 /usr/bin/deneb-printsvc
    log "installed deneb-printsvc to /usr/bin/deneb-printsvc"

    cp /tmp/update/deneb-printsvc-smoke /usr/bin/deneb-printsvc-smoke
    chmod 0755 /usr/bin/deneb-printsvc-smoke
    log "installed deneb-printsvc-smoke to /usr/bin/deneb-printsvc-smoke"

    cp /tmp/update/deneb-printsvc-smoke-verify /usr/bin/deneb-printsvc-smoke-verify
    chmod 0755 /usr/bin/deneb-printsvc-smoke-verify
    log "installed deneb-printsvc-smoke-verify to /usr/bin/deneb-printsvc-smoke-verify"

    cp /tmp/update/deneb-printsvc-smoke-compare /usr/bin/deneb-printsvc-smoke-compare
    chmod 0755 /usr/bin/deneb-printsvc-smoke-compare
    log "installed deneb-printsvc-smoke-compare to /usr/bin/deneb-printsvc-smoke-compare"

    cp /tmp/update/deneb-printsvc-smoke-selftest /usr/bin/deneb-printsvc-smoke-selftest
    chmod 0755 /usr/bin/deneb-printsvc-smoke-selftest
    log "installed deneb-printsvc-smoke-selftest to /usr/bin/deneb-printsvc-smoke-selftest"

    cp /tmp/update/deneb-printsvc-cli-selftest /usr/bin/deneb-printsvc-cli-selftest
    chmod 0755 /usr/bin/deneb-printsvc-cli-selftest
    log "installed deneb-printsvc-cli-selftest to /usr/bin/deneb-printsvc-cli-selftest"

    cp /tmp/update/deneb-printsvc-init-selftest /usr/bin/deneb-printsvc-init-selftest
    chmod 0755 /usr/bin/deneb-printsvc-init-selftest
    log "installed deneb-printsvc-init-selftest to /usr/bin/deneb-printsvc-init-selftest"

    cp /tmp/update/deneb-printsvc-native-audit /usr/bin/deneb-printsvc-native-audit
    chmod 0755 /usr/bin/deneb-printsvc-native-audit
    log "installed deneb-printsvc-native-audit to /usr/bin/deneb-printsvc-native-audit"

    cp /tmp/update/deneb-printsvc-native-audit-selftest /usr/bin/deneb-printsvc-native-audit-selftest
    chmod 0755 /usr/bin/deneb-printsvc-native-audit-selftest
    log "installed deneb-printsvc-native-audit-selftest to /usr/bin/deneb-printsvc-native-audit-selftest"

    mkdir -p /etc/deneb/marlindriver/gcode
    cp /tmp/update/deneb-printsvc-macros/*.gcode /etc/deneb/marlindriver/gcode/
    chmod 0644 /etc/deneb/marlindriver/gcode/*.gcode
    log "installed Deneb print-service macro defaults"

    cp /tmp/update/lighttpd /usr/sbin/lighttpd
    chmod 0755 /usr/sbin/lighttpd
    log "installed lighttpd to /usr/sbin/lighttpd"

    mkdir -p /etc/deneb /www/deneb
    cp /tmp/update/manifest.txt /etc/deneb/manifest.txt
    chmod 0644 /etc/deneb/manifest.txt
    log "installed Deneb package manifest"

    cp /tmp/update/lighttpd.conf /etc/deneb/lighttpd.conf
    cp -r /tmp/update/www/* /www/deneb/
    log "installed Deneb web assets"

    cp /tmp/update/deneb-api.init /etc/init.d/deneb-api
    cp /tmp/update/deneb-web.init /etc/init.d/deneb-web
    cp /tmp/update/deneb-mdns.init /etc/init.d/deneb-mdns
    cp /tmp/update/deneb-printsvc.init /etc/init.d/deneb-printsvc
    chmod 0755 /etc/init.d/deneb-api /etc/init.d/deneb-web /etc/init.d/deneb-mdns /etc/init.d/deneb-printsvc
    log "installed Deneb web and print service init scripts"

    [ -f /etc/config/deneb ] || touch /etc/config/deneb
    uci -q set deneb.web=web 2>/dev/null || true
    uci -q set deneb.web.enabled='1' 2>/dev/null || true
    uci -q set deneb.mdns=mdns 2>/dev/null || true
    uci -q set deneb.mdns.enabled='1' 2>/dev/null || true
    uci -q set deneb.printsvc=printsvc 2>/dev/null || true
    if [ -z "$(uci -q get ultimaker.option.nozzle_size 2>/dev/null || true)" ]; then
        uci -q set ultimaker.option.nozzle_size='0.4' 2>/dev/null || true
        log "defaulted nozzle size to 0.40 mm"
    fi
    if [ -z "$(uci -q get ultimaker.option.material_guid 2>/dev/null || true)" ]; then
        uci -q set ultimaker.option.material_guid='506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9' 2>/dev/null || true
        log "defaulted material to Generic PLA"
    fi
    mdns_machine="$(uci -q get deneb.mdns.machine 2>/dev/null || true)"
    if [ -z "${mdns_machine}" ] || [ "${mdns_machine}" = "ultimaker2_plus_connect" ] || [ "${mdns_machine}" = "9066" ]; then
        uci -q set deneb.mdns.machine='deneb_um2c' 2>/dev/null || true
    fi
    uci -q commit deneb 2>/dev/null || true
    uci -q commit ultimaker 2>/dev/null || true

    /etc/init.d/deneb-api enable 2>/dev/null || true
    /etc/init.d/deneb-web enable 2>/dev/null || true
    /etc/init.d/deneb-mdns enable 2>/dev/null || true
    /etc/init.d/deneb-printsvc enable 2>/dev/null || true
    log "enabled Deneb web services for next boot"
}

prune_stock_wifi_portal() {
    # Disable the WiFi AP at boot. The AP/router path was used for the stock
    # captive-portal WiFi setup; Deneb is client-only and uses USB wifi.txt
    # import instead. Preserve existing STA config if WiFi is already set up.
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

    # Disable server-side AP plumbing. IPv4/IPv6 clients still use netifd and
    # udhcpc; Deneb should not serve DHCP, DNS, or router advertisements. The
    # service binaries live in the read-only squashfs base, so stopping and
    # disabling them is the runtime win available to a .deneb package.
    /etc/init.d/dnsmasq stop 2>/dev/null || true
    /etc/init.d/dnsmasq disable 2>/dev/null || true
    /etc/init.d/odhcpd stop 2>/dev/null || true
    /etc/init.d/odhcpd disable 2>/dev/null || true
    uci delete dhcp.wlan 2>/dev/null || true
    uci commit dhcp 2>/dev/null || true
    log "disabled AP DHCP/DNS/IPv6 server services"

    # Disable nodogsplash captive portal
    /etc/init.d/nodogsplash stop 2>/dev/null || true
    /etc/init.d/nodogsplash disable 2>/dev/null || true
    uci set nodogsplash.@nodogsplash[0].enabled='0' 2>/dev/null || true
    uci commit nodogsplash 2>/dev/null || true

    # Hide stock WiFi portal files. Deneb replaces the AP/captive-portal setup
    # path with USB wifi.txt import. These paths usually originate in the
    # read-only squashfs base, so rm creates overlayfs whiteouts rather than
    # reclaiming base-image flash.
    if [ -d /home/cygnus/wificonnect ]; then
        rm -rf /home/cygnus/wificonnect
        log "hid obsolete stock wificonnect server"
    fi

    if [ -d /home/cygnus-web-assets ]; then
        rm -rf /home/cygnus-web-assets
        log "hid obsolete stock WiFi setup web assets"
    fi
    rm -f /home/cygnus-web-assets_git_hash

    if [ -d /etc/nodogsplash/htdocs ]; then
        rm -rf /etc/nodogsplash/htdocs
        log "hid obsolete nodogsplash web assets"
    fi

    # Clean backups left by older Deneb installers now that rollback no longer
    # restores the captive-portal WiFi setup.
    rm -rf "${DENEB_BACKUP_DIR}/wificonnect" \
           "${DENEB_BACKUP_DIR}/cygnus-web-assets" \
           "${DENEB_BACKUP_DIR}/nodogsplash/htdocs"
    rm -f "${DENEB_BACKUP_DIR}/cygnus-web-assets_git_hash"

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

install_motion_firmware_verify_cache() {
    local programmer_dir="/home/atmel_programmer"
    local prog="${programmer_dir}/prog.sh"
    local stock_prog="${programmer_dir}/prog.sh.stock"
    local hex="${programmer_dir}/cygnus-marlin.hex"
    local cache_dir="/etc/deneb"
    local cache_file="${cache_dir}/motion-controller-firmware.sha256"

    if [ ! -f "${prog}" ] || [ ! -f "${hex}" ]; then
        log "motion firmware programmer not found; skipping verify cache"
        return
    fi

    if ! grep -q "DENEB_MOTION_FW_CACHE" "${prog}" 2>/dev/null ||
       ! grep -q "reset_motion_controller" "${prog}" 2>/dev/null; then
        if [ ! -f "${stock_prog}" ]; then
            cp "${prog}" "${stock_prog}"
            chmod 0755 "${stock_prog}"
            log "backed up stock motion firmware programmer"
        fi

        cat > "${prog}" <<'EOF'
#!/bin/sh
# DENEB_MOTION_FW_CACHE
set -u

HERE=$(dirname "$(readlink -f "$0")")/
ORIG="${HERE}prog.sh.stock"
CACHE_DIR="/etc/deneb"
CACHE_FILE="${CACHE_DIR}/motion-controller-firmware.sha256"
HEX="${1:-}"

log() {
    logger -t deneb-motion-fw "$*" 2>/dev/null || true
    echo "deneb-motion-fw: $*" >&2
}

reset_motion_controller() {
    if [ -e /dev/gpio/avr_reset ]; then
        printf '0' > /dev/gpio/avr_reset
        sleep 0.02
        printf '1' > /dev/gpio/avr_reset
        sleep 0.15
        log "reset motion controller after cached firmware check"
    else
        log "avr_reset gpio unavailable; continuing without reset"
    fi
}

hash_file() {
    sha256sum "$1" 2>/dev/null | awk '{print $1}'
}

if [ "$#" -ge 1 ] && [ -f "${HEX}" ] && [ -x "${ORIG}" ]; then
    current_hash=$(hash_file "${HEX}" || true)
    cached_hash=""
    if [ -f "${CACHE_FILE}" ]; then
        cached_hash=$(cat "${CACHE_FILE}" 2>/dev/null || true)
    fi

    if [ -n "${current_hash}" ] && [ "${current_hash}" = "${cached_hash}" ]; then
        log "motion controller firmware hash already verified; skipping programmer"
        reset_motion_controller
        exit 0
    fi

    "${ORIG}" "$@"
    rc=$?
    if [ "${rc}" -eq 0 ] && [ -n "${current_hash}" ]; then
        mkdir -p "${CACHE_DIR}"
        printf '%s\n' "${current_hash}" > "${CACHE_FILE}"
        chmod 0644 "${CACHE_FILE}"
        log "cached verified firmware hash ${current_hash}"
    fi
    exit "${rc}"
fi

exec "${ORIG}" "$@"
EOF
        chmod 0755 "${prog}"
        log "installed motion firmware verify cache wrapper"
    else
        log "motion firmware verify cache wrapper already installed"
    fi

    mkdir -p "${cache_dir}"
}

patch_motion_stack_boot_order() {
    local gpio_init="/etc/init.d/marlin-gpio"
    local printserver_init="/etc/init.d/printserver"
    local coordinator_init="/etc/init.d/coordinator"
    local backup_dir="${DENEB_BACKUP_DIR}/init"

    mkdir -p "${backup_dir}"

    if [ -f "${gpio_init}" ]; then
        if [ ! -f "${backup_dir}/marlin-gpio.orig" ]; then
            cp "${gpio_init}" "${backup_dir}/marlin-gpio.orig"
        fi
        if grep -q '^START=99$' "${gpio_init}"; then
            sed -i 's/^START=99$/START=60/' "${gpio_init}"
            /etc/init.d/marlin-gpio enable 2>/dev/null || true
            log "moved marlin-gpio before printserver"
        fi
        rm -f /etc/rc.d/S99marlin-gpio
    fi

    if [ -f "${printserver_init}" ] &&
       ! grep -q "DENEB_PRINTSERVER_NATIVE_GATE" "${printserver_init}" 2>/dev/null; then
        if [ ! -f "${backup_dir}/printserver.orig" ]; then
            cp "${printserver_init}" "${backup_dir}/printserver.orig"
        fi

        cat > "${printserver_init}" <<'EOF'
#!/bin/sh /etc/rc.common
# Start the printer server
# DENEB_PRINTSERVER_START_WAIT
# DENEB_PRINTSERVER_NATIVE_GATE

# shellcheck disable=SC2034
START=95
# shellcheck disable=SC2034
STOP=15

stop_stock_print_service() {
    ps w | grep '/home/cygnus/marlindriver/print_service.py' |
        grep -v grep | while read -r PID REST; do
            case "${PID}" in
                ''|*[!0-9]*) continue ;;
            esac
            kill "${PID}" 2>/dev/null || true
            sleep 1
            kill -9 "${PID}" 2>/dev/null || true
        done
    rm -f /var/run/printserver.pid
}

start() {
    logger -t deneb-printserver "native deneb-printsvc owns the print backend"
    stop_stock_print_service
    return 0
}

stop() {
    echo Stopping print server
    if [ -f /var/run/printserver.pid ]; then
        PID=$(cat /var/run/printserver.pid)
        kill -9 "${PID}" 2>/dev/null || true
        rm -f /var/run/printserver.pid
    fi
    stop_stock_print_service
}
EOF
        chmod 0755 "${printserver_init}"
        log "installed native printserver handoff shim"
    fi

    if [ -f "${coordinator_init}" ] &&
       ! grep -q "DENEB_COORDINATOR_START_WAIT" "${coordinator_init}" 2>/dev/null; then
        if [ ! -f "${backup_dir}/coordinator.orig" ]; then
            cp "${coordinator_init}" "${backup_dir}/coordinator.orig"
        fi

        cat > "${coordinator_init}" <<'EOF'
#!/bin/sh /etc/rc.common
# Start the Coordinator node group
# DENEB_COORDINATOR_START_WAIT

# shellcheck disable=SC2034
START=96
# shellcheck disable=SC2034
STOP=14

STOCK_COORDINATOR_INIT="/home/deneb/backups/deneb-ui/init/coordinator.orig"

start() {
    if [ -x "${STOCK_COORDINATOR_INIT}" ]; then
        logger -t deneb-coordinator "delegating to backed-up stock coordinator init"
        "${STOCK_COORDINATOR_INIT}" start
        return $?
    fi

    logger -t deneb-coordinator "stock coordinator fallback unavailable"
    return 1
}

stop() {
    echo Stopping coordinator
    if [ -x "${STOCK_COORDINATOR_INIT}" ]; then
        "${STOCK_COORDINATOR_INIT}" stop 2>/dev/null || true
    fi
    if [ -f /var/run/coordinator.pid ]; then
        PID=$(cat /var/run/coordinator.pid)
        kill -9 "${PID}" 2>/dev/null || true
        rm -f /var/run/coordinator.pid
    fi
}
EOF
        chmod 0755 "${coordinator_init}"
        log "installed coordinator recovery handoff shim"
    fi
}

rollback_to_stock_menu() {
    log "rolling back to stock menu service"
    /etc/init.d/deneb-ui stop 2>/dev/null || true
    /etc/init.d/deneb-ui disable 2>/dev/null || true

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

smoke_test_printsvc_tools() {
    if ! /usr/bin/deneb-printsvc-smoke-selftest >/tmp/deneb-printsvc-smoke-selftest.log 2>&1; then
        log "ERROR: deneb-printsvc smoke tool selftest failed"
        cat /tmp/deneb-printsvc-smoke-selftest.log 2>/dev/null || true
        exit 1
    fi
    log "deneb-printsvc smoke tool selftest passed"

    if ! /usr/bin/deneb-printsvc-cli-selftest /usr/bin/deneb-printsvc >/tmp/deneb-printsvc-cli-selftest.log 2>&1; then
        log "ERROR: deneb-printsvc CLI selftest failed"
        cat /tmp/deneb-printsvc-cli-selftest.log 2>/dev/null || true
        exit 1
    fi
    log "deneb-printsvc CLI selftest passed"

    if ! /usr/bin/deneb-printsvc-native-audit-selftest >/tmp/deneb-printsvc-native-audit-selftest.log 2>&1; then
        log "ERROR: deneb-printsvc native audit selftest failed"
        cat /tmp/deneb-printsvc-native-audit-selftest.log 2>/dev/null || true
        exit 1
    fi
    log "deneb-printsvc native audit selftest passed"
}

smoke_test_printsvc_init_handoff() {
    if ! DENEB_PRINTSVC_INIT=/etc/init.d/deneb-printsvc \
        DENEB_INSTALLER=/tmp/update/update.sh \
        DENEB_PRINTSERVER_INIT=/etc/init.d/printserver \
        /usr/bin/deneb-printsvc-init-selftest >/tmp/deneb-printsvc-init-selftest.log 2>&1; then
        log "ERROR: deneb-printsvc init handoff selftest failed"
        cat /tmp/deneb-printsvc-init-selftest.log 2>/dev/null || true
        exit 1
    fi
    log "deneb-printsvc init handoff selftest passed"
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
install_web_runtime
smoke_test_printsvc_tools
install_motion_firmware_verify_cache
patch_motion_stack_boot_order
smoke_test_printsvc_init_handoff
smoke_test_binary
install_config
prune_stock_wifi_portal
prune_stock_menu_ui

log "installation complete"
schedule_reboot
sleep 2
reboot_now
