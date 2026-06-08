/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * HTTP request/response handling and routing.
 */

#ifndef API_HTTP_H
#define API_HTTP_H

#include <stddef.h>

#define HTTP_MAX_PATH    256
#define HTTP_MAX_HEADERS 16
#define HTTP_MAX_BODY    4096

/* Parsed HTTP request */
typedef struct {
    char method[8];                    /* GET, POST, PUT, DELETE */
    char path[HTTP_MAX_PATH];          /* e.g. /api/v1/printer/status */
    char query[HTTP_MAX_PATH];         /* query string after ? */
    struct {
        char name[64];
        char value[256];
    } headers[HTTP_MAX_HEADERS];
    int header_count;
    char body[HTTP_MAX_BODY];
    int body_len;
    char auth_digest[512];             /* Authorization header value */
    char content_type[64];
    int content_length;
    /* For multipart file uploads */
    char upload_path[128];             /* temp file path if body was streamed */
    int upload_size;                   /* bytes written to upload_path */
    char multipart_boundary[128];      /* parsed from Content-Type */
    int is_multipart;                  /* 1 if Content-Type is multipart/form-data */
} http_request_t;

/* HTTP response - built incrementally */
typedef struct {
    int status_code;
    char content_type[64];
    char *body;
    size_t body_len;
    size_t body_cap;
    int keep_alive;
    /* For SSE */
    int is_sse;
    /* For Digest auth challenge */
    char auth_challenge[256];
} http_response_t;

/* Route handler function type */
typedef void (*route_handler_t)(const http_request_t *req, http_response_t *resp);

/* Route entry */
typedef struct {
    const char *method;
    const char *pattern;       /* e.g. "/api/v1/printer/status" */
    route_handler_t handler;
    int requires_auth;         /* 1 = requires valid auth */
} api_route_t;

/* Initialize HTTP module. */
void api_http_init(void);

/* Parse an HTTP request from raw bytes. Returns 0 on success. */
int api_http_parse(http_request_t *req, const char *raw, size_t len);

/* Route a parsed request to the appropriate handler. */
void api_http_route(const http_request_t *req, http_response_t *resp);

/* Serialize response to raw HTTP bytes. Returns bytes written. */
int api_http_serialize(const http_response_t *resp, char *buf, size_t cap);

/* Initialize a response with defaults. */
void api_http_response_init(http_response_t *resp);

/* Set response body (copies data). */
void api_http_set_body(http_response_t *resp, const char *data, size_t len);

/* Set response body from null-terminated string. */
void api_http_set_body_str(http_response_t *resp, const char *str);

/* Check if request requires auth and validate it. Returns 1 if authorized. */
int api_http_check_auth(const http_request_t *req);

#endif /* API_HTTP_H */
