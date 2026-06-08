/* SPDX-License-Identifier: MPL-2.0 */
#include "frame_light.h"

#include <stdio.h>
#include <stdlib.h>

int deneb_frame_light_clamp_brightness(int value)
{
    if (value < 0)
        return 0;
    if (value > 100)
        return 100;
    return value;
}

void deneb_frame_light_state_init(deneb_frame_light_state_t *state)
{
    if (!state)
        return;
    state->enabled = 0;
    state->brightness = DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS;
}

void deneb_frame_light_state_from_values(int legacy_brightness,
                                         int deneb_brightness,
                                         int deneb_enabled,
                                         deneb_frame_light_state_t *state)
{
    int brightness = deneb_brightness;
    int enabled = deneb_enabled;

    if (!state)
        return;

    if (brightness < 0)
        brightness = legacy_brightness >= 0 ? legacy_brightness :
                                             DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS;
    if (enabled < 0)
        enabled = legacy_brightness > 0 ? 1 : 0;

    state->brightness = deneb_frame_light_clamp_brightness(brightness);
    if (state->brightness == 0)
        state->brightness = DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS;
    state->enabled = enabled ? 1 : 0;
}

static int read_uci_int(const char *key, int fallback)
{
    char cmd[128];
    char buf[32];
    FILE *f;

    if (!key || !*key)
        return fallback;

    if (snprintf(cmd, sizeof(cmd), "uci -q get %s 2>/dev/null", key) >=
        (int)sizeof(cmd))
        return fallback;

    f = popen(cmd, "r");
    if (!f)
        return fallback;
    if (!fgets(buf, sizeof(buf), f)) {
        pclose(f);
        return fallback;
    }
    pclose(f);

    return atoi(buf);
}

int deneb_frame_light_read_saved_state(deneb_frame_light_state_t *state)
{
    int legacy;
    int brightness;
    int enabled;

    if (!state)
        return -1;

    legacy = read_uci_int(DENEB_FRAME_LIGHT_LEGACY_UCI_KEY, -1);
    brightness = read_uci_int(DENEB_FRAME_LIGHT_BRIGHTNESS_UCI_KEY,
                              legacy >= 0 ? legacy : -1);
    enabled = read_uci_int(DENEB_FRAME_LIGHT_ENABLED_UCI_KEY,
                           legacy >= 0 ? (legacy > 0 ? 1 : 0) : -1);

    deneb_frame_light_state_from_values(legacy, brightness, enabled, state);
    return 0;
}

int deneb_frame_light_output_brightness(const deneb_frame_light_state_t *state)
{
    if (!state || !state->enabled)
        return 0;
    return deneb_frame_light_clamp_brightness(state->brightness);
}

int deneb_frame_light_format_save_command(
    const deneb_frame_light_state_t *state,
    char *out,
    size_t out_sz)
{
    int n;
    int brightness;
    int legacy_brightness;

    if (!state || !out || out_sz == 0)
        return -1;

    brightness = deneb_frame_light_clamp_brightness(state->brightness);
    if (brightness == 0)
        brightness = DENEB_FRAME_LIGHT_DEFAULT_BRIGHTNESS;
    legacy_brightness = state->enabled ? brightness : 0;

    n = snprintf(out, out_sz,
                 "uci -q set deneb.frame_light=frame_light; "
                 "uci -q set " DENEB_FRAME_LIGHT_ENABLED_UCI_KEY "='%d'; "
                 "uci -q set " DENEB_FRAME_LIGHT_BRIGHTNESS_UCI_KEY "='%d'; "
                 "uci -q set " DENEB_FRAME_LIGHT_LEGACY_UCI_KEY "='%d'; "
                 "uci -q commit deneb; "
                 "uci -q commit ultimaker",
                 state->enabled ? 1 : 0, brightness, legacy_brightness);
    if (n < 0 || n >= (int)out_sz)
        return -1;

    return 0;
}
