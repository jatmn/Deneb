/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Materials endpoint. Returns material definitions as JSON array.
 */

#include "api_materials.h"
#include "json_writer.h"

#include <stdio.h>
#include <string.h>

void api_materials_get(const http_request_t *req, http_response_t *resp)
{
    (void)req;

    /*
     * The UM API returns materials as an array of XML strings.
     * For now, return an empty array. Material support can be
     * expanded when the UM2+ Connect material system is integrated.
     */
    api_http_set_body_str(resp, "[]");
}
