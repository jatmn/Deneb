/* SPDX-License-Identifier: MPL-2.0 */
#include "printer_identity.h"

#include "print_state_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void deneb_printer_identity_copy_line_or_default(const char *value,
                                                 const char *fallback,
                                                 char *out,
                                                 size_t out_sz)
{
    size_t len;

    if (!out || out_sz == 0)
        return;

    snprintf(out, out_sz, "%s",
             value && *value ? value : (fallback ? fallback : ""));
    len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' ||
                       out[len - 1] == ' ' || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }
    if (out[0] == '\0')
        snprintf(out, out_sz, "%s", fallback ? fallback : "");
}

void deneb_printer_identity_hostname(char *out, size_t out_sz)
{
    FILE *f;
    char line[128] = "";

    if (!out || out_sz == 0)
        return;

    f = fopen("/proc/sys/kernel/hostname", "r");
    if (f) {
        (void)fgets(line, sizeof(line), f);
        fclose(f);
    }
    deneb_printer_identity_copy_line_or_default(line,
                                                DENEB_PRINTER_DEFAULT_HOSTNAME,
                                                out, out_sz);
}

void deneb_printer_identity_friendly_name(char *out, size_t out_sz)
{
    FILE *f;
    char line[128] = "";
    char hostname[128];

    if (!out || out_sz == 0)
        return;

    deneb_printer_identity_hostname(hostname, sizeof(hostname));
    f = popen("uci -q get ultimaker.option.printer_name 2>/dev/null", "r");
    if (f) {
        (void)fgets(line, sizeof(line), f);
        pclose(f);
    }
    deneb_printer_identity_copy_line_or_default(line, hostname, out, out_sz);
}

void deneb_printer_identity_guid(char *out, size_t out_sz)
{
    FILE *f;
    char line[96] = "";
    size_t i;

    if (!out || out_sz == 0)
        return;

    f = popen("uci -q get ultimaker.option.host_guid 2>/dev/null || "
              "uci -q get deneb.system.guid 2>/dev/null", "r");
    if (f) {
        (void)fgets(line, sizeof(line), f);
        pclose(f);
    }
    deneb_printer_identity_copy_line_or_default(line,
                                                DENEB_PRINTER_DEFAULT_GUID,
                                                out, out_sz);
    for (i = 0; out[i]; i++)
        out[i] = (char)tolower((unsigned char)out[i]);
    if (!deneb_print_is_cluster_guid(out))
        snprintf(out, out_sz, "%s", DENEB_PRINTER_DEFAULT_GUID);
}

void deneb_printer_identity_display_id(char *out, size_t out_sz)
{
    FILE *f;
    char line[128] = "";

    if (!out || out_sz == 0)
        return;

    f = popen("uci -q get ultimaker.option.host_guid 2>/dev/null || "
              "cat /sys/class/net/eth0/address 2>/dev/null || "
              "cat /sys/class/net/apcli0/address 2>/dev/null", "r");
    if (f) {
        (void)fgets(line, sizeof(line), f);
        pclose(f);
    }

    deneb_printer_identity_copy_line_or_default(
        line, DENEB_PRINTER_UNAVAILABLE_ID, out, out_sz);
}
