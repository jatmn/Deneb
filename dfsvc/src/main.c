/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Native Deneb Digital Factory connector.
 *
 * This replaces the stock Python connector process. It intentionally reuses the
 * existing Gershwin/ZMQ Digital Factory control contract so deneb-api
 * digital-factory remains the UI-side bridge.
 */

#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha1.h>
#include <mbedtls/ssl.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>
#include <zmq.h>

#include "print_job_file.h"

#define IPC_BASE "tcp://127.0.0.1:"
#define GERSHWIN_PUB_BASE 5546
#define DF_PUB_PORT 5549
#define SOURCE "digitalfactory"
#define STATUS_TARGET "coordinator::digitalfactory::status"
#define CONNECT_SERVICE "digitalfactory::connector::connect"
#define DISCONNECT_SERVICE "digitalfactory::connector::disconnect"
#define DF_HANDLING_SERVICE "coordinator::digitalfactory::handling"
#define API_SOCKET_PATH "/var/run/deneb-api.sock"
#define DF_DOWNLOAD_DIR "/tmp"
#define DF_STATUS_FILE "/tmp/deneb-df-status"
#define DF_PAIR_REQUEST_FILE "/tmp/deneb-df-pair-request"

#define DF_STATE_DISCONNECTED 0
#define DF_STATE_ENTER_PIN 1
#define DF_STATE_CONNECTED 2
#define DF_STATE_RECONNECTING 3
#define DF_INSTR_UPDATE_FIRMWARE 4
#define DF_WS_PING_INTERVAL_MS 10000
#define DF_WS_PING_TIMEOUT_MS 30000
#define DF_STATUS_REQUEST_STALE_MS 120000
#define DF_PAIR_REQUEST_TTL_SECONDS 120

static volatile sig_atomic_t g_stop = 0;

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} mp_buf_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
} mp_reader_t;

typedef struct {
    char scheme[8];
    char host[128];
    char port[8];
    char path[256];
} df_url_t;

typedef struct {
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt ca;
    bool connected;
} ws_client_t;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log_line(const char *level, const char *msg)
{
    fprintf(stderr, "deneb-dfsvc: %s: %s\n", level, msg);
}

static void logf_line(const char *level, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "deneb-dfsvc: %s: ", level);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int run_read_line(const char *cmd, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0)
        return -1;
    dst[0] = '\0';
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;
    if (!fgets(dst, (int)dst_size, fp)) {
        pclose(fp);
        return -1;
    }
    int rc = pclose(fp);
    size_t len = strlen(dst);
    while (len > 0 && (dst[len - 1] == '\n' || dst[len - 1] == '\r'))
        dst[--len] = '\0';
    return rc == 0 ? 0 : -1;
}

static void uci_get(const char *key, const char *fallback,
                    char *dst, size_t dst_size)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "uci -q get %s 2>/dev/null", key);
    if (run_read_line(cmd, dst, dst_size) < 0 || dst[0] == '\0') {
        snprintf(dst, dst_size, "%s", fallback ? fallback : "");
    }
}

static int run_argv(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
}

static void uci_set_option(const char *name, const char *value)
{
    char option[128];
    if (value && value[0]) {
        char assignment[320];
        int n = snprintf(assignment, sizeof(assignment),
                         "ultimaker.option.%s=%s", name, value);
        if (n <= 0 || (size_t)n >= sizeof(assignment))
            return;
        char *set_argv[] = {"uci", "-q", "set", assignment, NULL};
        char *commit_argv[] = {"uci", "-q", "commit", "ultimaker", NULL};
        if (run_argv(set_argv) == 0)
            (void)run_argv(commit_argv);
    } else {
        int n = snprintf(option, sizeof(option), "ultimaker.option.%s", name);
        if (n <= 0 || (size_t)n >= sizeof(option))
            return;
        char *delete_argv[] = {"uci", "-q", "delete", option, NULL};
        char *commit_argv[] = {"uci", "-q", "commit", "ultimaker", NULL};
        (void)run_argv(delete_argv);
        (void)run_argv(commit_argv);
    }
}

static void digital_factory_disable_service(void)
{
    char *disable_argv[] = {"/etc/init.d/digitalfactory", "disable", NULL};
    (void)run_argv(disable_argv);
}

static bool pair_request_is_fresh(void)
{
    struct stat st;
    if (stat(DF_PAIR_REQUEST_FILE, &st) != 0)
        return false;
    return time(NULL) - st.st_mtime <= DF_PAIR_REQUEST_TTL_SECONDS;
}

static void clear_pair_request(void)
{
    unlink(DF_PAIR_REQUEST_FILE);
}

static void create_pair_request(void)
{
    FILE *fp = fopen(DF_PAIR_REQUEST_FILE, "w");
    if (fp)
        fclose(fp);
}


static int fd_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

static int local_api_request(const char *method, const char *path,
                             const char *body, char *out, size_t out_size)
{
    int fd = -1;
    struct sockaddr_un addr;
    char req[768];
    char resp[12288];
    size_t used = 0;
    int status_code = -1;

    if (!out || out_size == 0)
        return -1;
    out[0] = '\0';

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct timeval tv = {.tv_sec = 2, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", API_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto out;

    int body_len = body ? (int)strlen(body) : 0;
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.1\r\n"
                     "Host: deneb.local\r\n"
                     "Connection: close\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: %d\r\n"
                     "\r\n"
                     "%s",
                     method, path, body_len, body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(req))
        goto out;
    if (fd_write_all(fd, req, (size_t)n) < 0)
        goto out;

    while (used + 1 < sizeof(resp)) {
        ssize_t r = read(fd, resp + used, sizeof(resp) - used - 1);
        if (r <= 0)
            break;
        used += (size_t)r;
    }
    if (used + 1 >= sizeof(resp))
        goto out;
    resp[used] = '\0';
    if (sscanf(resp, "HTTP/%*s %d", &status_code) != 1)
        goto out;
    char *payload = strstr(resp, "\r\n\r\n");
    if (!payload)
        goto out;
    payload += 4;
    if (strlen(payload) >= out_size) {
        status_code = -1;
        goto out;
    }
    snprintf(out, out_size, "%s", payload);

out:
    if (fd >= 0)
        close(fd);
    return status_code;
}

