/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Auth endpoint implementations.
 * Full RFC 2617 HTTP Digest auth for Cura + session tokens for web UI.
 */

#include "api_auth.h"
#include "md5.h"
#include "json_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <crypt.h>

#define AUTH_CONFIG_PATH  "/etc/deneb/web-auth.conf"
#define MAX_TOKENS        8
#define TOKEN_LEN         64
#define MAX_NONCES        16
#define NONCE_EXPIRY_SEC  300  /* 5 minutes */
#define DIGEST_REALM      "deneb"

/* Stored session token */
typedef struct {
    char token[TOKEN_LEN];
    time_t expires;
} session_token_t;

/* Stored nonce for Digest auth */
typedef struct {
    char nonce[64];
    time_t created;
    int nc;  /* last used nonce count */
} digest_nonce_t;

static int auth_disabled = 0;
static int setup_complete = 0;

/* SHA-256 crypt hash for web UI login */
static char password_hash[128] = "";

/* MD5 HA1 for Digest auth: MD5(username:realm:password) */
static char digest_ha1[33] = "";
static char digest_username[64] = "operator";

static session_token_t tokens[MAX_TOKENS];
static int token_count = 0;

static digest_nonce_t nonces[MAX_NONCES];
static int nonce_count = 0;

static void generate_random_hex(char *out, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "api_auth: /dev/urandom unavailable!\n");
        for (size_t i = 0; i < len - 1; i++) out[i] = '0';
    } else {
        unsigned char buf[64];
        ssize_t r = read(fd, buf, sizeof(buf));
        close(fd);
        if (r < 0) r = 0;
        for (size_t i = 0; i < len - 1 && i < (size_t)r; i++)
            out[i] = "0123456789abcdef"[buf[i] & 0x0f];
    }
    out[len - 1] = '\0';
}

static void load_config(void)
{
    FILE *f = fopen(AUTH_CONFIG_PATH, "r");
    if (!f) { setup_complete = 0; return; }
    setup_complete = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (strncmp(line, "setup_complete=", 15) == 0)
            setup_complete = atoi(line + 15);
        else if (strncmp(line, "auth_disabled=", 14) == 0)
            auth_disabled = atoi(line + 14);
        else if (strncmp(line, "password=", 9) == 0)
            snprintf(password_hash, sizeof(password_hash), "%s", line + 9);
        else if (strncmp(line, "digest_ha1=", 11) == 0)
            snprintf(digest_ha1, sizeof(digest_ha1), "%s", line + 11);
        else if (strncmp(line, "digest_user=", 12) == 0)
            snprintf(digest_username, sizeof(digest_username), "%s", line + 12);
    }
    fclose(f);

    if (!setup_complete && (password_hash[0] != '\0' || auth_disabled)) {
        setup_complete = 1;
    }
}

static void save_config(void)
{
    int fd = open(AUTH_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); return; }
    fprintf(f, "setup_complete=%d\n", setup_complete);
    fprintf(f, "auth_disabled=%d\n", auth_disabled);
    fprintf(f, "password=%s\n", password_hash);
    fprintf(f, "digest_ha1=%s\n", digest_ha1);
    fprintf(f, "digest_user=%s\n", digest_username);
    fclose(f);
}

static digest_nonce_t *create_nonce(void)
{
    /* Evict expired or oldest */
    time_t now = time(NULL);
    for (int i = 0; i < nonce_count; i++) {
        if (now - nonces[i].created > NONCE_EXPIRY_SEC) {
            memmove(&nonces[i], &nonces[i+1],
                    sizeof(digest_nonce_t) * (size_t)(nonce_count - i - 1));
            nonce_count--;
            i--;
        }
    }
    if (nonce_count >= MAX_NONCES) {
        memmove(&nonces[0], &nonces[1], sizeof(digest_nonce_t) * (MAX_NONCES - 1));
        nonce_count = MAX_NONCES - 1;
    }

    digest_nonce_t *n = &nonces[nonce_count++];
    generate_random_hex(n->nonce, sizeof(n->nonce));
    n->created = now;
    n->nc = 0;
    return n;
}

static int validate_nonce(const char *nonce)
{
    time_t now = time(NULL);
    for (int i = 0; i < nonce_count; i++) {
        if (strcmp(nonces[i].nonce, nonce) == 0 &&
            now - nonces[i].created <= NONCE_EXPIRY_SEC) {
            return 1;
        }
    }
    return 0;
}

