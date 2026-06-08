/* SPDX-License-Identifier: MPL-2.0 */
#ifndef DENEB_PRINT_STRING_H
#define DENEB_PRINT_STRING_H

int deneb_ascii_tolower(int c);
int deneb_ascii_isspace(int c);
int deneb_str_eq_ci(const char *a, const char *b);
int deneb_str_is_one_of_ci(const char *value, const char *const *choices);
int deneb_str_contains_ci(const char *haystack, const char *needle);

#endif