static bool df_is_guid(const char *uuid)
{
    if (!uuid || strlen(uuid) != 36)
        return false;
    for (size_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-')
                return false;
        /*
         * Keep this lowercase-only for Digital Factory cloud protocol fields.
         * Do not widen it to generic UUID permissiveness unless old firmware
         * source or captured DF traffic proves uppercase A-F is valid here.
         */
        } else if (!((uuid[i] >= '0' && uuid[i] <= '9') ||
                     (uuid[i] >= 'a' && uuid[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void read_host_guid(char *out, size_t out_size)
{
    size_t i;

    if (!out || out_size == 0)
        return;
    uci_get("ultimaker.option.host_guid", "", out, out_size);
    if (!out[0])
        uci_get("deneb.system.guid",
                "00000000-0000-0000-0000-000000000000", out, out_size);
    for (i = 0; out[i]; i++)
        out[i] = (char)tolower((unsigned char)out[i]);
    if (!df_is_guid(out))
        snprintf(out, out_size, "00000000-0000-0000-0000-000000000000");
}

static void normalize_nozzle_id(const char *in, char *out, size_t out_size)
{
    char tmp[24];
    size_t len;

    if (!out || out_size == 0)
        return;
    snprintf(tmp, sizeof(tmp), "%s", in && in[0] ? in : "0.4");
    len = strlen(tmp);
    while (len > 0 && (tmp[len - 1] == '\n' || tmp[len - 1] == '\r' ||
                       tmp[len - 1] == ' ' || tmp[len - 1] == '\t'))
        tmp[--len] = '\0';
    if (strcmp(tmp, "0.40") == 0)
        snprintf(tmp, sizeof(tmp), "0.4");
    else if (strcmp(tmp, "0.60") == 0)
        snprintf(tmp, sizeof(tmp), "0.6");
    else if (strcmp(tmp, "0.80") == 0)
        snprintf(tmp, sizeof(tmp), "0.8");
    if (strstr(tmp, "mm")) {
        snprintf(out, out_size, "%s", tmp);
    } else {
        snprintf(out, out_size, "%s mm", tmp);
    }
}

static int local_api_upload_file(const char *file_path, const char *file_name,
                                 const char *job_instance_uuid,
                                 const char *cloud_job_id,
                                 char *out, size_t out_size)
{
    int fd = -1;
    FILE *fp = NULL;
    struct sockaddr_un addr;
    struct stat st;
    char boundary[64];
    char meta[768];
    char head[512];
    char tail[64];
    char hdr[1024];
    char resp[8192];
    char chunk[2048];
    size_t used = 0;
    int status_code = -1;

    if (!out || out_size == 0)
        return -1;
    out[0] = '\0';
    fp = fopen(file_path, "rb");
    if (!fp)
        return -1;
    if (fstat(fileno(fp), &st) < 0 || st.st_size <= 0)
        goto out;
    snprintf(boundary, sizeof(boundary), "deneb-dfsvc-%lld",
             (long long)time(NULL));
    snprintf(meta, sizeof(meta),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"job_instance_uuid\"\r\n\r\n"
             "%s\r\n"
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"cloud_job_id\"\r\n\r\n"
             "%s\r\n"
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"owner\"\r\n\r\n"
             "Digital Factory\r\n",
             boundary, job_instance_uuid ? job_instance_uuid : "",
             boundary, cloud_job_id ? cloud_job_id : "",
             boundary);
    snprintf(head, sizeof(head),
             "--%s\r\n"
             "Content-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             boundary, file_name);
    snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
    long long content_len = (long long)strlen(meta) + (long long)strlen(head) +
                            (long long)st.st_size + (long long)strlen(tail);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        goto out;
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", API_SOCKET_PATH);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto out;

    int n = snprintf(hdr, sizeof(hdr),
                     "POST /cluster-api/v1/print_jobs HTTP/1.1\r\n"
                     "Host: deneb.local\r\n"
                     "Connection: close\r\n"
                     "Content-Type: multipart/form-data; boundary=%s\r\n"
                     "Content-Length: %lld\r\n\r\n",
                     boundary, content_len);
    if (n <= 0 || (size_t)n >= sizeof(hdr))
        goto out;
    if (fd_write_all(fd, hdr, (size_t)n) < 0 ||
        fd_write_all(fd, meta, strlen(meta)) < 0 ||
        fd_write_all(fd, head, strlen(head)) < 0)
        goto out;

    while (!feof(fp)) {
        size_t r = fread(chunk, 1, sizeof(chunk), fp);
        if (r > 0 && fd_write_all(fd, chunk, r) < 0)
            goto out;
        if (ferror(fp))
            goto out;
    }
    if (fd_write_all(fd, tail, strlen(tail)) < 0)
        goto out;

    while (used + 1 < sizeof(resp)) {
        ssize_t r = read(fd, resp + used, sizeof(resp) - used - 1);
        if (r <= 0)
            break;
        used += (size_t)r;
    }
    resp[used] = '\0';
    if (sscanf(resp, "HTTP/%*s %d", &status_code) != 1)
        goto out;
    char *payload = strstr(resp, "\r\n\r\n");
    if (payload) {
        payload += 4;
        snprintf(out, out_size, "%s", payload);
    }

out:
    if (fp)
        fclose(fp);
    if (fd >= 0)
        close(fd);
    return status_code;
}

static bool df_printer_is_idle(void)
{
    char status[64];
    if (local_api_request("GET", "/api/v1/printer/status", NULL,
                          status, sizeof(status)) != 200)
        return false;
    status[strcspn(status, "\r\n \t")] = '\0';
    if (status[0] == '"') {
        char unquoted[64];
        size_t len = strlen(status);
        if (len >= 2 && status[len - 1] == '"' &&
            len - 1 < sizeof(unquoted)) {
            memcpy(unquoted, status + 1, len - 2);
            unquoted[len - 2] = '\0';
            snprintf(status, sizeof(status), "%s", unquoted);
        }
    }
    return strcmp(status, "idle") == 0;
}

static bool filename_has_print_extension(const char *name)
{
    size_t len;

    if (!name)
        return false;
    len = strlen(name);
    return (len >= 6 && strcasecmp(name + len - 6, ".gcode") == 0) ||
           (len >= 4 && strcasecmp(name + len - 4, ".gco") == 0) ||
           (len >= 4 && strcasecmp(name + len - 4, ".ufp") == 0);
}

static void sanitize_filename(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    if (!out || out_size == 0)
        return;
    if (!in || !in[0])
        in = "digital_factory.gcode";
    while (*in && n + 1 < out_size) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')
            out[n++] = (char)c;
        else if (c == ' ')
            out[n++] = '_';
    }
    out[n] = '\0';
    if (n == 0)
        snprintf(out, out_size, "digital_factory.gcode");
    if (!filename_has_print_extension(out)) {
        size_t len = strlen(out);
        if (len + 6 < out_size)
            snprintf(out + len, out_size - len, ".gcode");
    }
}

static void sanitize_hostname(const char *in, char *out, size_t out_size)
{
    size_t n = 0;
    if (!out || out_size == 0)
        return;
    if (!in || !in[0])
        in = "Ultimaker";
    while (*in && n + 1 < out_size) {
        unsigned char c = (unsigned char)*in++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[n++] = (char)c;
        else if (c == ' ')
            out[n++] = '-';
    }
    out[n] = '\0';
    if (n == 0)
        snprintf(out, out_size, "Ultimaker");
}

static int download_file_with_wget(const char *url, const char *path)
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execlp("wget", "wget", "-q", "-T", "60", "-t", "1", "-O", path,
               url, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;
    if (!WIFEXITED(status))
        return -2;
    if (WEXITSTATUS(status) != 0)
        return WEXITSTATUS(status);
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0 ? 0 : -3;
}

static bool filename_ends_with_ci(const char *name, const char *suffix)
{
    size_t name_len;
    size_t suffix_len;

    if (!name || !suffix)
        return false;
    name_len = strlen(name);
    suffix_len = strlen(suffix);
    return name_len >= suffix_len &&
           strcasecmp(name + name_len - suffix_len, suffix) == 0;
}

static void replace_file_extension(const char *in, const char *extension,
                                   char *out, size_t out_size)
{
    const char *dot;

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!in || !in[0]) {
        snprintf(out, out_size, "digital_factory%s", extension);
        return;
    }
    snprintf(out, out_size, "%s", in);
    dot = strrchr(out, '.');
    if (dot)
        out[dot - out] = '\0';
    if (strlen(out) + strlen(extension) + 1 <= out_size)
        strncat(out, extension, out_size - strlen(out) - 1);
}

static int extract_ufp_member(const char *ufp_path, const char *member,
                              const char *gcode_path)
{
    pid_t pid;
    int out_fd;
    int status = 0;
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
    if (!WIFEXITED(status))
        return -2;
    return WEXITSTATUS(status) != 0 ? WEXITSTATUS(status) : -3;
}

static int extract_ufp_model_gcode(const char *ufp_path, const char *gcode_path)
{
    int rc = extract_ufp_member(ufp_path, "3D/model.gcode", gcode_path);
    if (rc == 0)
        return 0;
    return extract_ufp_member(ufp_path, "/3D/model.gcode", gcode_path);
}

static int prepend_file_metadata(const char *path, const char *material_guid,
                                 const char *print_core_id)
{
    char tmp_path[300];
    int src = -1;
    int dst = -1;
    char buf[4096];
    ssize_t nr;
    int ok = 0;

    if (!path || !path[0])
        return -1;
    if ((!material_guid || !material_guid[0]) &&
        (!print_core_id || !print_core_id[0]))
        return 0;

    if (snprintf(tmp_path, sizeof(tmp_path), "%s.meta", path) >=
        (int)sizeof(tmp_path))
        return -1;

    src = open(path, O_RDONLY);
    dst = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (src < 0 || dst < 0)
        goto out;

    if (material_guid && material_guid[0]) {
        dprintf(dst, "; material_guid=%s\n", material_guid);
    }
    if (print_core_id && print_core_id[0]) {
        dprintf(dst, "; print_core_id=%s\n", print_core_id);
    }

    while ((nr = read(src, buf, sizeof(buf))) > 0) {
        if (fd_write_all(dst, buf, (size_t)nr) < 0)
            goto out;
    }
    if (nr < 0)
        goto out;
    ok = 1;

out:
    if (src >= 0)
        close(src);
    if (dst >= 0)
        close(dst);
    if (ok) {
        if (rename(tmp_path, path) == 0)
            return 0;
    }
    unlink(tmp_path);
    return -1;
}

static int set_printer_name(const char *name)
{
    char safe[64];
    char cmd[384];
    sanitize_hostname(name, safe, sizeof(safe));
    snprintf(cmd, sizeof(cmd),
             "uci -q set system.@system[0].hostname='%s' && "
             "uci -q set network.wwan.hostname='%s' && "
             "uci -q set wireless.@wifi-iface[0].ssid='%s' && "
             "uci -q set wireless.ap.ssid='%s' && "
             "uci -q set ultimaker.option.printer_name='%s' && "
             "uci -q commit system && uci -q commit network && "
             "uci -q commit wireless && uci -q commit ultimaker && "
             "hostname '%s'",
             safe, safe, safe, safe, safe, safe);
    return system(cmd) == 0 ? 0 : -1;
}

static const char *json_array_or_empty(const char *json)
{
    if (!json)
        return "[]";
    while (*json == ' ' || *json == '\t' || *json == '\r' || *json == '\n')
        json++;
    return *json == '[' ? json : "[]";
}

static const char *df_cluster_printer_status_label(const char *status)
{
    if (!status || !status[0])
        return "unknown";
    if (strcasecmp(status, "offline") == 0)
        return "unreachable";
    if (strcasecmp(status, "paused") == 0 ||
        strcasecmp(status, "aborting") == 0)
        return "printing";
    if (strcasecmp(status, "finished") == 0)
        return "idle";
    return status;
}

static void json_escape_string(const char *src, char *out, size_t out_size)
{
    size_t n = 0;
    if (!out || out_size == 0)
        return;
    if (!src)
        src = "";
    while (*src && n + 1 < out_size) {
        unsigned char c = (unsigned char)*src++;
        if ((c == '"' || c == '\\') && n + 2 < out_size) {
            out[n++] = '\\';
            out[n++] = (char)c;
        } else if (c >= 0x20) {
            out[n++] = (char)c;
        }
    }
    out[n] = '\0';
}

static const char *material_type_from_guid(const char *guid)
{
    if (!guid)
        return "PLA";
    if (strcmp(guid, "9d5d2d7c-4e77-441c-85a0-e9eefd4aa68c") == 0)
        return "Tough PLA";
    if (strcmp(guid, "1cbfaeb3-1906-4b26-b2e7-6f777a8c197a") == 0)
        return "PETG";
    if (strcmp(guid, "60636bb4-518f-42e7-8237-fe77b194ebe0") == 0)
        return "ABS";
    if (strcmp(guid, "12f41353-1a33-415e-8b4f-a775a6c70cc6") == 0)
        return "CPE";
    if (strcmp(guid, "e2409626-b5a0-4025-b73e-b58070219259") == 0)
        return "CPE+";
    if (strcmp(guid, "28fb4162-db74-49e1-9008-d05f1e8bef5c") == 0)
        return "Nylon";
    if (strcmp(guid, "98c05714-bf4e-4455-ba27-57d74fe331e4") == 0)
        return "PC";
    if (strcmp(guid, "aa22e9c7-421f-4745-afc2-81851694394a") == 0)
        return "PP";
    if (strcmp(guid, "1d52b2be-a3a2-41de-a8b1-3bcdb5618695") == 0)
        return "TPU 95A";
    return "PLA";
}

static bool is_supported_print_job_action(const char *action)
{
    return strcmp(action, "abort") == 0 || strcmp(action, "remove") == 0 ||
           strcmp(action, "pause") == 0 || strcmp(action, "resume") == 0 ||
           strcmp(action, "force") == 0;
}

static const char *local_print_job_action(const char *action)
{
    return strcmp(action, "remove") == 0 ? "abort" : action;
}

static int mp_ensure(mp_buf_t *mp, size_t need)
{
    if (mp->len + need <= mp->cap)
        return 0;
    size_t cap = mp->cap ? mp->cap * 2 : 256;
    while (cap < mp->len + need)
        cap *= 2;
    uint8_t *p = realloc(mp->buf, cap);
    if (!p)
        return -1;
    mp->buf = p;
    mp->cap = cap;
    return 0;
}

static int mp_write(mp_buf_t *mp, const void *data, size_t len)
{
    if (mp_ensure(mp, len) < 0)
        return -1;
    memcpy(mp->buf + mp->len, data, len);
    mp->len += len;
    return 0;
}

static int mp_put_u8(mp_buf_t *mp, uint8_t v) { return mp_write(mp, &v, 1); }

static int mp_put_u16(mp_buf_t *mp, uint16_t v)
{
    uint8_t d[3] = {0xcd, (uint8_t)(v >> 8), (uint8_t)v};
    return mp_write(mp, d, sizeof(d));
}

static int mp_put_i64(mp_buf_t *mp, int64_t v)
{
    uint8_t d[9] = {0xd3, (uint8_t)(v >> 56), (uint8_t)(v >> 48),
                    (uint8_t)(v >> 40), (uint8_t)(v >> 32),
                    (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                    (uint8_t)(v >> 8), (uint8_t)v};
    return mp_write(mp, d, sizeof(d));
}

static int mp_put_bool(mp_buf_t *mp, bool v)
{
    return mp_put_u8(mp, v ? 0xc3 : 0xc2);
}

static int mp_put_str(mp_buf_t *mp, const char *s)
{
    size_t len = strlen(s);
    if (len <= 31) {
        if (mp_put_u8(mp, (uint8_t)(0xa0 | len)) < 0)
            return -1;
    } else if (len <= 65535) {
        if (mp_put_u8(mp, 0xda) < 0 || mp_put_u16(mp, (uint16_t)len) < 0)
            return -1;
    } else {
        return -1;
    }
    return mp_write(mp, s, len);
}

static int mp_encode_rpc_key(mp_buf_t *mp, const char *source,
                             const char *target)
{
    if (mp_put_u8(mp, 0x84) < 0)
        return -1;
    if (mp_put_str(mp, "ts") < 0 || mp_put_i64(mp, monotonic_ms()) < 0)
        return -1;
    if (mp_put_str(mp, "action") < 0 || mp_put_str(mp, "rpc-request") < 0)
        return -1;
    if (mp_put_str(mp, "source") < 0 || mp_put_str(mp, source) < 0)
        return -1;
    if (mp_put_str(mp, "target") < 0 || mp_put_str(mp, target) < 0)
        return -1;
    return 0;
}

static int mp_read_u8(mp_reader_t *r, uint8_t *out)
{
    if (r->pos >= r->len)
        return -1;
    *out = r->buf[r->pos++];
    return 0;
}

static int mp_read_bytes(mp_reader_t *r, void *out, size_t n)
{
    if (r->pos + n > r->len)
        return -1;
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return 0;
}

static int mp_skip_value(mp_reader_t *r);

static int mp_skip_values(mp_reader_t *r, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (mp_skip_value(r) < 0)
            return -1;
    }
    return 0;
}

static int mp_skip_value(mp_reader_t *r)
{
    uint8_t tag;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag <= 0x7f || tag >= 0xe0 || tag == 0xc0 || tag == 0xc2 || tag == 0xc3)
        return 0;
    if (tag >= 0xa0 && tag <= 0xbf) {
        r->pos += tag & 0x1f;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag >= 0x80 && tag <= 0x8f)
        return mp_skip_values(r, (tag & 0x0f) * 2);
    if (tag >= 0x90 && tag <= 0x9f)
        return mp_skip_values(r, tag & 0x0f);
    if (tag == 0xcc || tag == 0xd0) {
        r->pos += 1;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag == 0xcd || tag == 0xd1) {
        r->pos += 2;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag == 0xce || tag == 0xd2 || tag == 0xca) {
        r->pos += 4;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag == 0xcf || tag == 0xd3 || tag == 0xcb) {
        r->pos += 8;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag == 0xd9) {
        uint8_t l;
        if (mp_read_u8(r, &l) < 0)
            return -1;
        r->pos += l;
        return r->pos <= r->len ? 0 : -1;
    }
    if (tag == 0xda || tag == 0xdc || tag == 0xde) {
        uint8_t d[2];
        if (mp_read_bytes(r, d, 2) < 0)
            return -1;
        size_t n = ((size_t)d[0] << 8) | d[1];
        if (tag == 0xda) {
            r->pos += n;
            return r->pos <= r->len ? 0 : -1;
        }
        return mp_skip_values(r, tag == 0xde ? n * 2 : n);
    }
    return -1;
}

static int mp_read_str(mp_reader_t *r, char *out, size_t out_size)
{
    uint8_t tag;
    size_t len = 0;
    if (mp_read_u8(r, &tag) < 0)
        return -1;
    if (tag >= 0xa0 && tag <= 0xbf) {
        len = tag & 0x1f;
    } else if (tag == 0xd9) {
        uint8_t l;
        if (mp_read_u8(r, &l) < 0)
            return -1;
        len = l;
    } else if (tag == 0xda) {
        uint8_t d[2];
        if (mp_read_bytes(r, d, 2) < 0)
            return -1;
        len = ((size_t)d[0] << 8) | d[1];
    } else {
        return -1;
    }
    if (r->pos + len > r->len || out_size == 0)
        return -1;
    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, r->buf + r->pos, copy);
    out[copy] = '\0';
    r->pos += len;
    return 0;
}

static int read_key_parts(zmq_msg_t *key_msg, char *action, size_t action_size,
                          char *source, size_t source_size,
                          char *target, size_t target_size)
{
    mp_reader_t r = {(const uint8_t *)zmq_msg_data(key_msg), zmq_msg_size(key_msg), 0};
    uint8_t tag;
    if (mp_read_u8(&r, &tag) < 0 || (tag & 0xf0) != 0x80)
        return -1;
    size_t pair_count = (size_t)(tag & 0x0f);
    for (size_t i = 0; i < pair_count; i++) {
        char name[32];
        if (mp_read_str(&r, name, sizeof(name)) < 0)
            return -1;
        if (strcmp(name, "action") == 0) {
            if (mp_read_str(&r, action, action_size) < 0)
                return -1;
        } else if (strcmp(name, "source") == 0) {
            if (mp_read_str(&r, source, source_size) < 0)
                return -1;
        } else if (strcmp(name, "target") == 0) {
            if (mp_read_str(&r, target, target_size) < 0)
                return -1;
        } else if (mp_skip_value(&r) < 0) {
            return -1;
        }
    }
    return 0;
}

static const char *df_state_name(int state)
{
    switch (state) {
    case DF_STATE_DISCONNECTED:
        return "disconnected";
    case DF_STATE_ENTER_PIN:
        return "enter_pin";
    case DF_STATE_CONNECTED:
        return "connected";
    case DF_STATE_RECONNECTING:
        return "reconnecting";
    default:
        return "unknown";
    }
}

static void write_status_file(int state, const char *pin)
{
    FILE *fp = fopen(DF_STATUS_FILE, "w");
    if (!fp)
        return;
    if (pin && pin[0])
        fprintf(fp, "state=%s pin=%s\n", df_state_name(state), pin);
    else
        fprintf(fp, "state=%s\n", df_state_name(state));
    fclose(fp);
}

static int publish_status(void *pub, int state, const char *pin)
{
    mp_buf_t key = {0};
    mp_buf_t data = {0};
    int rc = -1;

    write_status_file(state, pin);

    if (mp_put_u8(&key, 0x85) < 0 ||
        mp_put_str(&key, "ts") < 0 || mp_put_i64(&key, monotonic_ms()) < 0 ||
        mp_put_str(&key, "action") < 0 || mp_put_str(&key, "drop") < 0 ||
        mp_put_str(&key, "source") < 0 || mp_put_str(&key, SOURCE) < 0 ||
        mp_put_str(&key, "uuid4") < 0 || mp_put_str(&key, "deneb-dfsvc-status") < 0 ||
        mp_put_str(&key, "target") < 0 || mp_put_str(&key, STATUS_TARGET) < 0)
        goto out;

    if (mp_put_u8(&data, 0x84) < 0 ||
        mp_put_str(&data, "tracker") < 0 || mp_put_u8(&data, 0) < 0 ||
        mp_put_str(&data, "state") < 0 || mp_put_u8(&data, (uint8_t)state) < 0 ||
        mp_put_str(&data, "pin") < 0 || mp_put_str(&data, pin ? pin : "") < 0 ||
        mp_put_str(&data, "pin_timestamp") < 0 || mp_put_i64(&data, monotonic_ms() / 1000) < 0)
        goto out;

    if (zmq_send(pub, key.buf, key.len, ZMQ_SNDMORE) < 0 ||
        zmq_send(pub, data.buf, data.len, 0) < 0)
        goto out;
    rc = 0;
out:
    free(key.buf);
    free(data.buf);
    return rc;
}

static int parse_url(const char *url, df_url_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *p = strstr(url, "://");
    if (!p)
        return -1;
    size_t scheme_len = (size_t)(p - url);
    if (scheme_len >= sizeof(out->scheme))
        return -1;
    memcpy(out->scheme, url, scheme_len);
    out->scheme[scheme_len] = '\0';
    p += 3;
    const char *path = strchr(p, '/');
    const char *host_end = path ? path : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(host_end - p));
    size_t host_len = (size_t)((colon ? colon : host_end) - p);
    if (host_len == 0 || host_len >= sizeof(out->host))
        return -1;
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    if (colon) {
        size_t port_len = (size_t)(host_end - colon - 1);
        if (port_len == 0 || port_len >= sizeof(out->port))
            return -1;
        memcpy(out->port, colon + 1, port_len);
        out->port[port_len] = '\0';
    } else {
        snprintf(out->port, sizeof(out->port), "%s",
                 strcmp(out->scheme, "wss") == 0 ? "443" : "80");
    }
    snprintf(out->path, sizeof(out->path), "%s", path ? path : "/");
    return strcmp(out->scheme, "wss") == 0 ? 0 : -1;
}

static void ws_free(ws_client_t *ws)
{
    if (!ws)
        return;
    mbedtls_ssl_close_notify(&ws->ssl);
    mbedtls_net_free(&ws->net);
    mbedtls_ssl_free(&ws->ssl);
    mbedtls_ssl_config_free(&ws->conf);
    mbedtls_ctr_drbg_free(&ws->ctr_drbg);
    mbedtls_entropy_free(&ws->entropy);
    mbedtls_x509_crt_free(&ws->ca);
    memset(ws, 0, sizeof(*ws));
}

static int ws_write_all(ws_client_t *ws, const uint8_t *buf, size_t len)
{
    while (len > 0) {
        int n = mbedtls_ssl_write(&ws->ssl, buf, len);
        if (n <= 0)
            return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

static int ws_read_exact(ws_client_t *ws, uint8_t *buf, size_t len)
{
    size_t used = 0;
    while (used < len) {
        int n = mbedtls_ssl_read(&ws->ssl, buf + used, len - used);
        if (n <= 0)
            return -1;
        used += (size_t)n;
    }
    return 0;
}

static int ws_random(ws_client_t *ws, uint8_t *buf, size_t len)
{
    return mbedtls_ctr_drbg_random(&ws->ctr_drbg, buf, len) == 0 ? 0 : -1;
}

static int base64_encode(const uint8_t *input, size_t input_len,
                         char *out, size_t out_size)
{
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_size, &olen,
                              input, input_len) != 0 ||
        olen >= out_size)
        return -1;
    out[olen] = '\0';
    return 0;
}

static int ws_make_key(ws_client_t *ws, char out[25])
{
    uint8_t nonce[16];
    if (ws_random(ws, nonce, sizeof(nonce)) < 0)
        return -1;
    return base64_encode(nonce, sizeof(nonce), out, 25);
}

static int ws_expected_accept(const char *key, char out[32])
{
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char input[96];
    uint8_t digest[20];
    int n = snprintf(input, sizeof(input), "%s%s", key, guid);
    if (n <= 0 || (size_t)n >= sizeof(input))
        return -1;
#if MBEDTLS_VERSION_NUMBER >= 0x02070000
    if (mbedtls_sha1_ret((const unsigned char *)input, strlen(input), digest) != 0)
        return -1;
#else
    mbedtls_sha1((const unsigned char *)input, strlen(input), digest);
#endif
    return base64_encode(digest, sizeof(digest), out, 32);
}

static bool header_value_matches(const char *headers, const char *name,
                                 const char *expected)
{
    size_t name_len = strlen(name);
    const char *line = headers;
    while (*line) {
        const char *next = strstr(line, "\r\n");
        size_t len = next ? (size_t)(next - line) : strlen(line);
        if (len > name_len && line[name_len] == ':' &&
            strncasecmp(line, name, name_len) == 0) {
            const char *value = line + name_len + 1;
            size_t value_len = len - name_len - 1;
            while (value_len > 0 && (*value == ' ' || *value == '\t')) {
                value++;
                value_len--;
            }
            while (value_len > 0 &&
                   (value[value_len - 1] == ' ' || value[value_len - 1] == '\t'))
                value_len--;
            return strlen(expected) == value_len &&
                   strncmp(value, expected, value_len) == 0;
        }
        if (!next)
            break;
        line = next + 2;
    }
    return false;
}

static int ws_send_control(ws_client_t *ws, uint8_t opcode,
                           const uint8_t *payload, size_t len)
{
    uint8_t hdr[14];
    size_t h = 0;
    uint8_t mask[4];
    if (len > 125)
        return -1;
    if (ws_random(ws, mask, sizeof(mask)) < 0)
        return -1;
    hdr[h++] = (uint8_t)(0x80 | (opcode & 0x0f));
    hdr[h++] = (uint8_t)(0x80 | len);
    memcpy(hdr + h, mask, 4);
    h += 4;
    if (ws_write_all(ws, hdr, h) < 0)
        return -1;
    if (len == 0)
        return 0;
    uint8_t buf[125];
    for (size_t i = 0; i < len; i++)
        buf[i] = payload[i] ^ mask[i % 4];
    return ws_write_all(ws, buf, len);
}

static void ws_send_close(ws_client_t *ws)
{
    if (!ws || !ws->connected)
        return;
    (void)ws_send_control(ws, 0x8, NULL, 0);
}

static int ws_connect(ws_client_t *ws, const df_url_t *url)
{
    memset(ws, 0, sizeof(*ws));
    mbedtls_net_init(&ws->net);
    mbedtls_ssl_init(&ws->ssl);
    mbedtls_ssl_config_init(&ws->conf);
    mbedtls_entropy_init(&ws->entropy);
    mbedtls_ctr_drbg_init(&ws->ctr_drbg);
    mbedtls_x509_crt_init(&ws->ca);

    if (mbedtls_ctr_drbg_seed(&ws->ctr_drbg, mbedtls_entropy_func,
                              &ws->entropy, (const unsigned char *)"deneb-dfsvc", 11) != 0)
        return -1;
    (void)mbedtls_x509_crt_parse_path(&ws->ca, "/etc/ssl/certs");
    if (mbedtls_net_connect(&ws->net, url->host, url->port, MBEDTLS_NET_PROTO_TCP) != 0)
        return -1;
    if (mbedtls_ssl_config_defaults(&ws->conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0)
        return -1;
    mbedtls_ssl_conf_authmode(&ws->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&ws->conf, &ws->ca, NULL);
    mbedtls_ssl_conf_rng(&ws->conf, mbedtls_ctr_drbg_random, &ws->ctr_drbg);
    if (mbedtls_ssl_setup(&ws->ssl, &ws->conf) != 0)
        return -1;
    if (mbedtls_ssl_set_hostname(&ws->ssl, url->host) != 0)
        return -1;
    mbedtls_ssl_set_bio(&ws->ssl, &ws->net, mbedtls_net_send, mbedtls_net_recv, NULL);
    int rc;
    while ((rc = mbedtls_ssl_handshake(&ws->ssl)) != 0) {
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            return -1;
    }
    if (mbedtls_ssl_get_verify_result(&ws->ssl) != 0)
        return -1;

    char key[25];
    char expected_accept[32];
    if (ws_make_key(ws, key) < 0 || ws_expected_accept(key, expected_accept) < 0)
        return -1;
    char req[768];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\n"
                     "\r\n",
                     url->path, url->host, key);
    if (n <= 0 || (size_t)n >= sizeof(req) ||
        ws_write_all(ws, (const uint8_t *)req, (size_t)n) < 0)
        return -1;

    char resp[1024];
    size_t used = 0;
    while (used + 1 < sizeof(resp)) {
        int r = mbedtls_ssl_read(&ws->ssl, (unsigned char *)resp + used, 1);
        if (r <= 0)
            return -1;
        used += (size_t)r;
        resp[used] = '\0';
        if (strstr(resp, "\r\n\r\n"))
            break;
    }
    if (!strstr(resp, " 101 ") ||
        !header_value_matches(resp, "Sec-WebSocket-Accept", expected_accept))
        return -1;
    ws->connected = true;
    return 0;
}

static int ws_wait_readable(ws_client_t *ws, uint32_t timeout_ms)
{
    int rc = mbedtls_net_poll(&ws->net, MBEDTLS_NET_POLL_READ, timeout_ms);
    if (rc < 0)
        return -1;
    return (rc & MBEDTLS_NET_POLL_READ) ? 1 : 0;
}

static int ws_send_text(ws_client_t *ws, const char *text)
{
    size_t len = strlen(text);
    uint8_t hdr[14];
    size_t h = 0;
    hdr[h++] = 0x81;
    if (len < 126) {
        hdr[h++] = 0x80 | (uint8_t)len;
    } else if (len <= 65535) {
        hdr[h++] = 0x80 | 126;
        hdr[h++] = (uint8_t)(len >> 8);
        hdr[h++] = (uint8_t)len;
    } else {
        return -1;
    }
    uint8_t mask[4];
    if (ws_random(ws, mask, sizeof(mask)) < 0)
        return -1;
    memcpy(hdr + h, mask, 4);
    h += 4;
    uint8_t *payload = malloc(len);
    if (!payload)
        return -1;
    for (size_t i = 0; i < len; i++)
        payload[i] = ((const uint8_t *)text)[i] ^ mask[i % 4];
    int rc = ws_write_all(ws, hdr, h) == 0 && ws_write_all(ws, payload, len) == 0 ? 0 : -1;
    free(payload);
    return rc;
}

static int ws_read_text(ws_client_t *ws, char *buf, size_t buf_size)
{
    size_t used = 0;
    bool in_text = false;

    while (true) {
        uint8_t h[2];
        if (ws_read_exact(ws, h, 2) < 0)
            return -1;
        bool fin = (h[0] & 0x80) != 0;
        int opcode = h[0] & 0x0f;
        bool masked = (h[1] & 0x80) != 0;
        size_t len = h[1] & 0x7f;
        if (len == 126) {
            uint8_t x[2];
            if (ws_read_exact(ws, x, 2) < 0)
                return -1;
            len = ((size_t)x[0] << 8) | x[1];
        } else if (len == 127) {
            return -1;
        }

        uint8_t mask[4] = {0};
        if (masked && ws_read_exact(ws, mask, 4) < 0)
            return -1;

        if (opcode == 0x8) {
            uint8_t control[125];
            unsigned int close_code = 0;
            char reason[96] = "";
            if (len > sizeof(control))
                return -1;
            if (ws_read_exact(ws, control, len) < 0)
                return -1;
            if (masked) {
                for (size_t i = 0; i < len; i++)
                    control[i] ^= mask[i % 4];
            }
            if (len >= 2) {
                close_code = ((unsigned int)control[0] << 8) | control[1];
                size_t reason_len = len - 2;
                if (reason_len >= sizeof(reason))
                    reason_len = sizeof(reason) - 1;
                memcpy(reason, control + 2, reason_len);
                reason[reason_len] = '\0';
            }
            logf_line("warn", "Digital Factory websocket close frame code=%u reason=%s",
                      close_code, reason[0] ? reason : "-");
            return -1;
        }

        if (opcode == 0x9 || opcode == 0x0a) {
            uint8_t control[125];
            if (len > sizeof(control))
                return -1;
            if (ws_read_exact(ws, control, len) < 0)
                return -1;
            if (masked) {
                for (size_t i = 0; i < len; i++)
                    control[i] ^= mask[i % 4];
            }
            if (opcode == 0x9)
                (void)ws_send_control(ws, 0x0a, control, len);
            if (!in_text)
                return 1;
            continue;
        }

        if (opcode != 0x1 && opcode != 0x0) {
            uint8_t discard[256];
            while (len > 0) {
                size_t chunk = len < sizeof(discard) ? len : sizeof(discard);
                if (ws_read_exact(ws, discard, chunk) < 0)
                    return -1;
                len -= chunk;
            }
            if (!in_text)
                return 1;
            continue;
        }

        if (opcode == 0x1) {
            used = 0;
            in_text = true;
        } else if (!in_text) {
            return -1;
        }

        if (used + len + 1 > buf_size)
            return -1;
        if (ws_read_exact(ws, (uint8_t *)buf + used, len) < 0)
            return -1;
        if (masked) {
            for (size_t i = 0; i < len; i++)
                buf[used + i] = (char)(((uint8_t)buf[used + i]) ^ mask[i % 4]);
        }
        used += len;
        if (fin) {
            buf[used] = '\0';
            return 0;
        }
    }
}

static int json_hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static const char *json_str_value(const char *json, const char *key,
                                  char *out, size_t out_size)
{
    char needle[64];
    int truncated = 0;

    if (!json || !key || !out || out_size == 0)
        return NULL;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p)
        return NULL;
    p = strchr(p + strlen(needle), ':');
    if (!p)
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p != '"')
        return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        char c = *p++;

        if (c == '\\' && *p) {
            c = *p++;
            switch (c) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            case 'u': {
                int h0 = json_hex_digit(p[0]);
                int h1 = json_hex_digit(p[1]);
                int h2 = json_hex_digit(p[2]);
                int h3 = json_hex_digit(p[3]);
                if (h0 >= 0 && h1 >= 0 && h2 >= 0 && h3 >= 0) {
                    unsigned code = (unsigned)((h0 << 12) | (h1 << 8) |
                                               (h2 << 4) | h3);
                    p += 4;
                    c = code <= 0x7f ? (char)code : '?';
                }
                break;
            }
            default:
                break;
            }
        }

        if (i + 1 < out_size) {
            out[i++] = c;
        } else {
            truncated = 1;
        }
    }
    out[i] = '\0';
    return truncated ? NULL : out;
}