static void set_digest_credentials(const char *username, const char *password)
{
    snprintf(digest_username, sizeof(digest_username), "%s", username);

    md5_ctx_t ctx;
    uint8_t digest[16];
    md5_init(&ctx);
    md5_update(&ctx, digest_username, strlen(digest_username));
    md5_update(&ctx, ":", 1);
    md5_update(&ctx, DIGEST_REALM, strlen(DIGEST_REALM));
    md5_update(&ctx, ":", 1);
    md5_update(&ctx, password, strlen(password));
    md5_final(&ctx, digest);
    for (int i = 0; i < 16; i++) {
        digest_ha1[i*2]   = "0123456789abcdef"[digest[i] >> 4];
        digest_ha1[i*2+1] = "0123456789abcdef"[digest[i] & 0x0f];
    }
    digest_ha1[32] = '\0';
}

/* Extract a quoted field value from a string: field="value" */
static int extract_quoted(const char *haystack, const char *field, char *out, size_t out_sz)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=\"", field);
    const char *p = strstr(haystack, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = strchr(p, '"');
    if (!end) return -1;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* Extract an unquoted field value: field=value */
static int extract_unquoted(const char *haystack, const char *field, char *out, size_t out_sz)
{
    char search[64];
    snprintf(search, sizeof(search), "%s=", field);
    const char *p = strstr(haystack, search);
    if (!p) return -1;
    p += strlen(search);
    const char *end = p;
    while (*end && *end != ',' && *end != ' ' && *end != '\r' && *end != '\n') end++;
    size_t len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

void api_auth_init(void)
{
    memset(tokens, 0, sizeof(tokens));
    memset(nonces, 0, sizeof(nonces));
    load_config();
}

/* POST /api/v1/auth/request — generate nonce for Digest auth */
void api_auth_request_post(const http_request_t *req, http_response_t *resp)
{
    if (!api_auth_is_disabled() && !api_http_check_auth(req)) {
        resp->status_code = 403;
        api_http_set_body_str(resp, "{\"message\":\"Pairing requires Open Access or an existing web session\"}");
        return;
    }

    char id[33];
    char key[33];
    generate_random_hex(id, sizeof(id));
    generate_random_hex(key, sizeof(key));
    set_digest_credentials(id, key);
    if (setup_complete) save_config();

    char buf[256];
    json_writer_t w;
    json_init(&w, buf, sizeof(buf));
    json_obj_open(&w);
    json_str(&w, "id", id);
    json_str(&w, "key", key);
    json_obj_close(&w);
    json_len(&w);
    api_http_set_body_str(resp, buf);
}

/* GET /api/v1/auth/check/{id} — check auth status */
void api_auth_check_get(const http_request_t *req, http_response_t *resp)
{
    const char *id = req->path + strlen("/api/v1/auth/check/");
    if (!*id) {
        resp->status_code = 400;
        api_http_set_body_str(resp, "{\"message\":\"Missing id\"}");
        return;
    }

    /* If auth is disabled or setup not done, always authorized */
    if (auth_disabled || !setup_complete) {
        api_http_set_body_str(resp, "{\"message\":\"authorized\"}");
        return;
    }

    if (strcmp(id, digest_username) == 0 && digest_ha1[0] != '\0') {
        api_http_set_body_str(resp, "{\"message\":\"authorized\"}");
        return;
    }

    /* Check if there's a valid session token in the request */
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "Authorization") == 0) {
            if (strncasecmp(req->headers[i].value, "Bearer ", 7) == 0) {
                if (api_auth_validate_token(req->headers[i].value + 7)) {
                    api_http_set_body_str(resp, "{\"message\":\"authorized\"}");
                    return;
                }
            }
        }
    }

    api_http_set_body_str(resp, "{\"message\":\"unauthorized\"}");
}

/* GET /api/v1/auth/verify — test Digest credentials */
void api_auth_verify_get(const http_request_t *req, http_response_t *resp)
{
    /* If we got here, auth was validated by api_http_check_auth */
    (void)req;
    api_http_set_body_str(resp, "{\"message\":\"ok\"}");
}

int api_auth_validate_token(const char *token)
{
    if (auth_disabled || !setup_complete) return 1;
    time_t now = time(NULL);
    for (int i = 0; i < token_count; i++) {
        if (strcmp(tokens[i].token, token) == 0 && tokens[i].expires > now)
            return 1;
    }
    return 0;
}

