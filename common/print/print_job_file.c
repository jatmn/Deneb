/* SPDX-License-Identifier: MPL-2.0 */
#define _GNU_SOURCE
#include "print_job_file.h"

#include "lodepng.h"
#include "print_state_rules.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DENEB_THUMB_TARGET_SIZE 116
#define DENEB_THUMB_RGB565_BYTES \
    (DENEB_THUMB_TARGET_SIZE * DENEB_THUMB_TARGET_SIZE * 2)
#define DENEB_UFP_THUMB_MAX_CANDIDATES 16
#define DENEB_UFP_MEMBER_MAX 256
#define DENEB_UFP_THUMB_MAX_BYTES (2U * 1024U * 1024U)

typedef struct {
    char name[DENEB_UFP_MEMBER_MAX];
    uint16_t method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
} deneb_ufp_thumb_candidate_t;

void deneb_print_job_file_metadata_init(deneb_print_job_file_metadata_t *meta)
{
    if (!meta)
        return;
    memset(meta, 0, sizeof(*meta));
}

void deneb_print_job_start_plan_init(deneb_print_job_start_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    plan->source = DENEB_PRINT_DEFAULT_JOB_SOURCE;
    plan->uuid = DENEB_PRINT_DEFAULT_JOB_UUID;
    plan->cloud_job_id = "";
}

void deneb_print_job_upload_storage_plan_init(
    deneb_print_job_upload_storage_plan_t *plan)
{
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
}

int deneb_print_job_start_plan_prepare(const char *path, const char *source,
                                       const char *uuid,
                                       const char *cloud_job_id,
                                       float bed_target,
                                       float nozzle_target,
                                       deneb_print_job_start_plan_t *plan)
{
    if (!plan || !path || !path[0]) {
        errno = EINVAL;
        return -1;
    }

    deneb_print_job_start_plan_init(plan);
    plan->path = path;
    plan->source = deneb_print_job_source_or_default(source);
    plan->uuid = deneb_print_job_uuid_or_default(uuid);
    plan->cloud_job_id = cloud_job_id ? cloud_job_id : "";
    plan->bed_target = bed_target;
    plan->nozzle_target = nozzle_target;
    return 0;
}

int deneb_print_job_start_plan_file(const char *path, const char *source,
                                    deneb_print_job_start_plan_t *plan)
{
    return deneb_print_job_start_plan_prepare(path, source, NULL, NULL, 0.0f,
                                             0.0f, plan);
}

int deneb_print_job_file_metadata_extract_value(const char *buf,
                                                const char *key,
                                                char *out,
                                                size_t out_sz)
{
    const char *p;
    size_t i = 0;

    if (!buf || !key || !out || out_sz == 0)
        return -1;

    p = strstr(buf, key);
    if (!p)
        return -1;
    out[0] = '\0';
    p += strlen(key);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':' || *p == '=' ||
                  *p == '"' || *p == '\''))
        p++;
    while (*p && *p != '"' && *p != '\'' && *p != ',' && *p != ';' &&
           *p != '\r' && *p != '\n' && !isspace((unsigned char)*p) &&
           i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0 ? 0 : -1;
}

static int metadata_extract_positive_int(const char *buf,
                                         const char *key,
                                         int *out)
{
    const char *p;
    size_t key_len;

    if (!buf || !key || !out)
        return -1;

    key_len = strlen(key);
    for (p = strstr(buf, key); p; p = strstr(p + 1, key)) {
        const char *v = p + key_len;
        long value = 0;
        int digits = 0;

        if (*v && *v != ' ' && *v != '\t' && *v != ':' && *v != '=' &&
            *v != '"' && *v != '\'')
            continue;
        while (*v && (*v == ' ' || *v == '\t' || *v == ':' ||
                      *v == '=' || *v == '"' || *v == '\''))
            v++;
        while (*v >= '0' && *v <= '9') {
            value = value * 10 + (*v - '0');
            if (value > 2147483647L)
                return -1;
            v++;
            digits++;
        }
        if (digits > 0) {
            *out = (int)value;
            return 0;
        }
    }
    return -1;
}

static int metadata_extract_float(const char *buf, const char *key, float *out)
{
    const char *p;
    size_t key_len;

    if (!buf || !key || !out)
        return -1;

    key_len = strlen(key);
    for (p = strstr(buf, key); p; p = strstr(p + 1, key)) {
        const char *v = p + key_len;
        float value;

        if (*v && *v != ' ' && *v != '\t' && *v != ':' && *v != '=' &&
            *v != '"' && *v != '\'' && *v != '-' && *v != '+')
            continue;
        while (*v && (*v == ' ' || *v == '\t' || *v == ':' ||
                      *v == '=' || *v == '"' || *v == '\''))
            v++;
        if (sscanf(v, "%f", &value) == 1) {
            *out = value;
            return 0;
        }
        return 1;
    }
    return -1;
}