static const char *json_top_level_str_value(const char *json, const char *key,
                                            char *out, size_t out_size)
{
    char needle[64];
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    if (!json || !key || !out || out_size == 0)
        return NULL;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    for (const char *p = json; *p; p++) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }

        if (*p == '"') {
            if (depth == 1 && strncmp(p, needle, strlen(needle)) == 0) {
                const char *q = p + strlen(needle);
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
                    q++;
                if (*q != ':')
                    continue;
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
                    q++;
                if (*q != '"')
                    return NULL;
                q++;
                size_t i = 0;
                while (*q && *q != '"' && i + 1 < out_size) {
                    if (*q == '\\' && q[1])
                        q++;
                    out[i++] = *q++;
                }
                out[i] = '\0';
                return out;
            }
            in_string = true;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            if (depth > 0)
                depth--;
        }
    }
    return NULL;
}

static bool json_has_top_level_key(const char *json, const char *key)
{
    char needle[64];
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    if (!json || !key)
        return false;
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    for (const char *p = json; *p; p++) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }

        if (*p == '"') {
            if (depth == 1 && strncmp(p, needle, strlen(needle)) == 0) {
                const char *q = p + strlen(needle);
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
                    q++;
                if (*q == ':')
                    return true;
            }
            in_string = true;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}') {
            if (depth > 0)
                depth--;
        }
    }
    return false;
}

