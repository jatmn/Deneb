/* SPDX-License-Identifier: MPL-2.0 */
#include "gcode_rewrite.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define DENEB_REWRITE_RETRACT_DISTANCE 6.5f
#define DENEB_REWRITE_COLD_ZONE_DISTANCE 16.5f
#define DENEB_REWRITE_PRIME_DISTANCE 8.5f
#define DENEB_REWRITE_PRIME_SPEED 300.0f
#define DENEB_REWRITE_SLOW_WIPE_SPEED 1200.0f

static int command_starts_with(const char *line, const char *prefix)
{
    size_t len;

    if (!line || !prefix)
        return 0;
    while (*line && isspace((unsigned char)*line))
        line++;
    len = strlen(prefix);
    return strncmp(line, prefix, len) == 0 &&
           (line[len] == '\0' || isspace((unsigned char)line[len]));
}

static int scan_float_param(const char *line, char param, float *out)
{
    char needle = (char)toupper((unsigned char)param);

    if (!line || !out)
        return 0;
    for (const char *p = line; *p; p++) {
        if ((char)toupper((unsigned char)*p) == needle &&
            sscanf(p + 1, "%f", out) == 1)
            return 1;
    }
    return 0;
}

static void rewrite_add(deneb_gcode_rewrite_t *rewrite, const char *command)
{
    if (!rewrite || !command || rewrite->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    snprintf(rewrite->commands[rewrite->count],
             sizeof(rewrite->commands[rewrite->count]), "%s", command);
    rewrite->count++;
}

static void rewrite_addf(deneb_gcode_rewrite_t *rewrite, const char *fmt,
                         float value)
{
    if (!rewrite || rewrite->count >= DENEB_PRINTSVC_MAX_COMMANDS)
        return;
    snprintf(rewrite->commands[rewrite->count],
             sizeof(rewrite->commands[rewrite->count]), fmt, value);
    rewrite->count++;
}

static int rewrite_wait_command(const char *line, deneb_gcode_rewrite_t *rewrite)
{
    float target = 0.0f;

    if (command_starts_with(line, "M190")) {
        if (!scan_float_param(line, 'S', &target))
            target = 0.0f;
        if (target < 0.0f)
            target = 0.0f;
        rewrite_addf(rewrite, "M140 S%.3g", target);
        rewrite->wait_for_bed = 1;
        rewrite->wait_target = target;
        return 1;
    }

    if (command_starts_with(line, "M109")) {
        if (!scan_float_param(line, 'S', &target))
            target = 0.0f;
        if (target < 0.0f)
            target = 0.0f;
        rewrite_addf(rewrite, "M104 S%.3g", target);
        rewrite->wait_for_nozzle = 1;
        rewrite->wait_target = target;
        return 1;
    }

    return 0;
}

static int rewrite_prime_command(const char *line, deneb_gcode_rewrite_t *rewrite)
{
    float strategy = 0.0f;
    float e;
    float wait_speed;

    if (!command_starts_with(line, "G280"))
        return 0;

    (void)scan_float_param(line, 'S', &strategy);
    if ((int)strategy != 0) {
        rewrite_addf(rewrite, "G92 E%.3g", -DENEB_REWRITE_COLD_ZONE_DISTANCE);
        return 1;
    }

    rewrite_add(rewrite, "G0 Z2 F9000");
    rewrite_addf(rewrite, "G10 S%.3g F1500",
                 -DENEB_REWRITE_RETRACT_DISTANCE);
    rewrite_add(rewrite, "G10 S0 F300");
    rewrite_add(rewrite, "G92 E0");
    e = DENEB_REWRITE_PRIME_DISTANCE * 0.9f;
    rewrite_addf(rewrite, "G1 E%.3g F75", e);
    e += DENEB_REWRITE_PRIME_DISTANCE * 0.1f;
    rewrite_addf(rewrite, "G1 E%.3g Z6 F150", e);
    wait_speed = 0.2f * 0.1f * 60.0f;
    rewrite_addf(rewrite, "G0 Z6.1 F%.3g", wait_speed);
    rewrite_add(rewrite, "G92 E0");
    rewrite_addf(rewrite, "G1 E%.3g F600",
                 -DENEB_REWRITE_RETRACT_DISTANCE);
    wait_speed = 0.4f * 0.1f * 60.0f;
    rewrite_addf(rewrite, "G0 Z6.2 F%.3g", wait_speed);
    rewrite_add(rewrite, "G91");
    rewrite_addf(rewrite, "G0 Y10 F%.3g", DENEB_REWRITE_SLOW_WIPE_SPEED);
    rewrite_add(rewrite, "G90");
    rewrite_addf(rewrite, "G0 Z2 F%.3g", DENEB_REWRITE_SLOW_WIPE_SPEED);
    rewrite_add(rewrite, "G0 F9000");
    return 1;
}

void deneb_gcode_rewrite_init(deneb_gcode_rewrite_t *rewrite)
{
    if (rewrite)
        memset(rewrite, 0, sizeof(*rewrite));
}

int deneb_gcode_rewrite_line(const char *line, deneb_gcode_rewrite_t *rewrite)
{
    if (!line || !rewrite)
        return -1;

    deneb_gcode_rewrite_init(rewrite);

    if (command_starts_with(line, "M117"))
        return 0;
    if (rewrite_wait_command(line, rewrite))
        return 1;
    if (rewrite_prime_command(line, rewrite))
        return 1;

    rewrite_add(rewrite, line);
    return 1;
}
