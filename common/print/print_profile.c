/* SPDX-License-Identifier: MPL-2.0 */
#include "print_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void read_line_command(const char *cmd,
                              char *out,
                              size_t out_sz,
                              const char *fallback)
{
    FILE *f;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    f = popen(cmd, "r");
    if (f) {
        if (fgets(out, out_sz, f) && out[0]) {
            char *nl = strchr(out, '\n');
            if (nl)
                *nl = '\0';
            pclose(f);
            return;
        }
        pclose(f);
    }

    snprintf(out, out_sz, "%s", fallback ? fallback : "");
}

void deneb_print_profile_read_loaded_material_guid(char *out, size_t out_sz)
{
    read_line_command("uci -q get ultimaker.option.material_guid 2>/dev/null",
                      out, out_sz,
                      DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID);
}

void deneb_print_profile_read_loaded_nozzle_size(char *out, size_t out_sz)
{
    read_line_command("uci -q get ultimaker.option.nozzle_size 2>/dev/null",
                      out, out_sz,
                      DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE);
}

int deneb_print_profile_normalize_nozzle_size(const char *value,
                                              char *out,
                                              size_t out_sz)
{
    char tmp[24];
    size_t len;

    if (!out || out_sz == 0)
        return -1;

    snprintf(tmp, sizeof(tmp), "%s",
             value && *value ? value : DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE);
    len = strlen(tmp);
    while (len > 0 && isspace((unsigned char)tmp[len - 1]))
        tmp[--len] = '\0';
    while (tmp[0] && isspace((unsigned char)tmp[0]))
        memmove(tmp, tmp + 1, strlen(tmp));

    if (strcmp(tmp, "0.40") == 0)
        snprintf(tmp, sizeof(tmp), "0.4");
    else if (strcmp(tmp, "0.60") == 0)
        snprintf(tmp, sizeof(tmp), "0.6");
    else if (strcmp(tmp, "0.80") == 0)
        snprintf(tmp, sizeof(tmp), "0.8");

    if (strcmp(tmp, "0.25") != 0 &&
        strcmp(tmp, "0.4") != 0 &&
        strcmp(tmp, "0.6") != 0 &&
        strcmp(tmp, "0.8") != 0) {
        snprintf(out, out_sz, "%s", DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE);
        return -1;
    }

    snprintf(out, out_sz, "%s", tmp);
    return 0;
}

void deneb_print_profile_normalize_nozzle_id(const char *value,
                                             char *out,
                                             size_t out_sz)
{
    char tmp[24];
    size_t len;

    if (!out || out_sz == 0)
        return;
    out[0] = '\0';

    snprintf(tmp, sizeof(tmp), "%s",
             value && *value ? value : DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_SIZE);
    len = strlen(tmp);
    while (len > 0 && isspace((unsigned char)tmp[len - 1]))
        tmp[--len] = '\0';

    if (strstr(tmp, "mm"))
        snprintf(out, out_sz, "%s", tmp);
    else
        snprintf(out, out_sz, "%s mm", tmp);
}

void deneb_print_profile_read_loaded_nozzle_id(char *out, size_t out_sz)
{
    char nozzle[16];

    deneb_print_profile_read_loaded_nozzle_size(nozzle, sizeof(nozzle));
    deneb_print_profile_normalize_nozzle_id(nozzle, out, out_sz);
}

void deneb_print_profile_material_name_from_guid(const char *guid,
                                                 char *out,
                                                 size_t out_sz)
{
    if (!out || out_sz == 0)
        return;

    if (!guid || !*guid) {
        snprintf(out, out_sz, "Unknown");
    } else if (strcmp(guid, DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_GUID) == 0) {
        snprintf(out, out_sz, DENEB_PRINT_PROFILE_DEFAULT_MATERIAL_TYPE);
    } else {
        snprintf(out, out_sz, "%s", guid);
    }
}
