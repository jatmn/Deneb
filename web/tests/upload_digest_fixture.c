/* SPDX-License-Identifier: MPL-2.0 */
#include "md5.h"
#include <stdio.h>

int main(void)
{
    char input[512];
    char result[33];
    size_t len = fread(input, 1, sizeof(input), stdin);
    if (ferror(stdin) || len == sizeof(input))
        return 1;
    md5_hex(input, len, result);
    return fputs(result, stdout) == EOF;
}
