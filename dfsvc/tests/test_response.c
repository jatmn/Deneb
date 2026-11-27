/* SPDX-License-Identifier: MPL-2.0 */
#include <mbedtls/ssl.h>
#include <psa/crypto.h>

static int fixture_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len);
static psa_status_t fixture_generate_random(uint8_t *out, size_t len);

#define mbedtls_ssl_write fixture_ssl_write
#define psa_generate_random fixture_generate_random
#define API_SOCKET_PATH "api.sock"
#define main deneb_connector_main
#include "../src/main.c"
#undef main
#undef psa_generate_random
#undef mbedtls_ssl_write
#include <assert.h>

#define PRINTERS "/cluster-api/v1/printers"
#define JOBS "/cluster-api/v1/print_jobs"
#define STATUS "/api/v1/printer/status"
#define ACTION "/cluster-api/v1/print_jobs/0/action"

typedef struct {
    const char *method;
    const char *path;
    int status;
    const char *body;
    int length_delta;
    bool close_delimited;
} local_reply_t;

static pid_t serve_replies(const local_reply_t *replies, size_t count)
{
    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = {.sun_family = AF_UNIX, .sun_path = API_SOCKET_PATH};
    assert(server >= 0 && bind(server, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(server, 1) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid != 0) {
        close(server);
        return pid;
    }
    alarm(6);
    for (size_t i = 0; i < count; i++) {
        const local_reply_t *reply = &replies[i];
        int fd = accept(server, NULL, NULL);
        assert(fd >= 0);
        char request[1024] = "";
        size_t used = 0;
        while (!strstr(request, "\r\n\r\n")) {
            ssize_t n = read(fd, request + used, sizeof(request) - 1 - used);
            assert(n > 0);
            used += (size_t)n;
            request[used] = '\0';
        }
        char start[128];
        snprintf(start, sizeof(start), "%s %s HTTP/1.1\r\n", reply->method, reply->path);
        assert(strncmp(request, start, strlen(start)) == 0);
        if (strcmp(reply->method, "PUT") == 0) {
            char *body = strstr(request, "\r\n\r\n") + 4;
            size_t got = strlen(body);
            assert(got <= 6);
            while (got < 6) {
                ssize_t n = read(fd, body + got, 6 - got);
                assert(n > 0);
                got += (size_t)n;
            }
            assert(memcmp(body, "cancel", 6) == 0);
        }

        char header[1024];
        int n = snprintf(header, sizeof(header), "HTTP/1.1 %d Fixture\r\n"
                         "Connection: close\r\nContent-Type: application/json\r\n",
                         reply->status);
        size_t body_size = strlen(reply->body);
        if (!reply->close_delimited)
            n += snprintf(header + n, sizeof(header) - (size_t)n, "cOnTeNt-LeNgTh: %zu\r\n",
                          body_size + (size_t)reply->length_delta);
        assert(n > 0 && (size_t)n < sizeof(header));
        assert(socket_write_all(fd, header, (size_t)n) == 0);
        usleep(1000);
        assert(socket_write_all(fd, "\r\n", 2) == 0);
        /* A declared oversize can be rejected before the peer sends its body. */
        (void)socket_write_all(fd, reply->body, body_size);
        close(fd);
    }
    close(server);
    _exit(0);
}

static void finish_replies(pid_t pid)
{
    int status;
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    assert(unlink(API_SOCKET_PATH) == 0);
}

static void test_local_http(void)
{
    const local_reply_t replies[] = {
        {"GET", STATUS, 200, "\"idle\"", 0, false},
        {"PUT", ACTION, 409, "conflict", 0, false},
        {"GET", STATUS, 204, "", 0, false},
        {"GET", STATUS, 200, "1234567", 0, true},
        {"GET", STATUS, 200, "12345678", 0, true},
        {"GET", STATUS, 200, "idle", 8, false}
    };
    pid_t pid = serve_replies(replies, sizeof(replies) / sizeof(replies[0]));
    char out[64];
    assert(local_api_request("GET", STATUS, NULL, out, sizeof(out)) == 200);
    assert(strcmp(out, "\"idle\"") == 0);
    assert(local_api_request("PUT", ACTION, "cancel", out, sizeof(out)) == 409);
    assert(strcmp(out, "conflict") == 0);
    assert(local_api_request("GET", STATUS, NULL, out, 1) == 204 && !out[0]);
    assert(local_api_request("GET", STATUS, NULL, out, 8) == 200);
    assert(strcmp(out, "1234567") == 0);
    assert(local_api_request("GET", STATUS, NULL, out, 8) == -1 && !out[0]);
    assert(local_api_request("GET", STATUS, NULL, out, sizeof(out)) == -1 && !out[0]);
    finish_replies(pid);
}

static void test_coalesced_reply(bool close_writer)
{
    int pair[2];
    const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n\"idle\"";
    char headers[128];
    char out[64];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(fcntl(pair[1], F_SETFL, O_NONBLOCK) == 0);
    assert(socket_write_all(pair[0], response, sizeof(response) - 1) == 0);
    /* With the writer open, the EOF check must see EAGAIN. */
    if (close_writer)
        assert(shutdown(pair[0], SHUT_WR) == 0);
    int status = local_api_read_response(pair[1], headers, sizeof(headers), out, sizeof(out));
    assert(status == (close_writer ? 200 : -1));
    if (close_writer)
        assert(strcmp(out, "\"idle\"") == 0);
    close(pair[0]);
    close(pair[1]);
}

static void make_array(char *out, size_t length, char fill)
{
    out[0] = '[';
    out[1] = '"';
    memset(out + 2, fill, length - 4);
    memcpy(out + length - 2, "\"]", 3);
}

static void test_status_json(void)
{
    const char format[] = "{\"payload\":{\"print_jobs\":%s,\"printers\":%s,"
                          "\"recently_completed\":[]},\"type\":\"status_response\"}";
    const local_reply_t shape[] = {
        {"GET", PRINTERS, 200, " \t[1]", 0, false},
        {"GET", JOBS, 200, "null", 0, false},
    };
    char out[8192], expected[8192], printers[4097], jobs[8192];
    pid_t pid = serve_replies(shape, sizeof(shape) / sizeof(shape[0]));
    assert(build_status_response(out, sizeof(out)));
    assert(snprintf(expected, sizeof(expected), format, "[]", "[1]") > 0);
    assert(strcmp(out, expected) == 0);
    finish_replies(pid);

    size_t exact_jobs = sizeof(out) - 1 - 4095 -
                        (size_t)snprintf(expected, sizeof(expected), format, "", "");
    make_array(printers, 4095, 'p');
    make_array(jobs, exact_jobs, 'j');
    const local_reply_t large[] = {
        {"GET", PRINTERS, 200, printers, 0, false},
        {"GET", JOBS, 200, jobs, 0, false},
        {"GET", STATUS, 200, "paused", 0, false},
    };
    pid = serve_replies(large, 2);
    assert(build_status_response(out, sizeof(out)));
    assert(snprintf(expected, sizeof(expected), format, jobs, printers) == sizeof(out) - 1);
    assert(strcmp(out, expected) == 0);
    finish_replies(pid);

    make_array(jobs, exact_jobs + 1, 'j');
    pid = serve_replies(large, 3);
    assert(build_status_response(out, sizeof(out)));
    assert(strstr(out, "\"status\":\"printing\""));
    assert(strstr(out, "\"print_jobs\":[]"));
    finish_replies(pid);

    make_array(printers, 4096, 'p');
    const local_reply_t oversized_printers[] = {
        {"GET", PRINTERS, 200, printers, 0, false},
        {"GET", STATUS, 503, "unavailable", 0, false},
    };
    pid = serve_replies(oversized_printers, 2);
    assert(build_status_response(out, sizeof(out)));
    assert(strstr(out, "\"status\":\"idle\""));
    finish_replies(pid);
}

static uint8_t captured[65535 + 8];
static size_t capture_size, largest_write, fail_after;
static unsigned masks;

static int fixture_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len)
{
    (void)ssl;
    if (len > largest_write)
        largest_write = len;
    if (capture_size == fail_after)
        return MBEDTLS_ERR_NET_SEND_FAILED;
    size_t count = len < 13 ? len : 13;
    if (count > fail_after - capture_size)
        count = fail_after - capture_size;
    assert(count <= sizeof(captured) - capture_size);
    memcpy(captured + capture_size, buf, count);
    capture_size += count;
    return (int)count;
}

