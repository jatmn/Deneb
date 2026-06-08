/* SPDX-License-Identifier: MPL-2.0 */
#include "print_profile.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const deneb_print_profile_material_choice_t material_choices[] = {
    {"Generic PLA", "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9"},
    {"Generic Tough PLA", "9d5d2d7c-4e77-441c-85a0-e9eefd4aa68c"},
    {"Generic PETG", "1cbfaeb3-1906-4b26-b2e7-6f777a8c197a"},
    {"Generic ABS", "60636bb4-518f-42e7-8237-fe77b194ebe0"},
    {"Generic CPE", "12f41353-1a33-415e-8b4f-a775a6c70cc6"},
    {"Generic CPE+", "e2409626-b5a0-4025-b73e-b58070219259"},
    {"Generic Nylon", "28fb4162-db74-49e1-9008-d05f1e8bef5c"},
    {"Generic PC", "98c05714-bf4e-4455-ba27-57d74fe331e4"},
    {"Generic PP", "aa22e9c7-421f-4745-afc2-81851694394a"},
    {"Generic TPU 95A", "1d52b2be-a3a2-41de-a8b1-3bcdb5618695"},
};

static const deneb_print_profile_nozzle_choice_t nozzle_choices[] = {
    {"0.25", "0.25 mm"},
    {"0.4", "0.40 mm"},
    {"0.6", "0.60 mm"},
    {"0.8", "0.80 mm"},
};

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

size_t deneb_print_profile_material_choice_count(void)
{
    return sizeof(material_choices) / sizeof(material_choices[0]);
}

const deneb_print_profile_material_choice_t *
deneb_print_profile_material_choice(size_t index)
{
    if (index >= deneb_print_profile_material_choice_count())
        return NULL;
    return &material_choices[index];
}

const char *deneb_print_profile_material_label_from_guid(const char *guid)
{
    size_t i;

    if (!guid || !*guid)
        return NULL;

    for (i = 0; i < deneb_print_profile_material_choice_count(); i++) {
        if (strcmp(material_choices[i].guid, guid) == 0)
            return material_choices[i].label;
    }

    return NULL;
}

int deneb_print_profile_format_set_material_command(const char *guid,
                                                    char *out,
                                                    size_t out_sz)
{
    int n;

    if (!guid || !*guid || !out || out_sz == 0)
        return -1;

    n = snprintf(out, out_sz,
                 "uci -q set ultimaker.option.material_guid='%s'; "
                 "uci -q commit ultimaker",
                 guid);
    if (n < 0 || n >= (int)out_sz)
        return -1;

    return 0;
}

size_t deneb_print_profile_nozzle_choice_count(void)
{
    return sizeof(nozzle_choices) / sizeof(nozzle_choices[0]);
}

const deneb_print_profile_nozzle_choice_t *
deneb_print_profile_nozzle_choice(size_t index)
{
    if (index >= deneb_print_profile_nozzle_choice_count())
        return NULL;
    return &nozzle_choices[index];
}

const char *deneb_print_profile_nozzle_label_from_size(const char *size)
{
    size_t i;

    for (i = 0; i < deneb_print_profile_nozzle_choice_count(); i++) {
        if (size && strcmp(nozzle_choices[i].size, size) == 0)
            return nozzle_choices[i].label;
    }

    return DENEB_PRINT_PROFILE_DEFAULT_NOZZLE_ID;
}

int deneb_print_profile_format_set_nozzle_command(const char *size,
                                                  char *out,
                                                  size_t out_sz)
{
    char normalized[16];
    int n;

    if (!out || out_sz == 0)
        return -1;
    if (deneb_print_profile_normalize_nozzle_size(size, normalized,
                                                  sizeof(normalized)) < 0)
        return -1;

    n = snprintf(out, out_sz,
                 "uci set ultimaker.option.nozzle_size=%s && "
                 "uci commit ultimaker",
                 normalized);
    if (n < 0 || n >= (int)out_sz)
        return -1;

    return 0;
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
