/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Auth endpoint handlers. /api/v1/auth/* and session management.
 */

#ifndef API_AUTH_H
#define API_AUTH_H

#include "api_http.h"

/* Initialize auth module (load config). */
void api_auth_init(void);

/* Auth endpoint handlers */
void api_auth_request_post(const http_request_t *req, http_response_t *resp);
void api_auth_check_get(const http_request_t *req, http_response_t *resp);
void api_auth_verify_get(const http_request_t *req, http_response_t *resp);

/* Validation helpers */
int api_auth_validate_token(const char *token);
int api_auth_validate_digest(const char *digest_header, const char *method, const char *path);
int api_auth_is_disabled(void);
int api_auth_is_setup_complete(void);

/* Setup management (called from api_deneb.c) */
void api_auth_set_password(const char *password);
void api_auth_set_disabled(int disabled);
void api_auth_set_setup_complete(void);
int api_auth_check_password(const char *password);
int api_auth_has_password(void);
int api_auth_add_session_token(const char *token, int expiry_days);

/* Digest auth helpers */
const char *api_auth_get_realm(void);
const char *api_auth_get_nonce(void);

#endif