static void json_collect_payload_keys(const char *json, char *out, size_t out_size)
{
    const char *payload;
    const char *p;
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    bool expect_key = false;
    size_t used = 0;

    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!json)
        return;
    payload = strstr(json, "\"payload\"");
    if (!payload)
        return;
    payload = strchr(payload, ':');
    if (!payload)
        return;
    payload++;
    while (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n')
        payload++;
    if (*payload != '{')
        return;

    for (p = payload; *p; p++) {
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (*p == '\\') {
                escape = true;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }
        if (*p == '{') {
            depth++;
            expect_key = depth == 1;
            continue;
        }
        if (*p == '}') {
            if (depth > 0)
                depth--;
            if (depth == 0)
                break;
            continue;
        }
        if (*p == ',') {
            expect_key = depth == 1;
            continue;
        }
        if (depth == 1 && expect_key && *p == '"') {
            char key[48];
            size_t k = 0;
            const char *q = p + 1;
            while (*q && *q != '"' && k + 1 < sizeof(key)) {
                if (*q == '\\' && q[1])
                    q++;
                key[k++] = *q++;
            }
            key[k] = '\0';
            if (key[0] && used < out_size) {
                size_t key_len = strlen(key);
                size_t separator_len = used ? 1U : 0U;
                size_t available = out_size - used - 1U;

                if (separator_len <= available &&
                    key_len <= available - separator_len) {
                    if (separator_len)
                        out[used++] = ',';
                    memcpy(out + used, key, key_len);
                    used += key_len;
                    out[used] = '\0';
                }
            }
            expect_key = false;
        } else if (*p == '"') {
            in_string = true;
        }
    }
}

