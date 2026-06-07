/* SPDX-License-Identifier: MPL-2.0 */
#include "motion_firmware.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int deneb_motion_firmware_hash_file(const char *path, char out_hex[65])
{
    FILE *f;
    uint8_t buf[512];
    uint8_t digest[32];
    deneb_sha256_t sha;

    if (!path || !out_hex)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;

    deneb_sha256_init(&sha);
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0)
            deneb_sha256_update(&sha, buf, n);
        if (n < sizeof(buf)) {
            if (ferror(f)) {
                fclose(f);
                return -1;
            }
            break;
        }
    }
    fclose(f);

    deneb_sha256_final(&sha, digest);
    deneb_sha256_hex(digest, out_hex);
    return 0;
}

static int read_cache_hash(const char *path, char out_hex[65])
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(out_hex, 1, 64, f);
    fclose(f);
    if (n != 64)
        return -1;
    out_hex[64] = '\0';
    return 0;
}

static int write_cache_hash(const char *path, const char hash[65])
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fprintf(f, "%s\n", hash) == 65;
    fclose(f);
    return ok ? 0 : -1;
}

deneb_motion_fw_result_t deneb_motion_firmware_check_cache(const char *hex_path,
                                                           const char *cache_path,
                                                           char out_hex[65])
{
    char cached[65];

    if (deneb_motion_firmware_hash_file(hex_path, out_hex) != 0)
        return DENEB_MOTION_FW_ERROR;

    if (read_cache_hash(cache_path, cached) == 0 && strcmp(cached, out_hex) == 0)
        return DENEB_MOTION_FW_CACHE_MATCH;

    return DENEB_MOTION_FW_PROGRAM_REQUIRED;
}

static int run_programmer(const char *programmer_path, const char *hex_path)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execl(programmer_path, programmer_path, hex_path, (char *)NULL);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return -1;
    return 0;
}

deneb_motion_fw_result_t deneb_motion_firmware_ensure(const char *hex_path,
                                                      const char *cache_path,
                                                      const char *programmer_path,
                                                      int allow_programming)
{
    char hash[65];
    deneb_motion_fw_result_t check =
        deneb_motion_firmware_check_cache(hex_path, cache_path, hash);

    if (check != DENEB_MOTION_FW_PROGRAM_REQUIRED)
        return check;
    if (!allow_programming)
        return DENEB_MOTION_FW_PROGRAM_REQUIRED;
    if (run_programmer(programmer_path, hex_path) != 0)
        return DENEB_MOTION_FW_ERROR;
    if (write_cache_hash(cache_path, hash) != 0)
        return DENEB_MOTION_FW_ERROR;
    return DENEB_MOTION_FW_PROGRAMMED;
}
