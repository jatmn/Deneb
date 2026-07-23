/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Locale/i18n system for Deneb UI.
 * Loads JSON locale files at runtime. Falls back to English for missing keys.
 * Designed for minimal memory: loads one locale at a time, keys are interned.
 */

#ifndef LOCALE_H
#define LOCALE_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Initialize the locale system.
 * Loads the specified locale from /etc/deneb/locales/<lang>.json
 * Falls back to embedded English defaults if file not found.
 *
 * @param lang  BCP 47 locale code (e.g., "en", "nl", "de", "zh-Hans")
 * @return 0 on success, -1 on error (will still have English defaults)
 */
int locale_init(const char *lang);

/**
 * Get a translated string by key.
 * Returns the English default if key not found in current locale.
 * Returns the key itself if not found anywhere.
 *
 * @param key  Dot-separated key (e.g., "menu.status.title")
 * @return Translated string. Valid until next locale_init() or deinit().
 */
const char *locale_get(const char *key);

/**
 * Safely substitute one or two string values into a translated template.
 * Only literal %s placeholders are recognized. Other percent sequences are
 * copied as text, so locale data is never interpreted as a printf format.
 *
 * @return 0 when the template has exactly the expected placeholders, -1 when
 *         arguments are invalid or the template does not match.
 */
int locale_format_s(char *out, size_t out_size, const char *key,
                    const char *value);
int locale_format_ss(char *out, size_t out_size, const char *key,
                     const char *first, const char *second);

/**
 * Change locale at runtime. Reloads from disk.
 * @param lang  BCP 47 locale code
 * @return 0 on success, -1 on error
 */
int locale_set(const char *lang);

/**
 * Get current locale code.
 */
const char *locale_current(void);

/**
 * Cleanup locale system.
 */
void locale_deinit(void);

#endif /* LOCALE_H */
