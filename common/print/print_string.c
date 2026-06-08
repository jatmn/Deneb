/* SPDX-License-Identifier: MPL-2.0 */
#include "print_string.h"

#include <stddef.h>
#include <string.h>

int deneb_ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

int deneb_ascii_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

int deneb_str_eq_ci(const char *a, const char *b)
{
    if (!a || !b)
        return 0;

    while (*a && *b) {
        if (deneb_ascii_tolower((unsigned char)*a) !=
            deneb_ascii_tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int deneb_str_is_one_of_ci(const char *value, const char *const *choices)
{
    if (!value || !*value)
        return 0;

    for (int i = 0; choices[i]; i++) {
        if (deneb_str_eq_ci(value, choices[i]))
            return 1;
    }
    return 0;
}

int deneb_str_contains_ci(const char *haystack, const char *needle)
{
    size_t hlen;
    size_t nlen;

    if (!haystack || !needle)
        return 0;

    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen)
        return 0;

    for (size_t i = 0; i <= hlen - nlen; i++) {
        size_t j = 0;
        while (j < nlen &&
               deneb_ascii_tolower((unsigned char)haystack[i + j]) ==
                   deneb_ascii_tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen)
            return 1;
    }

    return 0;
}
