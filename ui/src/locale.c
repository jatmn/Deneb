/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Locale/i18n implementation.
 * Loads JSON locale files from /etc/deneb/locales/<lang>.json
 * Uses a simple key-value store (no full JSON parser dependency).
 * Falls back to embedded English defaults for missing keys.
 *
 * Design: Minimal memory. One locale loaded at a time.
 * JSON parsing uses a lightweight state machine, not a library.
 */

#include "locale.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOCALE_DIR      "/etc/deneb/locales"
#define MAX_ENTRIES     256
#define MAX_KEY_LEN     48
#define MAX_VAL_LEN     128
#define MAX_LANG_LEN    16

typedef struct {
    char key[MAX_KEY_LEN];
    char val[MAX_VAL_LEN];
} locale_entry_t;

static locale_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;
static char current_lang[MAX_LANG_LEN] = "en";

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
    {"digital_factory.restart", "Restart Digital Factory"},
    {"digital_factory.restarting", "Digital Factory restarting"},
    {"digital_factory.request_running", "Digital Factory request already running"},
    {"digital_factory.requesting_pin", "Requesting pairing PIN..."},
    {"digital_factory.tap_disconnect", "Tap again to disconnect"},
    {"digital_factory.disconnect_requested", "Disconnect requested"},
    {"digital_factory.pair_show_pin", "Pair / Show PIN"},
    {"digital_factory.disconnect", "Disconnect"},
    {NULL, NULL}
};

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
static int parse_locale_json(const char *json, locale_entry_t *out, int max_entries)
{
    int count = 0;
    const char *p = json;

    while (*p && count < max_entries) {
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
            if (*p == '\\') {
                p += 2; /* skip escaped char */
                continue;
            }
            if (*p == '"') break;
            p++;
        }
        if (!*p) break;
        size_t val_len = p - val_start;
        p++; /* skip closing quote */

        /* Store entry */
        if (key_len < MAX_KEY_LEN) {
            memcpy(out[count].key, key_start, key_len);
            out[count].key[key_len] = '\0';
            if (!decode_json_string(val_start, val_start + val_len,
                                    out[count].val, MAX_VAL_LEN))
                continue;
            count++;
        }
    }

    return count;
}

int locale_init(const char *lang)
{
    entry_count = 0;
    memset(entries, 0, sizeof(entries));

    /* Load embedded English defaults first */
    for (int i = 0; en_defaults[i].key; i++) {
        strncpy(entries[entry_count].key, en_defaults[i].key, MAX_KEY_LEN - 1);
        strncpy(entries[entry_count].val, en_defaults[i].val, MAX_VAL_LEN - 1);
        entry_count++;
        if (entry_count >= MAX_ENTRIES) break;
    }

    /* If not English, try to load from disk */
    if (lang && strcmp(lang, "en") != 0) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s.json", LOCALE_DIR, lang);

        FILE *f = fopen(path, "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);

            if (size > 0 && size < 32768) {
                char *buf = malloc(size + 1);
                if (buf) {
                    fread(buf, 1, size, f);
                    buf[size] = '\0';

                    /* Parse and overlay on defaults */
                    locale_entry_t loaded[MAX_ENTRIES];
                    int loaded_count = parse_locale_json(buf, loaded, MAX_ENTRIES);

                    for (int i = 0; i < loaded_count; i++) {
                        /* Find existing entry and update, or add new */
                        int found = 0;
                        for (int j = 0; j < entry_count; j++) {
                            if (strcmp(entries[j].key, loaded[i].key) == 0) {
                                strncpy(entries[j].val, loaded[i].val, MAX_VAL_LEN - 1);
                                found = 1;
                                break;
                            }
                        }
                        if (!found && entry_count < MAX_ENTRIES) {
                            entries[entry_count] = loaded[i];
                            entry_count++;
                        }
                    }

                    free(buf);
                }
            }
            fclose(f);
        } else {
            fprintf(stderr, "locale: %s not found, using English defaults\n", path);
        }
    }

    strncpy(current_lang, lang ? lang : "en", MAX_LANG_LEN - 1);
    current_lang[MAX_LANG_LEN - 1] = '\0';

    fprintf(stderr, "locale: loaded %d entries for '%s'\n", entry_count, current_lang);
    return 0;
}

const char *locale_get(const char *key)
{
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(entries[i].key, key) == 0)
            return entries[i].val;
    }
    /* Key not found - return key itself as last resort */
    return key;
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
    entry_count = 0;
    memset(entries, 0, sizeof(entries));
}