static psa_status_t fixture_generate_random(uint8_t *out, size_t len)
{
    assert(len == 4);
    ++masks;
    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t)(17 * masks + 41 * i);
    return PSA_SUCCESS;
}

static void test_masked_frames(void)
{
    ws_client_t ws = {0};
    static char text[65537];
    const size_t lengths[] = {0, 125, 126, 8191, 65535};
    fail_after = sizeof(captured);
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        size_t length = lengths[i];
        for (size_t j = 0; j < length; j++)
            text[j] = (char)('a' + j % 26);
        text[length] = '\0';
        capture_size = largest_write = 0;
        assert(ws_send_text(&ws, text) == 0);
        size_t mask_offset = length < 126 ? 2 : 4;
        assert(captured[0] == 0x81 && (captured[1] & 0x80));
        assert((captured[1] & 0x7f) == (length < 126 ? length : 126));
        if (length >= 126)
            assert(((size_t)captured[2] << 8 | captured[3]) == length);
        assert(capture_size == length + mask_offset + 4 && largest_write <= 1024);
        assert(masks == i + 1 && captured[mask_offset] == (uint8_t)(17 * masks));
        for (size_t j = 0; j < length; j++)
            assert((captured[mask_offset + 4 + j] ^ captured[mask_offset + j % 4]) ==
                   (uint8_t)text[j]);
    }

    text[65535] = 'x';
    capture_size = 0;
    assert(ws_send_text(&ws, text) == -1 && !capture_size && masks == 5);
    text[8191] = '\0';
    fail_after = 1555;
    assert(ws_send_text(&ws, text) == -1 && capture_size == fail_after && masks == 6);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char dir[256];
    int n = snprintf(dir, sizeof(dir), "%s/deneb-df-response.XXXXXX", tmp ? tmp : "/tmp");
    assert(n > 0 && (size_t)n < sizeof(dir));
    assert(mkdtemp(dir) && chdir(dir) == 0);
    assert(setenv("PATH", "/nonexistent", 1) == 0); /* Use built-in UCI/IP fallbacks. */
    test_local_http();
    test_coalesced_reply(false);
    test_coalesced_reply(true);
    test_status_json();
    test_masked_frames();
    assert(chdir("/") == 0 && rmdir(dir) == 0);
    return 0;
}
