/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Locale/i18n implementation.
 * Loads JSON locale files from /etc/deneb/locales/<lang>.json
 * Keeps embedded English defaults and a compact overlay for one locale.
 */

#include "locale.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef LOCALE_DIR
#define LOCALE_DIR      "/etc/deneb/locales"
#endif
#define MAX_FILE_SIZE   32768
#define MAX_ENTRIES     256
#define MAX_KEY_LEN     48
#define MAX_VAL_LEN     128
#define MAX_LANG_LEN    16

/* Embedded English defaults - matches en.json */
static const struct { const char *key; const char *val; } en_defaults[] = {
    {"menu.status", "Home"},
    {"menu.print", "Print from USB"},
    {"menu.material", "Material"},
    {"menu.maintenance", "Maintenance"},
    {"menu.jog", "Manual Control"},
    {"menu.temp", "Temperature"},
    {"menu.settings", "Settings"},
    {"app.name", "Deneb"},
    {"status.idle", "Idle"},
    {"status.printing", "Printing"},
    {"status.paused", "Paused"},
    {"status.pausing", "Pausing"},
    {"status.heating", "Heating"},
    {"status.cooling", "Cooling"},
    {"status.error", "Error"},
    {"status.complete", "Complete"},
    {"status.preparing", "Preparing"},
    {"status.nozzle", "Nozzle"},
    {"status.bed", "Bed"},
    {"status.no_file", "No file loaded"},
    {"print.no_files", "No printable files found"},
    {"print.start", "Start Print"},
    {"print.stop", "Stop"},
    {"print.pause", "Pause"},
    {"print.resume", "Resume"},
    {"print.cancel", "Cancel"},
    {"print.selected_fmt", "Selected\n%s\n\nReview material/nozzle before starting."},
    {"print.ready", "Ready to start selected file"},
    {"print.select_first", "Select a file first"},
    {"print.starting_fmt", "Starting: %s"},
    {"print.send_failed", "Error: send failed"},
    {"print.pausing", "Pausing..."},
    {"print.resuming", "Resuming..."},
    {"print.resumed", "Resumed"},
    {"print.stopping", "Stopping and cooling"},
    {"print.cancelled", "Cancelled"},
    {"print.select_usb_file", "Select a USB print file"},
    {"print_conflict.title", "Material Mismatch"},
    {"print_conflict.message_fmt", "This job was sliced for %s, but %s is loaded."},
    {"print_conflict.job_fmt", "Job: %s"},
    {"print_conflict.continue", "Continue Anyway"},
    {"print_conflict.cancel", "Cancel"},
    {"print_conflict.continuing", "Continuing print..."},
    {"print_conflict.cancelled", "Print cancelled"},
    {"print_conflict.action_failed", "Unable to update print. Try again."},
    {"material.load", "Load Material"},
    {"material.load_short", "Load"},
    {"material.unload", "Unload Material"},
    {"material.unload_short", "Unload"},
    {"material.change", "Change Material"},
    {"material.load_change", "Load / Change Material"},
    {"material.set", "Set Material"},
    {"material.move", "Move Material"},
    {"material.finish_move", "Finish Material Move"},
    {"material.stop", "Stop"},
    {"material.import", "Import Material"},
    {"material.insert_material", "Insert material and select an option below."},
    {"material.busy", "Printer busy or status unavailable"},
    {"material.cooling", "Cooling nozzle to 0C."},
    {"material.heating", "Heating nozzle to target temperature..."},
    {"material.set_target", "Set target temperature to start heating."},
    {"material.ready_to_move", "Nozzle ready. Load or unload material."},
    {"material.target_too_low", "Set target to at least 170C before moving material."},
    {"material.loading", "Loading material (210C)..."},
    {"material.unloading", "Unloading material (210C)..."},
    {"material.change_started", "Change material: unload started"},
    {"material.moving", "Moving material..."},
    {"material.move_unconfirmed", "Material move could not be confirmed."},
    {"material.stop_unconfirmed", "Material stop could not be confirmed."},
    {"material.current_fmt", "Current material: %s"},
    {"material.current_unknown", "Unknown"},
    {"material.move_finished", "Material move finished"},
    {"material.set_fmt", "Material set: %s"},
    {"material.importing", "Importing material profiles..."},
    {"settings.language", "Language"},
    {"settings.about", "About Deneb"},
    {"settings.network", "Network"},
    {"settings.maintenance", "Maintenance"},
    {"settings.nozzle_size", "Set Nozzle Size"},
    {"settings.nozzle_size_hint", "Select the installed nozzle size."},
    {"settings.digital_factory", "Digital Factory"},
    {"settings.frame_lighting", "Frame Lighting"},
    {"settings.factory_reset", "Factory Reset"},
    {"settings.save_failed", "Save failed"},
    {"language.en", "English"},
    {"language.nl", "Nederlands"},
    {"language.de", "Deutsch"},
    {"language.fr", "Français"},
    {"language.zh_Hans", "简体中文"},
    {"language.en_pirate", "Pirate English"},
    {"language.en_1337", "L33T English"},
    {"language.saved", "Language saved"},
    {"about.description", "Deneb is a community firmware mod for the UltiMaker 2+ Connect. Local-first, responsive, lightweight."},
    {"about.license", "Deneb MPL-2.0; LVGL MIT"},
    {"about.version", "Version"},
    {"about.stock_base_fmt", "Stock base: %s"},
    {"about.printer_id", "Printer ID"},
    {"about.certifications", "Certifications"},
    {"error.title", "Error"},
    {"error.er_code", "ER code"},
    {"error.ok", "OK"},
    {"confirm.title", "Confirm"},
    {"confirm.yes", "Yes"},
    {"confirm.no", "No"},
    {"jog.title", "Manual Control"},
    {"jog.x", "X"},
    {"jog.y", "Y"},
    {"jog.z", "Z"},
    {"jog.home", "Home"},
    {"jog.z_home", "Z Home"},
    {"jog.busy", "Printer busy or status unavailable"},
    {"jog.unconfirmed", "Move could not be confirmed."},
    {"temp.title", "Temperature"},
    {"temp.nozzle", "Nozzle"},
    {"temp.bed", "Bed"},
    {"temp.set", "Set"},
    {"temp.cooldown", "Cooldown"},
    {"cooldown.title", "Cooldown"},
    {"cooldown.message", "Cooling nozzle and bed to safe temperature."},
    {"maintenance.temperature", "Set Nozzle Temperature"},
    {"maintenance.update_firmware", "Update Firmware"},
    {"maintenance.move_buildplate", "Move Build Plate"},
    {"maintenance.level_buildplate", "Level Build Plate"},
    {"maintenance.diagnostics", "Diagnostics"},
    {"update.select", "Select a Deneb .deneb package from USB."},
    {"update.no_files", "No Deneb packages found on USB."},
    {"update.complete", "Update completed. Awaiting reboot."},
    {"maintenance.running", "Maintenance is running. Please wait."},
    {"maintenance.ended", "Maintenance ended. Its result is unavailable in this session."},
    {"update.failed", "Update failed. Check the update log before retrying."},
    {"factory_reset.complete", "Factory reset completed. Awaiting reboot."},
    {"factory_reset.failed", "Factory reset failed. Check the reset log before retrying."},
    {"update.started", "Update started. The printer may reboot."},
    {"update.tap_again_fmt", "Tap %s again to install"},
    {"update.check_releases", "Check Deneb releases"},
    {"update.check_pending", "Git release checks are pending private repo support"},
    {"network.title", "Network Configuration"},
    {"network.hostname_label", "Hostname:"},
    {"network.hostname_fmt", "Hostname: %s"},
    {"network.printer", "Printer"},
    {"network.addresses", "IPv4 addresses"},
    {"network.wifi_label", "WiFi:"},
    {"network.eth_label", "Ethernet:"},
    {"network.import_wifi", "Load WiFi Settings from USB"},
    {"network.disconnect_wifi", "Disconnect WiFi"},
    {"network.import_eth", "Load Ethernet Settings from USB"},
    {"network.reset_eth", "Reset Ethernet to DHCP"},
    {"network.wifi_power", "WiFi"},
    {"network.wifi_turning_on", "Turning WiFi on..."},
    {"network.wifi_turning_off", "Turning WiFi off..."},
    {"network.wifi_enabled", "WiFi on"},
    {"network.wifi_disabled", "WiFi off"},
    {"network.wifi_no_config", "Load WiFi settings first"},
    {"network.wifi_toggle_failed", "WiFi change failed"},
    {"network.importing", "Importing WiFi config from USB..."},
    {"network.importing_eth", "Importing Ethernet config from USB..."},
    {"network.import_running", "Import already in progress"},
    {"network.import_start_failed", "Failed to start import"},
    {"network.no_usb", "No USB drive found"},
    {"network.no_wifi_txt", "wifi.txt not found on USB"},
    {"network.no_eth_txt", "eth.txt not found on USB"},
    {"network.wifi_disconnected", "WiFi disconnected"},
    {"network.wifi_no_ssid", "No SSID in wifi.txt"},
    {"network.wifi_parse_error", "Parse error in wifi.txt"},
    {"network.wifi_configured_fmt", "WiFi configured: %s"},
    {"network.wifi_save_failed", "Failed to save WiFi config"},
    {"network.wifi_restarting", "WiFi configured, restarting..."},
    {"network.wifi_setup_failed", "WiFi setup failed"},
    {"network.wifi_not_configured", "WiFi not configured"},
    {"network.wifi_off_fmt", "WiFi off (%s)"},
    {"network.wifi_connected_fmt", "Connected to %s (%s)"},
    {"network.wifi_connecting_fmt", "Connecting to %s..."},
    {"network.wifi_configured", "WiFi configured"},
    {"network.eth_reset", "Ethernet reset to DHCP"},
    {"network.eth_parse_error", "Parse error in eth.txt"},
    {"network.eth_static_fmt", "Ethernet static: %s"},
    {"network.eth_dhcp_set", "Ethernet set to DHCP"},
    {"network.eth_save_failed", "Failed to save Ethernet config"},
    {"network.eth_restarting", "Ethernet configured, restarting..."},
    {"network.eth_setup_failed", "Ethernet setup failed"},
    {"network.eth_status_static_fmt", "%s (static)"},
    {"network.eth_status_dhcp_fmt", "%s (DHCP)"},
    {"network.not_connected", "not connected"},
    {"diagnostics.message", "Export logs and config to USB for troubleshooting."},
    {"diagnostics.export_logs", "Export Logs to USB"},
    {"diagnostics.export_started", "Log export started"},
    {"diagnostics.air_manager_fmt", "Air Manager: %s"},
    {"diagnostics.build_volume_fmt", "Build volume: %.0f C"},
    {"diagnostics.build_volume_unknown", "Build volume: -- C"},
    {"diagnostics.fan_fmt", "Air Manager fan: %s"},
    {"diagnostics.present", "present"},
    {"diagnostics.not_present", "not present"},
    {"diagnostics.unknown", "unknown"},
    {"diagnostics.on", "on"},
    {"diagnostics.off", "off"},
    {"diagnostics.usb_required", "Insert a writable USB drive first"},
    {"level.message", "Run each leveling step in order, then finish."},
    {"level.busy", "Printer busy or status unavailable"},
    {"level.moving", "Moving to leveling point..."},
    {"level.ready_next", "Ready for the next leveling step."},
    {"level.done", "Build plate leveling complete."},
    {"level.cancelled", "Build plate leveling cancelled."},
    {"level.cancelling", "Cancelling after current move..."},
    {"level.cancel_unconfirmed", "Cancel could not be confirmed. Tap Cancel to retry."},
    {"level.failed", "Build plate leveling move failed."},
    {"level.step1", "Level Step 1"},
    {"level.step2", "Level Step 2"},
    {"level.step3", "Level Step 3"},
    {"level.step4", "Level Step 4"},
    {"level.finish", "Finish Leveling"},
    {"frame_lighting.on", "Frame Light On"},
    {"frame_lighting.off", "Frame Light Off"},
    {"frame_lighting.brightness_fmt", "Brightness: %d%%"},
    {"frame_lighting.status_fmt", "Saved: %s at %d%%"},
    {"factory_reset.warning", "Factory reset erases local printer settings and reboots."},
    {"factory_reset.started", "Factory reset started"},
    {"factory_reset.tap_again", "Tap again to erase settings"},
    {"digital_factory.title", "Digital Factory"},
    {"digital_factory.cluster", "Cluster"},
    {"digital_factory.service", "Service"},
    {"digital_factory.request_running", "Digital Factory request already running"},
    {"digital_factory.requesting_pin", "Requesting pairing PIN..."},
    {"digital_factory.tap_disconnect", "Tap again to disconnect"},
    {"digital_factory.disconnect_requested", "Disconnect requested"},
    {"digital_factory.pair_show_pin", "Connect"},
    {"digital_factory.disconnect", "Disconnect"},
    {"digital_factory.status_connected", "Connected"},
    {"digital_factory.status_disconnected", "Disconnected"},
    {"digital_factory.status_reconnecting", "Reconnecting"},
    {"digital_factory.status_pin_fmt", "PIN: %s"},
    {"digital_factory.status_cloud_signin_required", "Cloud sign-in required"},
    {"digital_factory.status_disconnect_failed", "Disconnect failed"},
};

