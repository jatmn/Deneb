/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_MANUAL_OPERATION_H
#define DENEB_MANUAL_OPERATION_H
#include <stdint.h>

/* Decimal wire tokens avoid precision loss in JSON number consumers. */
static inline uint64_t deneb_manual_id_parse(const char *text)
{
    uint64_t value = 0;
    if (!text || !*text)
        return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9')
            return 0;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (UINT64_MAX - digit) / 10)
            return 0;
        value = value * 10 + digit;
    }
    return value;
}

/* Token identifies the accepted operation; terminal state is retained until
 * another operation or manual ABORT is accepted. COMPLETE requires the final
 * M400 ACK. */
typedef enum {
    DENEB_MANUAL_NONE = 0,
    DENEB_MANUAL_RUNNING,
    DENEB_MANUAL_COMPLETE,
    DENEB_MANUAL_CANCELLED,
    DENEB_MANUAL_CANCELLING,
    DENEB_MANUAL_ERROR
} deneb_manual_state_t;

#endif