int deneb_print_job_file_metadata_load(const char *path,
                                       deneb_print_job_file_metadata_t *meta)
{
    char buf[131073];
    FILE *f;
    size_t n;
    int found = 0;

    if (!path || !meta)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;

    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    if (deneb_print_job_file_metadata_extract_value(
            buf, "material_guid", meta->material_guid,
            sizeof(meta->material_guid)) == 0)
        found = 1;
    if (!meta->material_guid[0] &&
        deneb_print_job_file_metadata_extract_value(
            buf, "EXTRUDER_TRAIN.0.MATERIAL.GUID", meta->material_guid,
            sizeof(meta->material_guid)) == 0)
        found = 1;
    if (deneb_print_job_file_metadata_extract_value(
            buf, "nozzle_size", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;
    if (deneb_print_job_file_metadata_extract_value(
            buf, "print_core_id", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;
    if (!meta->nozzle_size[0] &&
        deneb_print_job_file_metadata_extract_value(
            buf, "EXTRUDER_TRAIN.0.NOZZLE.DIAMETER", meta->nozzle_size,
            sizeof(meta->nozzle_size)) == 0)
        found = 1;
    if (metadata_extract_positive_int(buf, "PRINT.TIME",
                                      &meta->print_time_seconds) == 0)
        found = 1;
    else if (metadata_extract_positive_int(buf, "TIME",
                                           &meta->print_time_seconds) == 0)
        found = 1;

    {
        unsigned bounds_field_mask = 0;
        int bounds_invalid = 0;
        float min_x = 0.0f, min_y = 0.0f, min_z = 0.0f;
        float max_x = 0.0f, max_y = 0.0f, max_z = 0.0f;
        int rc;

        rc = metadata_extract_float(buf, "PRINT.MIN.X", &min_x);
        if (rc == 0)
            bounds_field_mask |= (1u << 0);
        else if (rc > 0)
            bounds_invalid = 1;
        rc = metadata_extract_float(buf, "PRINT.MIN.Y", &min_y);
        if (rc == 0)
            bounds_field_mask |= (1u << 1);
        else if (rc > 0)
            bounds_invalid = 1;
        rc = metadata_extract_float(buf, "PRINT.MIN.Z", &min_z);
        if (rc == 0)
            bounds_field_mask |= (1u << 2);
        else if (rc > 0)
            bounds_invalid = 1;
        rc = metadata_extract_float(buf, "PRINT.MAX.X", &max_x);
        if (rc == 0)
            bounds_field_mask |= (1u << 3);
        else if (rc > 0)
            bounds_invalid = 1;
        rc = metadata_extract_float(buf, "PRINT.MAX.Y", &max_y);
        if (rc == 0)
            bounds_field_mask |= (1u << 4);
        else if (rc > 0)
            bounds_invalid = 1;
        rc = metadata_extract_float(buf, "PRINT.MAX.Z", &max_z);
        if (rc == 0)
            bounds_field_mask |= (1u << 5);
        else if (rc > 0)
            bounds_invalid = 1;
        if (bounds_field_mask != 0 || bounds_invalid) {
            meta->has_bounds = 1;
            meta->bounds_invalid = bounds_invalid;
            meta->bounds_field_mask = bounds_field_mask;
            meta->min_x = min_x;
            meta->min_y = min_y;
            meta->min_z = min_z;
            meta->max_x = max_x;
            meta->max_y = max_y;
            meta->max_z = max_z;
            found = 1;
        }
    }

    return found ? 0 : -1;
}

int deneb_print_job_file_has_extension(const char *name, const char *extension)
{
    size_t name_len;
    size_t extension_len;

    if (!name || !extension)
        return 0;
    name_len = strlen(name);
    extension_len = strlen(extension);
    return name_len >= extension_len &&
           strcasecmp(name + name_len - extension_len, extension) == 0;
}

int deneb_print_job_file_replace_extension(const char *name,
                                           const char *extension,
                                           char *out,
                                           size_t out_sz)
{
    const char *dot;
    size_t base_len;

    if (!name || !*name || !extension || !out || out_sz == 0) {
        errno = EINVAL;
        return -1;
    }

    dot = strrchr(name, '.');
    base_len = dot ? (size_t)(dot - name) : strlen(name);
    if (base_len == 0 || base_len + strlen(extension) + 1 > out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }

    memcpy(out, name, base_len);
    out[base_len] = '\0';
    strncat(out, extension, out_sz - strlen(out) - 1);
    return 0;
}

static int extract_ufp_member(const char *ufp_path, const char *member,
                              const char *gcode_path)
{
    int out_fd;
    int status = 0;
    pid_t pid;
    struct stat st;

    out_fd = open(gcode_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(out_fd);
        return -1;
    }
    if (pid == 0) {
        if (dup2(out_fd, STDOUT_FILENO) < 0)
            _exit(126);
        close(out_fd);
        execlp("unzip", "unzip", "-p", ufp_path, member, (char *)NULL);
        _exit(127);
    }
    close(out_fd);

    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (stat(gcode_path, &st) == 0 && st.st_size > 0)
        return 0;
    unlink(gcode_path);
    if (!WIFEXITED(status))
        return -2;
    return WEXITSTATUS(status) != 0 ? WEXITSTATUS(status) : -3;
}

static int contains_ci(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle)
        return 0;

    needle_len = strlen(needle);
    if (needle_len == 0)
        return 1;

    for (; *haystack; haystack++) {
        size_t i;

        for (i = 0; i < needle_len; i++) {
            unsigned char a = (unsigned char)haystack[i];
            unsigned char b = (unsigned char)needle[i];

            if (!a || tolower(a) != tolower(b))
                break;
        }
        if (i == needle_len)
            return 1;
    }
    return 0;
}

static uint16_t read_le16(const unsigned char *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int read_png_dimensions_data(const unsigned char *data, size_t size,
                                    int *width, int *height)
{
    static const unsigned char png_sig[8] =
        {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    uint32_t png_width;
    uint32_t png_height;

    if (!data || size < 24 || !width || !height)
        return -1;

    if (memcmp(data, png_sig, sizeof(png_sig)) != 0 ||
        memcmp(data + 12, "IHDR", 4) != 0)
        return -1;

    png_width = read_be32(data + 16);
    png_height = read_be32(data + 20);
    if (png_width == 0 || png_height == 0 ||
        png_width > 2147483647U || png_height > 2147483647U)
        return -1;

    *width = (int)png_width;
    *height = (int)png_height;
    return 0;
}

static int thumbnail_score(int width, int height)
{
    int dw = width - DENEB_THUMB_TARGET_SIZE;
    int dh = height - DENEB_THUMB_TARGET_SIZE;

    if (dw < 0)
        dw = -dw;
    if (dh < 0)
        dh = -dh;
    return dw + dh;
}

static int inflate_zip_deflate(const unsigned char *compressed,
                               size_t compressed_size,
                               size_t uncompressed_size,
                               unsigned char **out,
                               size_t *out_size)
{
    LodePNGDecompressSettings settings;
    unsigned char *buf = NULL;
    size_t inflated_size = 0;
    unsigned error;

    if (!compressed || compressed_size == 0 || uncompressed_size == 0 ||
        uncompressed_size > DENEB_UFP_THUMB_MAX_BYTES || !out || !out_size)
        return -1;

    lodepng_decompress_settings_init(&settings);
    settings.max_output_size = uncompressed_size;

    error = lodepng_inflate(&buf, &inflated_size, compressed,
                            compressed_size, &settings);
    if (error || inflated_size != uncompressed_size) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_size = inflated_size;
    return 0;
}

static int read_file_at(FILE *f, uint32_t offset, void *buf, size_t size)
{
    if (fseek(f, (long)offset, SEEK_SET) < 0)
        return -1;
    return fread(buf, 1, size, f) == size ? 0 : -1;
}

static int find_zip_central_directory(FILE *f, uint32_t *cd_offset)
{
    long file_size;
    size_t tail_size;
    unsigned char *tail;
    size_t i;
    int found = -1;

    if (!f || !cd_offset || fseek(f, 0, SEEK_END) < 0)
        return -1;

    file_size = ftell(f);
    if (file_size < 22)
        return -1;

    tail_size = (size_t)(file_size < 66000 ? file_size : 66000);
    tail = malloc(tail_size);
    if (!tail)
        return -1;

    if (fseek(f, file_size - (long)tail_size, SEEK_SET) < 0 ||
        fread(tail, 1, tail_size, f) != tail_size) {
        free(tail);
        return -1;
    }

    for (i = tail_size - 22;; i--) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4b &&
            tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            *cd_offset = read_le32(tail + i + 16);
            found = 0;
            break;
        }
        if (i == 0)
            break;
    }

    free(tail);
    return found;
}

static size_t list_ufp_thumbnail_members(
    const char *ufp_path,
    deneb_ufp_thumb_candidate_t candidates[],
    size_t max_candidates)
{
    FILE *f;
    uint32_t offset;
    size_t count = 0;

    if (!ufp_path || !candidates || max_candidates == 0)
        return 0;

    f = fopen(ufp_path, "rb");
    if (!f)
        return 0;

    if (find_zip_central_directory(f, &offset) < 0) {
        fclose(f);
        return 0;
    }

    while (count < max_candidates) {
        unsigned char hdr[46];
        uint16_t name_len;
        uint16_t extra_len;
        uint16_t comment_len;
        char name[DENEB_UFP_MEMBER_MAX];
        uint16_t method;
        uint32_t compressed_size;
        uint32_t uncompressed_size;
        uint32_t local_header_offset;
        long next_offset;

        if (read_file_at(f, offset, hdr, sizeof(hdr)) < 0 ||
            read_le32(hdr) != 0x02014b50U)
            break;

        method = read_le16(hdr + 10);
        compressed_size = read_le32(hdr + 20);
        uncompressed_size = read_le32(hdr + 24);
        name_len = read_le16(hdr + 28);
        extra_len = read_le16(hdr + 30);
        comment_len = read_le16(hdr + 32);
        local_header_offset = read_le32(hdr + 42);
        next_offset = (long)offset + 46L + name_len + extra_len +
                      comment_len;

        if (name_len > 0 && name_len < sizeof(name) &&
            read_file_at(f, offset + 46U, name, name_len) == 0) {
            name[name_len] = '\0';

            if ((method == 0 || method == 8) &&
                compressed_size > 0 &&
                uncompressed_size > 0 &&
                uncompressed_size <= DENEB_UFP_THUMB_MAX_BYTES &&
                deneb_print_job_file_has_extension(name, ".png") &&
                contains_ci(name, "thumbnail")) {
                snprintf(candidates[count].name,
                         sizeof(candidates[count].name), "%s", name);
                candidates[count].method = method;
                candidates[count].compressed_size = compressed_size;
                candidates[count].uncompressed_size = uncompressed_size;
                candidates[count].local_header_offset = local_header_offset;
                count++;
            }
        }

        if (next_offset <= (long)offset)
            break;
        offset = (uint32_t)next_offset;
    }

    fclose(f);
    return count;
}

static int extract_zip_candidate(const char *ufp_path,
                                 const deneb_ufp_thumb_candidate_t *candidate,
                                 unsigned char **out,
                                 size_t *out_size)
{
    FILE *f;
    unsigned char local[30];
    uint16_t name_len;
    uint16_t extra_len;
    uint32_t data_offset;
    unsigned char *compressed;
    unsigned char *inflated = NULL;
    size_t inflated_size = 0;
    int rc = -1;

    if (!ufp_path || !candidate || !out || !out_size)
        return -1;

    *out = NULL;
    *out_size = 0;
    f = fopen(ufp_path, "rb");
    if (!f)
        return -1;

    if (read_file_at(f, candidate->local_header_offset, local,
                     sizeof(local)) < 0 ||
        read_le32(local) != 0x04034b50U)
        goto done;

    name_len = read_le16(local + 26);
    extra_len = read_le16(local + 28);
    data_offset = candidate->local_header_offset + 30U + name_len +
                  extra_len;
    compressed = malloc(candidate->compressed_size);
    if (!compressed)
        goto done;

    if (read_file_at(f, data_offset, compressed,
                     candidate->compressed_size) < 0) {
        free(compressed);
        goto done;
    }

    if (candidate->method == 0) {
        inflated = compressed;
        inflated_size = candidate->compressed_size;
        compressed = NULL;
    } else {
        if (inflate_zip_deflate(compressed, candidate->compressed_size,
                                candidate->uncompressed_size, &inflated,
                                &inflated_size) != 0) {
            free(compressed);
            goto done;
        }
        free(compressed);
    }

    *out = inflated;
    *out_size = inflated_size;
    inflated = NULL;
    rc = 0;

done:
    free(inflated);
    fclose(f);
    return rc;
}

static int write_all_bytes(const char *path, const unsigned char *data,
                           size_t size)
{
    FILE *f;
    size_t written;

    if (!path || !data)
        return -1;

    f = fopen(path, "wb");
    if (!f)
        return -1;
    written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size ? 0 : -1;
}

static unsigned short rgb_to_rgb565(unsigned char r, unsigned char g,
                                    unsigned char b)
{
    return (unsigned short)(((unsigned short)(r & 0xf8) << 8) |
                            ((unsigned short)(g & 0xfc) << 3) |
                            ((unsigned short)b >> 3));
}

static void write_rgb565_pixel(unsigned char *buf, size_t pixel,
                               unsigned short color)
{
    buf[pixel * 2U] = (unsigned char)(color & 0xffU);
    buf[pixel * 2U + 1U] = (unsigned char)((color >> 8) & 0xffU);
}

static int paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

static int append_png_chunk(unsigned char **buf, size_t *size, size_t *cap,
                            const unsigned char *data, size_t data_size)
{
    unsigned char *grown;
    size_t next_cap;

    if (!buf || !size || !cap || !data)
        return -1;
    if (data_size > DENEB_UFP_THUMB_MAX_BYTES ||
        *size > DENEB_UFP_THUMB_MAX_BYTES - data_size)
        return -1;
    if (*size + data_size > *cap) {
        next_cap = *cap ? *cap : 4096U;
        while (next_cap < *size + data_size) {
            if (next_cap > DENEB_UFP_THUMB_MAX_BYTES / 2U)
                next_cap = DENEB_UFP_THUMB_MAX_BYTES;
            else
                next_cap *= 2U;
            if (next_cap < *size + data_size &&
                next_cap == DENEB_UFP_THUMB_MAX_BYTES)
                return -1;
        }
        grown = realloc(*buf, next_cap);
        if (!grown)
            return -1;
        *buf = grown;
        *cap = next_cap;
    }
    memcpy(*buf + *size, data, data_size);
    *size += data_size;
    return 0;
}

static int decode_png_rgba32(const unsigned char *png_data, size_t png_size,
                             unsigned char **out_rgba, unsigned *out_width,
                             unsigned *out_height)
{
    static const unsigned char png_sig[8] =
        {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    unsigned char *idat = NULL;
    unsigned char *raw = NULL;
    unsigned char *rgba = NULL;
    unsigned char *prev = NULL;
    unsigned char *cur = NULL;
    size_t idat_size = 0;
    size_t idat_cap = 0;
    size_t raw_size = 0;
    size_t pos = 8;
    size_t stride;
    size_t expected_raw_size;
    unsigned width = 0;
    unsigned height = 0;
    unsigned color_type = 0;
    unsigned bpp = 0;
    int saw_ihdr = 0;
    int saw_iend = 0;
    int rc = -1;

    if (!png_data || png_size < 33 || !out_rgba || !out_width ||
        !out_height || memcmp(png_data, png_sig, sizeof(png_sig)) != 0)
        return -1;

    while (pos + 12U <= png_size) {
        uint32_t chunk_len = read_be32(png_data + pos);
        const unsigned char *type = png_data + pos + 4U;
        const unsigned char *chunk = png_data + pos + 8U;

        pos += 8U;
        if (chunk_len > png_size - pos || chunk_len > DENEB_UFP_THUMB_MAX_BYTES)
            goto done;

        if (memcmp(type, "IHDR", 4) == 0) {
            if (chunk_len != 13U)
                goto done;
            width = read_be32(chunk);
            height = read_be32(chunk + 4);
            color_type = chunk[9];
            if (width == 0 || height == 0 || width > 4096U ||
                height > 4096U || chunk[8] != 8U ||
                (color_type != 2U && color_type != 6U) || chunk[10] != 0U ||
                chunk[11] != 0U || chunk[12] != 0U)
                goto done;
            bpp = color_type == 6U ? 4U : 3U;
            saw_ihdr = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (append_png_chunk(&idat, &idat_size, &idat_cap, chunk,
                                 chunk_len) < 0)
                goto done;
        } else if (memcmp(type, "IEND", 4) == 0) {
            saw_iend = 1;
            break;
        }

        pos += (size_t)chunk_len + 4U;
    }

    if (!saw_ihdr || !saw_iend || idat_size == 0)
        goto done;

    stride = (size_t)width * bpp;
    if (height > SIZE_MAX / (stride + 1U))
        goto done;
    expected_raw_size = (stride + 1U) * height;

    {
        LodePNGDecompressSettings settings;

        lodepng_decompress_settings_init(&settings);
        settings.max_output_size = expected_raw_size;
        if (lodepng_zlib_decompress(&raw, &raw_size, idat, idat_size,
                                    &settings) != 0 ||
            raw_size != expected_raw_size)
            goto done;
    }

    if ((size_t)width > SIZE_MAX / height ||
        (size_t)width * height > SIZE_MAX / 4U)
        goto done;
    rgba = malloc((size_t)width * height * 4U);
    prev = calloc(1, stride);
    cur = malloc(stride);
    if (!rgba || !prev || !cur)
        goto done;

    pos = 0;
    for (unsigned y = 0; y < height; y++) {
        unsigned filter = raw[pos++];
        const unsigned char *scanline = raw + pos;

        if (filter > 4U)
            goto done;
        for (size_t i = 0; i < stride; i++) {
            int x = scanline[i];
            int left = i >= bpp ? cur[i - bpp] : 0;
            int up = prev[i];
            int upper_left = i >= bpp ? prev[i - bpp] : 0;

            if (filter == 1U)
                x += left;
            else if (filter == 2U)
                x += up;
            else if (filter == 3U)
                x += (left + up) / 2;
            else if (filter == 4U)
                x += paeth_predictor(left, up, upper_left);
            cur[i] = (unsigned char)(x & 0xff);
        }
        for (unsigned x = 0; x < width; x++) {
            const unsigned char *src = cur + (size_t)x * bpp;
            unsigned char *dst = rgba + (((size_t)y * width + x) * 4U);

            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = bpp == 4U ? src[3] : 255U;
        }
        memcpy(prev, cur, stride);
        pos += stride;
    }

    *out_rgba = rgba;
    *out_width = width;
    *out_height = height;
    rgba = NULL;
    rc = 0;

done:
    free(cur);
    free(prev);
    free(rgba);
    free(raw);
    free(idat);
    return rc;
}

static int write_png_thumbnail_rgb565(const unsigned char *png_data,
                                      size_t png_size,
                                      const char *target_path)
{
    static const unsigned char bg_r = 10;
    static const unsigned char bg_g = 10;
    static const unsigned char bg_b = 26;
    unsigned char *rgba = NULL;
    unsigned char *rgb565 = NULL;
    unsigned width = 0;
    unsigned height = 0;
    unsigned scaled_w;
    unsigned scaled_h;
    unsigned offset_x;
    unsigned offset_y;
    unsigned x;
    unsigned y;
    unsigned short bg_color = rgb_to_rgb565(bg_r, bg_g, bg_b);
    int rc = -1;

    if (!png_data || png_size == 0 || !target_path || !*target_path)
        return -1;

    if (decode_png_rgba32(png_data, png_size, &rgba, &width, &height) < 0)
        goto done;

    rgb565 = malloc(DENEB_THUMB_RGB565_BYTES);
    if (!rgb565)
        goto done;

    for (y = 0; y < DENEB_THUMB_TARGET_SIZE; y++) {
        for (x = 0; x < DENEB_THUMB_TARGET_SIZE; x++) {
            write_rgb565_pixel(rgb565,
                               (size_t)y * DENEB_THUMB_TARGET_SIZE + x,
                               bg_color);
        }
    }

    if (width >= height) {
        scaled_w = DENEB_THUMB_TARGET_SIZE;
        scaled_h = (unsigned)(((uint64_t)height * DENEB_THUMB_TARGET_SIZE +
                               width / 2U) /
                              width);
    } else {
        scaled_h = DENEB_THUMB_TARGET_SIZE;
        scaled_w = (unsigned)(((uint64_t)width * DENEB_THUMB_TARGET_SIZE +
                               height / 2U) /
                              height);
    }
    if (scaled_w == 0)
        scaled_w = 1;
    if (scaled_h == 0)
        scaled_h = 1;

    offset_x = (DENEB_THUMB_TARGET_SIZE - scaled_w) / 2U;
    offset_y = (DENEB_THUMB_TARGET_SIZE - scaled_h) / 2U;

    for (y = 0; y < scaled_h; y++) {
        unsigned src_y = (unsigned)(((uint64_t)y * height) / scaled_h);

        if (src_y >= height)
            src_y = height - 1U;
        for (x = 0; x < scaled_w; x++) {
            unsigned src_x = (unsigned)(((uint64_t)x * width) / scaled_w);
            const unsigned char *src;
            unsigned alpha;
            unsigned r;
            unsigned g;
            unsigned b;

            if (src_x >= width)
                src_x = width - 1U;
            src = rgba + (((size_t)src_y * width + src_x) * 4U);
            alpha = src[3];
            r = ((unsigned)src[0] * alpha + (unsigned)bg_r * (255U - alpha)) /
                255U;
            g = ((unsigned)src[1] * alpha + (unsigned)bg_g * (255U - alpha)) /
                255U;
            b = ((unsigned)src[2] * alpha + (unsigned)bg_b * (255U - alpha)) /
                255U;
            write_rgb565_pixel(rgb565,
                               (size_t)(y + offset_y) *
                                   DENEB_THUMB_TARGET_SIZE +
                                   (x + offset_x),
                               rgb_to_rgb565((unsigned char)r,
                                             (unsigned char)g,
                                             (unsigned char)b));
        }
    }

    rc = write_all_bytes(target_path, rgb565, DENEB_THUMB_RGB565_BYTES);

done:
    free(rgb565);
    free(rgba);
    return rc;
}

int deneb_print_job_file_extract_ufp_model_gcode(const char *ufp_path,
                                                 const char *gcode_path)
{
    int rc;

    if (!ufp_path || !*ufp_path || !gcode_path || !*gcode_path) {
        errno = EINVAL;
        return -1;
    }

    rc = extract_ufp_member(ufp_path, "3D/model.gcode", gcode_path);
    if (rc == 0)
        return 0;
    return extract_ufp_member(ufp_path, "/3D/model.gcode", gcode_path);
}

int deneb_print_job_file_extract_ufp_thumbnail(const char *ufp_path,
                                               const char *out_path)
{
    deneb_ufp_thumb_candidate_t candidates[DENEB_UFP_THUMB_MAX_CANDIDATES];
    unsigned char *best_data = NULL;
    size_t best_size = 0;
    int best_score = 0;
    int best_ready = 0;
    size_t candidate_count;
    size_t i;
    const char *target_path = out_path ? out_path : DENEB_ACTIVE_THUMB_PATH;

    if (!ufp_path || !*ufp_path || !target_path || !*target_path) {
        errno = EINVAL;
        return -1;
    }

    candidate_count = list_ufp_thumbnail_members(
        ufp_path, candidates, DENEB_UFP_THUMB_MAX_CANDIDATES);
    if (candidate_count == 0)
        return -1;

    for (i = 0; i < candidate_count; i++) {
        unsigned char *data = NULL;
        size_t data_size = 0;
        int width = 0;
        int height = 0;
        int score;

        if (extract_zip_candidate(ufp_path, &candidates[i], &data,
                                  &data_size) != 0 ||
            read_png_dimensions_data(data, data_size, &width, &height) != 0) {
            free(data);
            continue;
        }

        score = thumbnail_score(width, height);
        if (!best_ready || score < best_score) {
            free(best_data);
            best_data = data;
            best_size = data_size;
            best_score = score;
            best_ready = 1;
            continue;
        }
        free(data);
    }

    if (!best_ready) {
        free(best_data);
        return -1;
    }

    if (write_png_thumbnail_rgb565(best_data, best_size, target_path) < 0) {
        free(best_data);
        return -1;
    }
    free(best_data);
    return 0;
}

void deneb_print_job_file_clear_active_thumbnail(void)
{
    unlink(DENEB_ACTIVE_THUMB_PATH);
}

int deneb_print_job_file_sanitize_name(const char *name, char *out,
                                       size_t out_sz)
{
    const char *base;
    const char *slash;
    const char *backslash;

    if (!out || out_sz == 0)
        return -1;

    base = name && *name ? name : "upload.gcode";
    slash = strrchr(base, '/');
    backslash = strrchr(base, '\\');
    if (slash && (!backslash || slash > backslash))
        base = slash + 1;
    else if (backslash)
        base = backslash + 1;

    if (!*base || strcmp(base, ".") == 0 || strcmp(base, "..") == 0)
        base = "upload.gcode";

    if (strlen(base) >= out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }
    memmove(out, base, strlen(base) + 1);

    return 0;
}

int deneb_print_job_file_spool_path(const char *name, char *out,
                                    size_t out_sz)
{
    char safe[128];

    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';

    if (deneb_print_job_file_sanitize_name(name, safe, sizeof(safe)) < 0)
        return -1;

    if (snprintf(out, out_sz, "%s/%s", DENEB_PRINT_JOB_SPOOL_DIR,
                 safe) >= (int)out_sz) {
        errno = ENAMETOOLONG;
        out[0] = '\0';
        return -1;
    }

    return 0;
}

int deneb_print_job_file_upload_storage_plan(
    const char *filename,
    deneb_print_job_upload_storage_plan_t *plan)
{
    if (!plan) {
        errno = EINVAL;
        return -1;
    }

    deneb_print_job_upload_storage_plan_init(plan);
    if (deneb_print_job_file_sanitize_name(filename, plan->filename,
                                           sizeof(plan->filename)) < 0)
        return -1;
    return deneb_print_job_file_spool_path(plan->filename, plan->dest_path,
                                           sizeof(plan->dest_path));
}

static int copy_file(const char *src_path, const char *dest_path)
{
    int src_fd;
    int dst_fd;
    char buf[65536];
    ssize_t nr;
    int copy_ok = 1;

    src_fd = open(src_path, O_RDONLY);
    dst_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (src_fd < 0 || dst_fd < 0) {
        if (src_fd >= 0)
            close(src_fd);
        if (dst_fd >= 0)
            close(dst_fd);
        return -1;
    }

    while ((nr = read(src_fd, buf, sizeof(buf))) >= 0) {
        if (nr == 0)
            break;
        if (write(dst_fd, buf, (size_t)nr) != nr) {
            copy_ok = 0;
            break;
        }
    }
    if (nr < 0)
        copy_ok = 0;

    close(src_fd);
    close(dst_fd);
    return copy_ok ? 0 : -1;
}

int deneb_print_job_file_store_upload(const char *src_path,
                                      const char *dest_path)
{
    if (!src_path || !*src_path || !dest_path || !*dest_path) {
        errno = EINVAL;
        return -1;
    }

    if (mkdir(DENEB_PRINT_JOB_SPOOL_DIR, 0755) < 0 && errno != EEXIST)
        return -1;

    if (rename(src_path, dest_path) == 0)
        return 0;

    if (copy_file(src_path, dest_path) < 0) {
        unlink(dest_path);
        return -1;
    }

    unlink(src_path);
    return 0;
}

int deneb_print_job_file_check_build_volume(
    const deneb_print_job_file_metadata_t *meta,
    char *out_error, size_t out_error_sz)
{
    const unsigned all_bounds = (1u << 6) - 1u;

    if (!meta)
        return -1;

    if (!meta->has_bounds)
        return 0;

    if (meta->bounds_invalid) {
        if (out_error && out_error_sz > 0) {
            snprintf(out_error, out_error_sz,
                     "model build-volume metadata is invalid");
        }
        return -1;
    }

    if (meta->bounds_field_mask != 0 &&
        meta->bounds_field_mask != all_bounds) {
        if (out_error && out_error_sz > 0) {
            snprintf(out_error, out_error_sz,
                     "model build-volume metadata is incomplete");
        }
        return -1;
    }

    if (!isfinite(meta->min_x) || !isfinite(meta->min_y) ||
        !isfinite(meta->min_z) || !isfinite(meta->max_x) ||
        !isfinite(meta->max_y) || !isfinite(meta->max_z) ||
        meta->min_x > meta->max_x ||
        meta->min_y > meta->max_y ||
        meta->min_z > meta->max_z) {
        if (out_error && out_error_sz > 0) {
            snprintf(out_error, out_error_sz,
                     "model build-volume metadata is invalid");
        }
        return -1;
    }

    if (meta->min_x < DENEB_BUILD_VOLUME_X_MIN_MM ||
        meta->min_y < DENEB_BUILD_VOLUME_Y_MIN_MM ||
        meta->min_z < DENEB_BUILD_VOLUME_Z_MIN_MM ||
        meta->max_x > DENEB_BUILD_VOLUME_X_MAX_MM ||
        meta->max_y > DENEB_BUILD_VOLUME_Y_MAX_MM ||
        meta->max_z > DENEB_BUILD_VOLUME_Z_MAX_MM) {
        if (out_error && out_error_sz > 0) {
            snprintf(out_error, out_error_sz,
                     "model exceeds build volume: "
                     "X [%.0f..%.0f] Y [%.0f..%.0f] Z [%.0f..%.0f]; "
                     "machine supports "
                     "X [%.0f..%.0f] Y [%.0f..%.0f] Z [%.0f..%.0f]",
                     (double)meta->min_x, (double)meta->max_x,
                     (double)meta->min_y, (double)meta->max_y,
                     (double)meta->min_z, (double)meta->max_z,
                     (double)DENEB_BUILD_VOLUME_X_MIN_MM,
                     (double)DENEB_BUILD_VOLUME_X_MAX_MM,
                     (double)DENEB_BUILD_VOLUME_Y_MIN_MM,
                     (double)DENEB_BUILD_VOLUME_Y_MAX_MM,
                     (double)DENEB_BUILD_VOLUME_Z_MIN_MM,
                     (double)DENEB_BUILD_VOLUME_Z_MAX_MM);
        }
        return -1;
    }

    return 0;
}