enum { EN_DEFAULT_COUNT = sizeof(en_defaults) / sizeof(en_defaults[0]) };

typedef struct {
    int extra_count;
    uint16_t value_offsets[MAX_ENTRIES]; /* 0 means no override, others are one-based. */
    uint16_t extra_keys[MAX_ENTRIES - EN_DEFAULT_COUNT];
    char strings[];
} locale_catalog_t;

static locale_catalog_t *catalog;
static bool initialized;
static char current_lang[MAX_LANG_LEN] = "en";

static int find_default(const char *key)
{
    for (int i = 0; i < EN_DEFAULT_COUNT; i++) {
        if (strcmp(en_defaults[i].key, key) == 0)
            return i;
    }
    return -1;
}

static int find_extra(const locale_catalog_t *loaded, const char *key)
{
    for (int i = 0; i < loaded->extra_count; i++) {
        if (strcmp(loaded->strings + loaded->extra_keys[i], key) == 0)
            return i;
    }
    return -1;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int append_utf8(char *out, size_t *out_len, size_t max_len, unsigned int cp)
{
    if (cp <= 0x7f) {
        if (*out_len + 1 >= max_len)
            return 0;
        out[(*out_len)++] = (char)cp;
    } else if (cp <= 0x7ff) {
        if (*out_len + 2 >= max_len)
            return 0;
        out[(*out_len)++] = (char)(0xc0 | (cp >> 6));
        out[(*out_len)++] = (char)(0x80 | (cp & 0x3f));
    } else {
        if (*out_len + 3 >= max_len)
            return 0;
        out[(*out_len)++] = (char)(0xe0 | (cp >> 12));
        out[(*out_len)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[(*out_len)++] = (char)(0x80 | (cp & 0x3f));
    }

    return 1;
}

static int decode_json_string(const char *start, const char *end,
                              char *out, size_t max_len)
{
    size_t out_len = 0;
    const char *p = start;

    while (p < end) {
        if (*p != '\\') {
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = *p++;
            continue;
        }

        p++;
        if (p >= end)
            return 0;

        switch (*p) {
        case '"':
        case '\\':
        case '/':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = *p++;
            break;
        case 'b':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = '\b';
            p++;
            break;
        case 'f':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = '\f';
            p++;
            break;
        case 'n':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = '\n';
            p++;
            break;
        case 'r':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = '\r';
            p++;
            break;
        case 't':
            if (out_len + 1 >= max_len)
                return 0;
            out[out_len++] = '\t';
            p++;
            break;
        case 'u': {
            unsigned int cp = 0;
            p++;
            if (end - p < 4)
                return 0;
            for (int i = 0; i < 4; i++) {
                int value = hex_value(p[i]);
                if (value < 0)
                    return 0;
                cp = (cp << 4) | (unsigned int)value;
            }
            p += 4;
            if (!append_utf8(out, &out_len, max_len, cp))
                return 0;
            break;
        }
        default:
            return 0;
        }
    }

    out[out_len] = '\0';
    return 1;
}

/**
 * Simple JSON string value parser.
 * Handles: "key": "value" pairs, with escaped quotes.
 * Does NOT handle nested objects, arrays, or non-string values.
 * This is intentional: locale files are flat key-value maps.
 */
static size_t parse_locale_json(locale_catalog_t *loaded)
{
    int parsed = 0;
    const char *p = loaded->strings;
    char *packed = loaded->strings;

    while (*p && parsed < MAX_ENTRIES) {
        /* Find next key */
        p = strchr(p, '"');
        if (!p) break;
        p++; /* skip opening quote */

        /* Read key */
        const char *key_start = p;
        while (*p && *p != '"') p++;
        if (!*p) break;
        size_t key_len = p - key_start;
        p++; /* skip closing quote */

        /* Find colon */
        while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
        if (*p != '"') continue; /* expect string value */
        p++; /* skip opening quote */

        /* Read value */
        const char *val_start = p;
        while (*p) {
            if (*p == '\\' && p[1]) {
                p += 2; /* skip escaped char */
                continue;
            }
            if (*p == '"') break;
            p++;
        }
        if (!*p) break;
        size_t val_len = p - val_start;
        p++; /* skip closing quote */

        char key[MAX_KEY_LEN];
        char value[MAX_VAL_LEN];
        if (key_len >= sizeof(key) ||
            !decode_json_string(val_start, val_start + val_len, value, sizeof(value)))
            continue;
        parsed++;
        memcpy(key, key_start, key_len);
        key[key_len] = '\0';

        int slot = find_default(key);
        if (slot < 0) {
            int extra = find_extra(loaded, key);
            if (extra < 0) {
                if (loaded->extra_count == MAX_ENTRIES - EN_DEFAULT_COUNT)
                    continue;
                extra = loaded->extra_count++;
                loaded->extra_keys[extra] = packed - loaded->strings;
                memcpy(packed, key, key_len + 1);
                packed += key_len + 1;
            }
            slot = EN_DEFAULT_COUNT + extra;
        } else if (strcmp(value, en_defaults[slot].val) == 0) {
            loaded->value_offsets[slot] = 0;
            continue;
        }

        /* Each packed pair is shorter than its JSON tokens already consumed. */
        loaded->value_offsets[slot] = packed - loaded->strings + 1;
        size_t value_size = strlen(value) + 1;
        memcpy(packed, value, value_size);
        packed += value_size;
    }

    return packed - loaded->strings;
}

static locale_catalog_t *load_catalog(const char *filename)
{
    int dir = open(LOCALE_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int fd = -1;
    if (dir >= 0) {
        fd = openat(dir, filename, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        close(dir);
    }
    FILE *f = fd >= 0 ? fdopen(fd, "r") : NULL;
    if (!f) {
        if (fd >= 0)
            close(fd);
        fprintf(stderr, "locale: %s/%s not found, using English defaults\n",
                LOCALE_DIR, filename);
        return NULL;
    }

    locale_catalog_t *loaded = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long size = ftell(f);
        if (size > 0 && size < MAX_FILE_SIZE && fseek(f, 0, SEEK_SET) == 0) {
            loaded = malloc(sizeof(*loaded) + size + 1);
            if (loaded && fread(loaded->strings, 1, size, f) != (size_t)size) {
                free(loaded);
                loaded = NULL;
            }
            if (loaded)
                loaded->strings[size] = '\0';
        }
    }
    fclose(f);
    if (!loaded)
        return NULL;

    memset(loaded, 0, offsetof(locale_catalog_t, strings));
    size_t packed = parse_locale_json(loaded);
    if (packed == 0) {
        free(loaded);
        return NULL;
    }
    locale_catalog_t *compact = realloc(loaded, sizeof(*loaded) + packed);
    return compact ? compact : loaded;
}

int locale_init(const char *lang)
{
    /* Keep the full path limit, including the directory and separator. */
    char filename[128 - sizeof(LOCALE_DIR)] = "";
    if (lang && *lang && strcmp(lang, "en") != 0 &&
        !strstr(lang, "..") && !strchr(lang, '/') && !strchr(lang, '\\')) {
        int length = snprintf(filename, sizeof(filename), "%s.json", lang);
        if (length < 0 || (size_t)length >= sizeof(filename))
            filename[0] = '\0';
    }

    if (lang != current_lang)
        snprintf(current_lang, sizeof(current_lang), "%s", lang ? lang : "en");

    free(catalog);
    catalog = filename[0] ? load_catalog(filename) : NULL;
    initialized = true;
    fprintf(stderr, "locale: loaded %d entries for '%s'\n",
            EN_DEFAULT_COUNT + (catalog ? catalog->extra_count : 0), current_lang);
    return 0;
}

const char *locale_get(const char *key)
{
    if (!initialized)
        return key;

    int slot = find_default(key);
    if (catalog) {
        if (slot < 0) {
            int extra = find_extra(catalog, key);
            if (extra < 0)
                return key;
            slot = EN_DEFAULT_COUNT + extra;
        }
        uint16_t offset = catalog->value_offsets[slot];
        if (offset)
            return catalog->strings + offset - 1;
    }
    return slot >= 0 ? en_defaults[slot].val : key;
}

static void locale_append_text(char *out, size_t out_size, size_t *used,
                               const char *text)
{
    const char *value = text ? text : "";

    while (*value && *used + 1 < out_size)
        out[(*used)++] = *value++;
}

static int locale_format_string_values(char *out, size_t out_size,
                                       const char *key,
                                       const char *const *values,
                                       size_t value_count)
{
    const char *format;
    size_t used = 0;
    size_t value_index = 0;
    bool placeholder_mismatch = false;

    if (!out || out_size == 0 || !key || (!values && value_count != 0))
        return -1;

    out[0] = '\0';
    format = locale_get(key);
    while (*format && used + 1 < out_size) {
        if (format[0] == '%' && format[1] == 's') {
            if (value_index < value_count)
                locale_append_text(out, out_size, &used,
                                   values[value_index++]);
            else {
                locale_append_text(out, out_size, &used, "%s");
                placeholder_mismatch = true;
            }
            format += 2;
            continue;
        }
        out[used++] = *format++;
    }
    out[used] = '\0';

    return !placeholder_mismatch && value_index == value_count ? 0 : -1;
}

int locale_format_s(char *out, size_t out_size, const char *key,
                    const char *value)
{
    const char *values[] = {value};

    return locale_format_string_values(out, out_size, key, values, 1);
}

int locale_format_ss(char *out, size_t out_size, const char *key,
                     const char *first, const char *second)
{
    const char *values[] = {first, second};

    return locale_format_string_values(out, out_size, key, values, 2);
}

int locale_set(const char *lang)
{
    return locale_init(lang);
}

const char *locale_current(void)
{
    return current_lang;
}

void locale_deinit(void)
{
    free(catalog);
    catalog = NULL;
    initialized = false;
}
