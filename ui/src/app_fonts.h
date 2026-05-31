/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Application font fallbacks for translated UI text.
 */

#ifndef APP_FONTS_H
#define APP_FONTS_H

#include "lvgl.h"

extern lv_font_t deneb_font_12;
extern lv_font_t deneb_font_14;
extern lv_font_t deneb_font_16;

void deneb_fonts_init(void);

#endif /* APP_FONTS_H */