static void log_protocol_message(const char *direction, const char *json)
{
    char type[64] = "";
    char keys[256] = "";
    bool has_payload;

    if (!json)
        return;
    json_top_level_str_value(json, "type", type, sizeof(type));
    has_payload = json_has_top_level_key(json, "payload");
    if (has_payload)
        json_collect_payload_keys(json, keys, sizeof(keys));
    logf_line("info",
              "Digital Factory protocol %s type=%s len=%u payload=%d payload_keys=%s",
              direction ? direction : "?",
              type[0] ? type : "?",
              (unsigned)strlen(json),
              has_payload ? 1 : 0,
              keys[0] ? keys : "-");
}

static bool json_bool_value(const char *json, const char *key, bool *out)
{
    char needle[64];
    const char *p;

    if (!json || !key || !out)
        return false;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (!p)
        return false;
    p = strchr(p + strlen(needle), ':');
    if (!p)
        return false;
    p++;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static int publish_update_firmware_request(void *pub, const char *msg)
{
    static unsigned int update_trackcount = 0;
    mp_buf_t key = {0};
    mp_buf_t data = {0};
    char target[96];
    char cluster_printer_id[96] = "";
    char action_id[96] = "";
    bool schedule_update = false;
    bool has_action_id;
    int rc = -1;

    if (!json_str_value(msg, "cluster_printer_id", cluster_printer_id,
                        sizeof(cluster_printer_id)) ||
        !json_bool_value(msg, "schedule_update", &schedule_update))
        goto out;
    has_action_id = json_str_value(msg, "action_id", action_id,
                                   sizeof(action_id)) &&
                    df_is_guid(action_id);
    update_trackcount++;
    snprintf(target, sizeof(target), "%s@execute|D%u",
             DF_HANDLING_SERVICE, update_trackcount);

    if (mp_encode_rpc_key(&key, "connector", target) < 0)
        goto out;
    if (mp_put_u8(&data, 0x84) < 0 ||
        mp_put_str(&data, "tracker") < 0 || mp_put_u8(&data, 0) < 0 ||
        mp_put_str(&data, "instruction") < 0 ||
        mp_put_u8(&data, DF_INSTR_UPDATE_FIRMWARE) < 0 ||
        mp_put_str(&data, "data") < 0 ||
        mp_put_u8(&data, (uint8_t)(has_action_id ? 0x84 : 0x83)) < 0)
        goto out;
    if (has_action_id &&
        (mp_put_str(&data, "action_id") < 0 ||
         mp_put_str(&data, action_id) < 0))
        goto out;
    if (mp_put_str(&data, "action") < 0 ||
        mp_put_str(&data, "update_firmware") < 0 ||
        mp_put_str(&data, "cluster_printer_id") < 0 ||
        mp_put_str(&data, cluster_printer_id) < 0 ||
        mp_put_str(&data, "action_details") < 0 ||
        mp_put_u8(&data, 0x82) < 0 ||
        mp_put_str(&data, "type") < 0 ||
        mp_put_str(&data, "update_firmware") < 0 ||
        mp_put_str(&data, "value") < 0 ||
        mp_put_u8(&data, 0x81) < 0 ||
        mp_put_str(&data, "schedule_update") < 0 ||
        mp_put_bool(&data, schedule_update) < 0 ||
        mp_put_str(&data, "__class__") < 0 ||
        mp_put_str(&data, "DigitalFactoryRequest") < 0)
        goto out;

    if (zmq_send(pub, key.buf, key.len, ZMQ_SNDMORE) < 0 ||
        zmq_send(pub, data.buf, data.len, 0) < 0)
        goto out;
    rc = 0;

out:
    free(key.buf);
    free(data.buf);
    return rc;
}

static void build_connection_request(char *out, size_t out_size)
{
    char host_guid[64], host_name[64], version[32], ip[64], cluster_id[128];
    char safe_host_guid[128], safe_host_name[128], safe_version[64], safe_ip[128];
    char safe_cluster_id[256];
    char cluster_json[288];
    read_host_guid(host_guid, sizeof(host_guid));
    uci_get("system.@system[0].hostname", "Ultimaker", host_name, sizeof(host_name));
    if (strlen(host_name) < 3)
        snprintf(host_name, sizeof(host_name), "Ultimaker");
    uci_get("ultimaker.version.nr", DENEB_VERSION, version, sizeof(version));
    uci_get("ultimaker.option.cluster_id", "", cluster_id, sizeof(cluster_id));
    if (run_read_line("ip -4 addr show | awk '/inet / && $2 !~ /^127/ { sub(\"/.*\", \"\", $2); print $2; exit }'",
                      ip, sizeof(ip)) < 0)
        snprintf(ip, sizeof(ip), "127.0.0.1");
    if (ip[0] == '\0')
        snprintf(ip, sizeof(ip), "127.0.0.1");
    json_escape_string(host_guid, safe_host_guid, sizeof(safe_host_guid));
    json_escape_string(host_name, safe_host_name, sizeof(safe_host_name));
    json_escape_string(version, safe_version, sizeof(safe_version));
    json_escape_string(ip, safe_ip, sizeof(safe_ip));
    json_escape_string(cluster_id, safe_cluster_id, sizeof(safe_cluster_id));
    if (cluster_id[0])
        snprintf(cluster_json, sizeof(cluster_json),
                 "\"cluster_id\":\"%s\",", safe_cluster_id);
    else
        cluster_json[0] = '\0';
    snprintf(out, out_size,
             "{\"payload\":{\"capabilities\":[\"cluster_account_status\",\"connect_with_cluster_id\",\"status\",\"rename\",\"update_firmware\","
             "\"printer_faults\",\"print_job_action\","
             "\"print_job_action_response\"],%s\"host_guid\":\"%s\","
             "\"host_internal_ip\":\"%s\",\"host_name\":\"%s\",\"host_version\":\"%s\"},"
             "\"type\":\"connection_request\"}",
             cluster_json, safe_host_guid, safe_ip, safe_host_name, safe_version);
}

static void build_status_response(char *out, size_t out_size)
{
    char printers[4096];
    char jobs[8192];
    if (local_api_request("GET", "/cluster-api/v1/printers", NULL,
                          printers, sizeof(printers)) == 200 &&
        local_api_request("GET", "/cluster-api/v1/print_jobs", NULL,
                          jobs, sizeof(jobs)) == 200) {
        snprintf(out, out_size,
                 "{\"payload\":{\"print_jobs\":%s,\"printers\":%s,"
                 "\"recently_completed\":[]},\"type\":\"status_response\"}",
                 json_array_or_empty(jobs), json_array_or_empty(printers));
        return;
    }

    char host_guid[64], host_name[64], friendly_name[64], version[32], latest_version[32], update_status[32], material[48], nozzle[24], nozzle_id[32], ip[64], status[64];
    char safe_host_guid[128], safe_host_name[128], safe_friendly_name[128], safe_version[64], safe_ip[128];
    char safe_latest_version[64], safe_update_status[64];
    char safe_material[96], safe_nozzle[48], safe_material_type[64], safe_status[96];
    char material_guid_json[128];
    read_host_guid(host_guid, sizeof(host_guid));
    uci_get("system.@system[0].hostname", "Ultimaker", host_name, sizeof(host_name));
    if (strlen(host_name) < 3)
        snprintf(host_name, sizeof(host_name), "Ultimaker");
    uci_get("ultimaker.option.printer_name", host_name, friendly_name, sizeof(friendly_name));
    uci_get("ultimaker.version.nr", DENEB_VERSION, version, sizeof(version));
    uci_get("ultimaker.version.latest", " ", latest_version,
            sizeof(latest_version));
    if (strcmp(latest_version, " ") != 0 &&
        strcmp(latest_version, version) != 0)
        snprintf(update_status, sizeof(update_status), "update_available");
    else
        snprintf(update_status, sizeof(update_status), "up_to_date");
    uci_get("ultimaker.option.material_guid",
            "506c9f0d-e3aa-4bd4-b2d2-23e2425b1aa9", material,
            sizeof(material));
    uci_get("ultimaker.option.nozzle_size", "0.4", nozzle, sizeof(nozzle));
    normalize_nozzle_id(nozzle, nozzle_id, sizeof(nozzle_id));
    if (!df_is_guid(material))
        material[0] = '\0';
    if (run_read_line("ip -4 addr show | awk '/inet / && $2 !~ /^127/ { sub(\"/.*\", \"\", $2); print $2; exit }'",
                      ip, sizeof(ip)) < 0)
        snprintf(ip, sizeof(ip), "127.0.0.1");
    if (ip[0] == '\0')
        snprintf(ip, sizeof(ip), "127.0.0.1");
    if (local_api_request("GET", "/api/v1/printer/status", NULL,
                          status, sizeof(status)) != 200)
        snprintf(status, sizeof(status), "idle");
    status[strcspn(status, "\r\n \t")] = '\0';
    {
        const char *mapped_status = df_cluster_printer_status_label(status);
        if (mapped_status != status)
            snprintf(status, sizeof(status), "%s", mapped_status);
    }
    json_escape_string(host_guid, safe_host_guid, sizeof(safe_host_guid));
    json_escape_string(host_name, safe_host_name, sizeof(safe_host_name));
    json_escape_string(friendly_name, safe_friendly_name, sizeof(safe_friendly_name));
    json_escape_string(version, safe_version, sizeof(safe_version));
    json_escape_string(latest_version, safe_latest_version,
                       sizeof(safe_latest_version));
    json_escape_string(update_status, safe_update_status,
                       sizeof(safe_update_status));
    json_escape_string(ip, safe_ip, sizeof(safe_ip));
    json_escape_string(material, safe_material, sizeof(safe_material));
    json_escape_string(nozzle_id, safe_nozzle, sizeof(safe_nozzle));
    json_escape_string(material_type_from_guid(material), safe_material_type,
                       sizeof(safe_material_type));
    json_escape_string(status, safe_status, sizeof(safe_status));
    if (safe_material[0])
        snprintf(material_guid_json, sizeof(material_guid_json),
                 "\"guid\":\"%s\",", safe_material);
    else
        material_guid_json[0] = '\0';
    snprintf(out, out_size,
             "{\"payload\":{\"print_jobs\":[],\"printers\":[{\"build_plate\":{\"type\":\"glass\"},"
             "\"configuration\":[{\"extruder_index\":0,\"material\":{\"brand\":\"Generic\","
             "\"color\":\"Generic\",%s\"material\":\"%s\"},"
             "\"print_core_id\":\"%s\"}],\"enabled\":true,"
             "\"faults\":[],\"firmware_update_status\":\"%s\","
             "\"firmware_version\":\"%s\",\"friendly_name\":\"%s\","
             "\"ip_address\":\"%s\",\"latest_available_firmware\":\"%s\","
             "\"machine_variant\":\"Ultimaker 2+ Connect\",\"status\":\"%s\","
             "\"unique_name\":\"%s\",\"uuid\":\"%s\"}],\"recently_completed\":[]},"
             "\"type\":\"status_response\"}",
             material_guid_json, safe_material_type, safe_nozzle,
             safe_update_status,
             safe_version, safe_friendly_name, safe_ip, safe_latest_version,
             safe_status,
             safe_host_name, safe_host_guid);
}

static void handle_print_job_action_request(ws_client_t *ws, const char *msg)
{
    char cluster_job_id[96] = "";
    char action_id[96] = "";
    char action[32] = "";
    char safe_cluster_job_id[192];
    char safe_action_id[192];
    char body[64];
    char api_body[512];
    const char *status = "failed";

    json_str_value(msg, "cluster_job_id", cluster_job_id, sizeof(cluster_job_id));
    json_str_value(msg, "action_id", action_id, sizeof(action_id));
    if (action_id[0] && !df_is_guid(action_id))
        action_id[0] = '\0';
    json_str_value(msg, "action", action, sizeof(action));
    json_escape_string(cluster_job_id, safe_cluster_job_id,
                       sizeof(safe_cluster_job_id));
    json_escape_string(action_id, safe_action_id, sizeof(safe_action_id));

    if (is_supported_print_job_action(action)) {
        snprintf(body, sizeof(body), "%s", local_print_job_action(action));
        int code = local_api_request("PUT", "/cluster-api/v1/print_jobs/0/action",
                                     body, api_body, sizeof(api_body));
        if (code >= 200 && code < 300)
            status = "success";
    }

    if (action_id[0]) {
        snprintf(api_body, sizeof(api_body),
                 "{\"payload\":{\"cluster_job_id\":\"%s\",\"action_id\":\"%s\","
                 "\"status\":\"%s\"},\"type\":\"print_job_action_response\"}",
                 safe_cluster_job_id, safe_action_id, status);
    } else {
        snprintf(api_body, sizeof(api_body),
                 "{\"payload\":{\"cluster_job_id\":\"%s\",\"status\":\"%s\"},"
                 "\"type\":\"print_job_action_response\"}",
                 safe_cluster_job_id, status);
    }
    log_protocol_message("send", api_body);
    (void)ws_send_text(ws, api_body);
}

static void handle_print_request(ws_client_t *ws, const char *msg)
{
    char job_id[96] = "";
    char job_instance_uuid[96] = "";
    char job_name[160] = "";
    char download_url[4096] = "";
    char material_guid[96] = "";
    char print_core_id[32] = "";
    char file_name[192];
    char upload_name[192];
    char path[256];
    char upload_path[256];
    char api_body[1024] = "";
    char response[512];
    char safe_job_id[192];
    char validation_error[256] = "";
    const char *status = "failed";
    const char *reason = "unknown";
    int code = -1;
    int download_rc = -1;
    int extract_rc = 0;
    bool has_download_url = false;

    json_str_value(msg, "job_id", job_id, sizeof(job_id));
    json_str_value(msg, "job_instance_uuid", job_instance_uuid,
                   sizeof(job_instance_uuid));
    json_str_value(msg, "job_name", job_name, sizeof(job_name));
    has_download_url = json_str_value(msg, "download_url", download_url,
                                      sizeof(download_url)) != NULL;
    json_str_value(msg, "guid", material_guid, sizeof(material_guid));
    if (!df_is_guid(material_guid))
        material_guid[0] = '\0';
    json_str_value(msg, "print_core_id", print_core_id, sizeof(print_core_id));

    sanitize_filename(job_name, file_name, sizeof(file_name));
    snprintf(upload_name, sizeof(upload_name), "%s", file_name);
    snprintf(path, sizeof(path), "%s/deneb-df-%lld-%s", DF_DOWNLOAD_DIR,
             (long long)time(NULL), file_name);
    snprintf(upload_path, sizeof(upload_path), "%s", path);

    if (!df_printer_is_idle()) {
        reason = "printer_not_idle";
    } else if (!job_id[0]) {
        reason = "missing_job_id";
    } else if (!job_name[0]) {
        reason = "missing_job_name";
    } else if (!job_instance_uuid[0]) {
        reason = "missing_job_instance_uuid";
    } else if (!df_is_guid(job_instance_uuid)) {
        reason = "invalid_job_instance_uuid";
    } else if (!has_download_url || !download_url[0]) {
        reason = download_url[0] ? "download_url_too_long" :
                                   "missing_download_url";
    } else {
        download_rc = download_file_with_wget(download_url, path);
        if (download_rc != 0) {
            reason = "download_failed";
        } else {
            if (filename_ends_with_ci(file_name, ".ufp")) {
                replace_file_extension(file_name, ".gcode", upload_name,
                                       sizeof(upload_name));
                snprintf(upload_path, sizeof(upload_path),
                         "%s/deneb-df-%lld-%s", DF_DOWNLOAD_DIR,
                         (long long)time(NULL), upload_name);
                extract_rc = extract_ufp_model_gcode(path, upload_path);
            }
            if (extract_rc != 0) {
                reason = "ufp_extract_failed";
            } else if (prepend_file_metadata(upload_path, material_guid,
                                             print_core_id) != 0) {
                reason = "metadata_prepend_failed";
            } else if (deneb_print_job_file_validate_build_volume_path(
                           upload_path, validation_error,
                           sizeof(validation_error)) != 0) {
                reason = "build_volume_validation_failed";
            } else {
                code = local_api_upload_file(upload_path, upload_name,
                                             job_instance_uuid, job_id,
                                             api_body, sizeof(api_body));
                if (code >= 200 && code < 300) {
                    reason = "queued";
                    status = "queued";
                } else {
                    reason = "local_upload_failed";
                }
            }
        }
    }
    logf_line("info",
              "Digital Factory print request result status=%s reason=%s "
              "api_code=%d download_rc=%d extract_rc=%d job_id=%d job_name=%d "
              "job_instance_uuid=%d download_url=%d download_url_len=%zu "
              "material_guid=%d print_core_id=%d file=%s upload_file=%s "
              "validation=%.160s api_body=%.180s",
              status, reason, code, download_rc, extract_rc,
              job_id[0] ? 1 : 0,
              job_name[0] ? 1 : 0, job_instance_uuid[0] ? 1 : 0,
              has_download_url && download_url[0] ? 1 : 0,
              strlen(download_url), material_guid[0] ? 1 : 0,
              print_core_id[0] ? 1 : 0, file_name, upload_name,
              validation_error[0] ? validation_error : "-",
              api_body[0] ? api_body : "-");
    unlink(path);
    if (strcmp(upload_path, path) != 0)
        unlink(upload_path);
    json_escape_string(job_id, safe_job_id, sizeof(safe_job_id));
    snprintf(response, sizeof(response),
             "{\"payload\":{\"job_id\":\"%s\",\"status\":\"%s\"},"
             "\"type\":\"print_response\"}",
             safe_job_id, status);
    log_protocol_message("send", response);
    (void)ws_send_text(ws, response);
}

static void handle_printer_action_request(void *pub, ws_client_t *ws,
                                          const char *msg)
{
    char cluster_printer_id[96] = "";
    char action_id[96] = "";
    char action[48] = "";
    char name[96] = "";
    char safe_cluster_printer_id[192];
    char safe_action_id[192];
    const char *status = "failed";
    char response[512];

    json_str_value(msg, "cluster_printer_id", cluster_printer_id,
                   sizeof(cluster_printer_id));
    json_str_value(msg, "action_id", action_id, sizeof(action_id));
    if (action_id[0] && !df_is_guid(action_id))
        action_id[0] = '\0';
    json_str_value(msg, "action", action, sizeof(action));

    if (strcmp(action, "rename") == 0 &&
        json_str_value(msg, "name", name, sizeof(name)) &&
        set_printer_name(name) == 0) {
        status = "success";
    } else if (strcmp(action, "update_firmware") == 0 &&
               df_printer_is_idle() &&
               publish_update_firmware_request(pub, msg) == 0) {
        status = "success";
    }
    json_escape_string(cluster_printer_id, safe_cluster_printer_id,
                       sizeof(safe_cluster_printer_id));
    json_escape_string(action_id, safe_action_id, sizeof(safe_action_id));

    if (action_id[0]) {
        snprintf(response, sizeof(response),
                 "{\"payload\":{\"cluster_printer_id\":\"%s\",\"action_id\":\"%s\","
                 "\"status\":\"%s\"},\"type\":\"printer_action_response\"}",
                 safe_cluster_printer_id, safe_action_id, status);
    } else {
        snprintf(response, sizeof(response),
                 "{\"payload\":{\"cluster_printer_id\":\"%s\",\"status\":\"%s\"},"
                 "\"type\":\"printer_action_response\"}",
                 safe_cluster_printer_id, status);
    }
    log_protocol_message("send", response);
    (void)ws_send_text(ws, response);
}

static void handle_cluster_account_status(void *pub, ws_client_t *ws,
                                          const char *msg,
                                          int *current_state,
                                          char *current_pin,
                                          size_t current_pin_size,
                                          int64_t *last_status_request,
                                          bool *want_connect)
{
    char action_id[128] = "";
    char user_id[128] = "";
    char organization_id[128] = "";
    char safe_action_id[256];
    char response[384];
    bool has_action_id = json_str_value(msg, "action_id", action_id, sizeof(action_id)) != NULL;
    bool has_user_id = json_str_value(msg, "user_id", user_id, sizeof(user_id)) != NULL &&
                       user_id[0] != '\0';

    (void)json_str_value(msg, "organization_id", organization_id, sizeof(organization_id));
    logf_line("info",
              "Digital Factory account status received action_id=%d user_id=%d organization_id=%d state=%d",
              has_action_id ? 1 : 0, has_user_id ? 1 : 0,
              organization_id[0] ? 1 : 0, *current_state);

    if (has_action_id) {
        json_escape_string(action_id, safe_action_id, sizeof(safe_action_id));
        snprintf(response, sizeof(response),
                 "{\"payload\":{\"action_id\":\"%s\",\"status\":\"success\"},"
                 "\"type\":\"cluster_account_status_confirm\"}",
                 safe_action_id);
        log_protocol_message("send", response);
        (void)ws_send_text(ws, response);
    } else {
        log_line("warn", "Digital Factory account status omitted action_id; confirm not sent");
    }

    if (has_user_id) {
        *current_state = DF_STATE_CONNECTED;
        if (current_pin_size > 0)
            current_pin[0] = '\0';
        if (last_status_request)
            *last_status_request = monotonic_ms();
        clear_pair_request();
        log_line("info", "Digital Factory cloud account confirmed");
        publish_status(pub, DF_STATE_CONNECTED, "");
    } else if (*current_state != DF_STATE_ENTER_PIN) {
        *current_state = DF_STATE_DISCONNECTED;
        if (current_pin_size > 0)
            current_pin[0] = '\0';
        if (last_status_request)
            *last_status_request = 0;
        if (want_connect)
            *want_connect = false;
        uci_set_option("cluster_id", NULL);
        digital_factory_disable_service();
        clear_pair_request();
        log_line("info", "Digital Factory cloud account removed");
        publish_status(pub, DF_STATE_DISCONNECTED, "");
        g_stop = 1;
    }
}

static void handle_cloud_message(void *pub, ws_client_t *ws, const char *msg,
                                 int *current_state, char *current_pin,
                                 size_t current_pin_size,
                                 int64_t *last_status_request,
                                 bool *want_connect,
                                 bool pairing_requested)
{
    log_protocol_message("recv", msg);

    if (strstr(msg, "\"type\":\"connection_response\"") ||
        strstr(msg, "\"type\": \"connection_response\"")) {
        char cluster_id[160] = "";
        char pin[64] = "";
        bool has_payload = json_has_top_level_key(msg, "payload");
        bool has_pin_key = strstr(msg, "\"pin_code\"") != NULL;
        bool has_cluster_key = strstr(msg, "\"cluster_id\"") != NULL;
        logf_line("info",
                  "Digital Factory connection response received payload=%d pin_key=%d cluster_key=%d state=%d pairing=%d",
                  has_payload ? 1 : 0, has_pin_key ? 1 : 0,
                  has_cluster_key ? 1 : 0, *current_state,
                  pairing_requested ? 1 : 0);
        if (json_str_value(msg, "pin_code", pin, sizeof(pin))) {
            if (!pairing_requested) {
                uci_set_option("cluster_id", NULL);
                digital_factory_disable_service();
                clear_pair_request();
                if (want_connect)
                    *want_connect = false;
                *current_state = DF_STATE_DISCONNECTED;
                if (current_pin_size > 0)
                    current_pin[0] = '\0';
                log_line("info", "Digital Factory pairing PIN ignored without pairing request");
                publish_status(pub, DF_STATE_DISCONNECTED, "");
                return;
            }
            if (json_str_value(msg, "cluster_id", cluster_id, sizeof(cluster_id)))
                uci_set_option("cluster_id", cluster_id);
            clear_pair_request();
            *current_state = DF_STATE_ENTER_PIN;
            snprintf(current_pin, current_pin_size, "%s", pin);
            log_line("info", "Digital Factory pairing PIN received");
            publish_status(pub, DF_STATE_ENTER_PIN, pin);
        } else if (has_payload) {
            if (json_str_value(msg, "cluster_id", cluster_id, sizeof(cluster_id)))
                uci_set_option("cluster_id", cluster_id);
            clear_pair_request();
            *current_state = DF_STATE_ENTER_PIN;
            if (current_pin_size > 0)
                current_pin[0] = '\0';
            log_line("info", "Digital Factory pairing response received");
            publish_status(pub, DF_STATE_ENTER_PIN, "");
        } else if (!has_payload) {
            *current_state = DF_STATE_CONNECTED;
            if (current_pin_size > 0)
                current_pin[0] = '\0';
            if (last_status_request)
                *last_status_request = monotonic_ms();
            log_line("info", "Digital Factory connection established");
            publish_status(pub, DF_STATE_CONNECTED, "");
        }
    } else if (strstr(msg, "\"type\":\"status_request\"") ||
               strstr(msg, "\"type\": \"status_request\"")) {
        char out[8192];
        bool has_payload = json_has_top_level_key(msg, "payload");
        logf_line("info", "Digital Factory status request received payload=%d state=%d pin=%d",
                  has_payload ? 1 : 0, *current_state, current_pin[0] ? 1 : 0);
        if (has_payload)
            return;
        if (last_status_request && *current_state == DF_STATE_CONNECTED)
            *last_status_request = monotonic_ms();
        build_status_response(out, sizeof(out));
        log_protocol_message("send", out);
        (void)ws_send_text(ws, out);
    } else if (strstr(msg, "\"type\":\"print_request\"") ||
               strstr(msg, "\"type\": \"print_request\"")) {
        handle_print_request(ws, msg);
    } else if (strstr(msg, "\"type\":\"print_job_action_request\"") ||
               strstr(msg, "\"type\": \"print_job_action_request\"")) {
        handle_print_job_action_request(ws, msg);
    } else if (strstr(msg, "\"type\":\"printer_action_request\"") ||
               strstr(msg, "\"type\": \"printer_action_request\"")) {
        handle_printer_action_request(pub, ws, msg);
    } else if (strstr(msg, "\"type\":\"cluster_account_status\"") ||
               strstr(msg, "\"type\": \"cluster_account_status\"")) {
        handle_cluster_account_status(pub, ws, msg, current_state, current_pin,
                                      current_pin_size, last_status_request,
                                      want_connect);
    }
}

static bool poll_connector_commands(void *sub, bool *want_connect,
                                    bool *connect_requested)
{
    bool handled = false;
    if (connect_requested)
        *connect_requested = false;
    while (true) {
        zmq_pollitem_t items[] = {{sub, 0, ZMQ_POLLIN, 0}};
        if (zmq_poll(items, 1, 0) <= 0)
            break;
        zmq_msg_t key_msg, data_msg;
        zmq_msg_init(&key_msg);
        zmq_msg_init(&data_msg);
        if (zmq_msg_recv(&key_msg, sub, 0) < 0) {
            zmq_msg_close(&key_msg);
            zmq_msg_close(&data_msg);
            break;
        }
        if (zmq_msg_more(&key_msg))
            (void)zmq_msg_recv(&data_msg, sub, 0);
        char action[32] = "", source[128] = "", target[160] = "";
        if (read_key_parts(&key_msg, action, sizeof(action), source, sizeof(source),
                           target, sizeof(target)) == 0 &&
            strcmp(action, "rpc-request") == 0) {
            if (strncmp(target, CONNECT_SERVICE, strlen(CONNECT_SERVICE)) == 0) {
                *want_connect = true;
                if (connect_requested)
                    *connect_requested = true;
                handled = true;
            } else if (strncmp(target, DISCONNECT_SERVICE, strlen(DISCONNECT_SERVICE)) == 0) {
                *want_connect = false;
                uci_set_option("cluster_id", NULL);
                handled = true;
            }
        }
        zmq_msg_close(&key_msg);
        zmq_msg_close(&data_msg);
    }
    return handled;
}

int main(void)
{
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    log_line("info", "starting native Digital Factory connector");

    void *ctx = zmq_ctx_new();
    void *pub = zmq_socket(ctx, ZMQ_PUB);
    void *sub = zmq_socket(ctx, ZMQ_SUB);
    int linger = 0;
    zmq_setsockopt(pub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(sub, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0);
    char pub_url[64];
    snprintf(pub_url, sizeof(pub_url), "%s%d", IPC_BASE, DF_PUB_PORT);
    if (zmq_bind(pub, pub_url) != 0) {
        log_line("error", "failed to bind ZMQ publisher");
        return 1;
    }
    for (int offset = 0; offset < 4; offset++) {
        char sub_url[64];
        snprintf(sub_url, sizeof(sub_url), "%s%d", IPC_BASE, GERSHWIN_PUB_BASE + offset);
        zmq_connect(sub, sub_url);
    }
    sleep(1);

    char cluster_id[160];
    uci_get("ultimaker.option.cluster_id", "", cluster_id, sizeof(cluster_id));
    bool want_connect = cluster_id[0] != '\0';
    int current_state = want_connect ? DF_STATE_RECONNECTING : DF_STATE_DISCONNECTED;
    char current_pin[64] = "";
    int64_t next_status_publish = 0;
    int reconnect_wait_seconds = 1;
    bool pairing_requested = false;
    if (pair_request_is_fresh())
        pairing_requested = true;
    publish_status(pub, current_state, current_pin);

    while (!g_stop) {
        bool connect_requested = false;
        poll_connector_commands(sub, &want_connect, &connect_requested);
        if (connect_requested) {
            pairing_requested = true;
            create_pair_request();
        }
        if (!want_connect) {
            current_state = DF_STATE_DISCONNECTED;
            current_pin[0] = '\0';
            publish_status(pub, current_state, current_pin);
            sleep(1);
            continue;
        }

        char url_s[256];
        df_url_t url;
        uci_get("ultimaker.option.digitalfactory_url",
                "wss://api.ultimaker.com/gateway/v1/socket",
                url_s, sizeof(url_s));
        if (parse_url(url_s, &url) < 0) {
            log_line("error", "invalid digitalfactory_url");
            sleep(10);
            continue;
        }

        current_state = DF_STATE_RECONNECTING;
        current_pin[0] = '\0';
        publish_status(pub, current_state, current_pin);
        ws_client_t ws;
        if (ws_connect(&ws, &url) < 0) {
            log_line("warn", "cloud websocket connect failed");
            ws_free(&ws);
            current_state = DF_STATE_RECONNECTING;
            current_pin[0] = '\0';
            publish_status(pub, current_state, current_pin);
            sleep(reconnect_wait_seconds);
            reconnect_wait_seconds *= 2;
            if (reconnect_wait_seconds > 120)
                reconnect_wait_seconds = 120;
            continue;
        }
        reconnect_wait_seconds = 1;
        char req[1024];
        build_connection_request(req, sizeof(req));
        log_protocol_message("send", req);
        (void)ws_send_text(&ws, req);
        int64_t next_ping = monotonic_ms() + DF_WS_PING_INTERVAL_MS;
        int64_t last_rx = monotonic_ms();
        int64_t last_status_request = 0;

        bool reconnect_requested = false;
        while (!g_stop && want_connect) {
            bool connect_requested = false;
            poll_connector_commands(sub, &want_connect, &connect_requested);
            if (connect_requested) {
                pairing_requested = true;
                create_pair_request();
                reconnect_requested = true;
                break;
            }
            int64_t now = monotonic_ms();
            if (current_state == DF_STATE_CONNECTED && last_status_request > 0 &&
                now - last_status_request > DF_STATUS_REQUEST_STALE_MS) {
                current_state = DF_STATE_RECONNECTING;
                current_pin[0] = '\0';
                last_status_request = 0;
                log_line("info", "Digital Factory cloud status is stale");
                publish_status(pub, current_state, current_pin);
            }
            if (now >= next_status_publish) {
                publish_status(pub, current_state, current_pin);
                next_status_publish = now + 5000;
            }
            if (now >= next_ping) {
                if (ws_send_control(&ws, 0x9, NULL, 0) < 0) {
                    log_line("warn", "Digital Factory websocket ping failed");
                    break;
                }
                next_ping = now + DF_WS_PING_INTERVAL_MS;
            }
            if (now - last_rx > DF_WS_PING_TIMEOUT_MS) {
                logf_line("warn",
                          "Digital Factory websocket receive timeout state=%d",
                          current_state);
                break;
            }
            int ready = ws_wait_readable(&ws, 1000);
            if (ready < 0) {
                logf_line("warn", "Digital Factory websocket wait failed state=%d",
                          current_state);
                break;
            }
            if (ready == 0)
                continue;
            char msg[8192];
            int rc = ws_read_text(&ws, msg, sizeof(msg));
            if (rc < 0) {
                logf_line("warn", "Digital Factory websocket read failed state=%d",
                          current_state);
                break;
            }
            last_rx = monotonic_ms();
            if (rc == 0)
                handle_cloud_message(pub, &ws, msg, &current_state,
                                     current_pin, sizeof(current_pin),
                                     &last_status_request, &want_connect,
                                     pairing_requested);
        }
        if (!want_connect || g_stop)
            ws_send_close(&ws);
        if (want_connect && !reconnect_requested) {
            current_state = DF_STATE_DISCONNECTED;
            current_pin[0] = '\0';
            publish_status(pub, current_state, current_pin);
        }
        ws_free(&ws);
        if (want_connect && reconnect_requested) {
            continue;
        } else if (want_connect) {
            current_state = DF_STATE_RECONNECTING;
            current_pin[0] = '\0';
            publish_status(pub, current_state, current_pin);
            sleep(reconnect_wait_seconds);
            reconnect_wait_seconds *= 2;
            if (reconnect_wait_seconds > 120)
                reconnect_wait_seconds = 120;
        }
    }

    publish_status(pub, DF_STATE_DISCONNECTED, "");
    zmq_close(sub);
    zmq_close(pub);
    zmq_ctx_destroy(ctx);
    log_line("info", "stopped");
    return 0;
}
