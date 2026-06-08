/* SPDX-License-Identifier: MPL-2.0 */
#include "diagnostics_export.h"

#include <stdio.h>

#define DENEB_DIAGNOSTICS_EXPORT_USB_PICK_COMMAND \
    "USB=$(awk '($2==\"/mnt/sda1\" || $2==\"/mnt/usb\" || " \
    "$2==\"/media/usb\") {print $2; exit}' /proc/mounts)"

const char *deneb_diagnostics_export_usb_available_command(void)
{
    return DENEB_DIAGNOSTICS_EXPORT_USB_PICK_COMMAND
           "; [ -n \"$USB\" ] && [ -w \"$USB\" ]";
}

int deneb_diagnostics_export_format_command(char *out, size_t out_sz)
{
    int n;

    if (!out || out_sz == 0)
        return -1;

    n = snprintf(
        out, out_sz,
        "(" DENEB_DIAGNOSTICS_EXPORT_USB_PICK_COMMAND "; "
        "[ -n \"$USB\" ] && [ -w \"$USB\" ] || "
        "{ echo 'No writable USB mount' >&2; exit 1; }; "
        "NAME=$(uci -q get ultimaker.option.printer_name || "
        "uci -q get system.@system[0].hostname || echo Deneb); "
        "VER=$(uci -q get ultimaker.version.nr || echo unknown); "
        "STAMP=$(date -u +%%Y-%%m-%%d_%%H-%%M); "
        "OUT=\"$USB/UM2C_${NAME}_v${VER}_${STAMP}\"; export OUT; "
        "TMP=" DENEB_DIAGNOSTICS_EXPORT_TMP_DIR
        "; rm -rf \"$TMP\"; mkdir -p \"$TMP\"; export TMP; "
        "cp -R /var/log/ultimaker \"$TMP/\" 2>/dev/null || "
        "cp -R /var/log \"$TMP/\" 2>/dev/null || true; "
        "logread > \"$TMP/logread.txt\" 2>/dev/null || true; "
        "dmesg > \"$TMP/dmesg.txt\" 2>/dev/null || true; "
        "ps w > \"$TMP/processes.txt\" 2>/dev/null || true; "
        "cat /proc/meminfo > \"$TMP/meminfo.txt\" 2>/dev/null || true; "
        "uptime > \"$TMP/uptime.txt\" 2>/dev/null || true; "
        "cp /var/log/ultimaker/digitalfactory.log* \"$TMP/\" 2>/dev/null || true; "
        "/etc/init.d/digitalfactory status > \"$TMP/digitalfactory_service_status.txt\" 2>&1 || true; "
        "uci -q show ultimaker | grep digitalfactory "
        "> \"$TMP/digitalfactory_uci.txt\" 2>/dev/null || true; "
        "cp /tmp/deneb*.log \"$TMP/\" 2>/dev/null || true; "
        "cp /tmp/deneb-df-status \"$TMP/\" 2>/dev/null || true; "
        "uci show | grep -v ssid | grep -v key | grep -v encryption "
        "> \"$TMP/uci_dump\" 2>/dev/null || true; "
        "tar -czf \"$OUT.tar.gz\" -C \"$TMP\" .) "
        ">" DENEB_DIAGNOSTICS_EXPORT_LOG_PATH " 2>&1 &");

    if (n < 0 || n >= (int)out_sz)
        return -1;

    return 0;
}
