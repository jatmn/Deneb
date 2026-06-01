/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Materials endpoint handlers. /api/v1/materials
 */

#ifndef API_MATERIALS_H
#define API_MATERIALS_H

#include "api_http.h"

void api_materials_get(const http_request_t *req, http_response_t *resp);

#endif
