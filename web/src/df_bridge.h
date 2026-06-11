/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Digital Factory Gershwin bridge owned by deneb-api.
 */

#ifndef DENEB_DF_BRIDGE_H
#define DENEB_DF_BRIDGE_H

#include <stddef.h>

int deneb_df_bridge_run(const char *action, int timeout_seconds,
                        char *out, size_t out_size);

#endif /* DENEB_DF_BRIDGE_H */