int api_auth_validate_digest(const char *digest_header, const char *method, const char *path)
{
    if (auth_disabled || !setup_complete) return 1;
    if (digest_ha1[0] == '\0') return 0;  /* no password set */

    /* Parse Digest header fields */
    char username[64], realm[64], nonce[64], uri[256], response[64];
    char qop[16], nc[16], cnonce[64];

    if (extract_quoted(digest_header, "username", username, sizeof(username)) < 0) return 0;
    if (extract_quoted(digest_header, "realm", realm, sizeof(realm)) < 0) return 0;
    if (extract_quoted(digest_header, "nonce", nonce, sizeof(nonce)) < 0) return 0;
    if (extract_quoted(digest_header, "uri", uri, sizeof(uri)) < 0) return 0;
    if (extract_quoted(digest_header, "response", response, sizeof(response)) < 0) return 0;

    /* qop, nc, cnonce are optional but required for auth-int/auth */
    int has_qop = (extract_quoted(digest_header, "qop", qop, sizeof(qop)) == 0 ||
                   extract_unquoted(digest_header, "qop", qop, sizeof(qop)) == 0);
    int has_nc = (extract_unquoted(digest_header, "nc", nc, sizeof(nc)) == 0);
    int has_cnonce = (extract_quoted(digest_header, "cnonce", cnonce, sizeof(cnonce)) == 0);

    /* Validate username matches */
    if (strcmp(username, digest_username) != 0) return 0;

    /* Validate nonce is still valid */
    if (!validate_nonce(nonce)) return 0;

    /* Compute HA2 = MD5(method:uri) */
    char ha2[33];
    md5_hex_concat(method, uri, ha2);

    /* Compute expected response */
    char expected[33];
    if (has_qop && has_nc && has_cnonce) {
        /* response = MD5(HA1:nonce:nc:cnonce:qop:HA2) */
        md5_ctx_t ctx;
        uint8_t digest[16];
        md5_init(&ctx);
        md5_update(&ctx, digest_ha1, 32);
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, nonce, strlen(nonce));
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, nc, strlen(nc));
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, cnonce, strlen(cnonce));
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, qop, strlen(qop));
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, ha2, 32);
        md5_final(&ctx, digest);
        for (int i = 0; i < 16; i++) {
            expected[i*2]   = "0123456789abcdef"[digest[i] >> 4];
            expected[i*2+1] = "0123456789abcdef"[digest[i] & 0x0f];
        }
        expected[32] = '\0';
    } else {
        /* Simple: response = MD5(HA1:nonce:HA2) */
        md5_ctx_t ctx;
        uint8_t digest[16];
        md5_init(&ctx);
        md5_update(&ctx, digest_ha1, 32);
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, nonce, strlen(nonce));
        md5_update(&ctx, ":", 1);
        md5_update(&ctx, ha2, 32);
        md5_final(&ctx, digest);
        for (int i = 0; i < 16; i++) {
            expected[i*2]   = "0123456789abcdef"[digest[i] >> 4];
            expected[i*2+1] = "0123456789abcdef"[digest[i] & 0x0f];
        }
        expected[32] = '\0';
    }

    /* Constant-time comparison */
    volatile unsigned char diff = 0;
    for (int i = 0; i < 32; i++)
        diff |= (unsigned char)(expected[i] ^ response[i]);
    return diff == 0 ? 1 : 0;
}

int api_auth_is_disabled(void)
{
    return auth_disabled || !setup_complete;
}

int api_auth_is_setup_complete(void)
{
    return setup_complete;
}

/* Generate a nonce value for WWW-Authenticate header */
const char *api_auth_get_realm(void) { return DIGEST_REALM; }
const char *api_auth_get_nonce(void)
{
    digest_nonce_t *n = create_nonce();
    return n ? n->nonce : "00000000000000000000000000000000";
}

int api_auth_add_session_token(const char *token, int expiry_days)
{
    if (token_count >= MAX_TOKENS) {
        memmove(&tokens[0], &tokens[1],
                sizeof(session_token_t) * (MAX_TOKENS - 1));
        token_count = MAX_TOKENS - 1;
    }
    session_token_t *t = &tokens[token_count++];
    snprintf(t->token, TOKEN_LEN, "%s", token);
    t->expires = time(NULL) + (time_t)expiry_days * 86400;
    return 0;
}

void api_auth_set_disabled(int disabled)
{
    auth_disabled = disabled;
    save_config();
}

void api_auth_set_password(const char *password)
{
    /* SHA-256 hash for web UI login */
    char salt[24];
    generate_random_hex(salt + 3, 16);
    salt[0] = '$'; salt[1] = '5'; salt[2] = '$';
    salt[19] = '$'; salt[20] = '\0';
    const char *hashed = crypt(password, salt);
    if (hashed) {
        snprintf(password_hash, sizeof(password_hash), "%s", hashed);
    }

    set_digest_credentials("operator", password);

    save_config();
}

void api_auth_set_setup_complete(void)
{
    setup_complete = 1;
    save_config();
}

int api_auth_check_password(const char *password)
{
    if (password_hash[0] == '\0') return 0;
    const char *hashed = crypt(password, password_hash);
    if (!hashed) return 0;
    size_t len = strlen(password_hash);
    if (strlen(hashed) != len) return 0;
    volatile unsigned char diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= (unsigned char)(hashed[i] ^ password_hash[i]);
    return diff == 0 ? 1 : 0;
}

int api_auth_has_password(void)
{
    return password_hash[0] != '\0' && digest_ha1[0] != '\0';
}
