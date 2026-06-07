/* SPDX-License-Identifier: MPL-2.0 */
#include "print_history.h"

#include "json_file.h"

void deneb_print_history_read_default_array_or_empty(char *out, size_t out_sz)
{
    deneb_json_file_read_array_or_empty(DENEB_PRINT_HISTORY_PATH, out, out_sz);
}
