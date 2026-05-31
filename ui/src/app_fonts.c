/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Application font fallbacks for translated UI text.
 */

#include "app_fonts.h"

LV_FONT_DECLARE(deneb_font_i18n_12);
LV_FONT_DECLARE(deneb_font_i18n_14);
LV_FONT_DECLARE(deneb_font_i18n_16);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);

static lv_font_t i18n_12;
static lv_font_t i18n_14;
static lv_font_t i18n_16;

lv_font_t deneb_font_12;
lv_font_t deneb_font_14;
lv_font_t deneb_font_16;

void deneb_fonts_init(void)
{
    i18n_12 = deneb_font_i18n_12;
    i18n_14 = deneb_font_i18n_14;
    i18n_16 = deneb_font_i18n_16;

    deneb_font_12 = lv_font_montserrat_12;
    deneb_font_14 = lv_font_montserrat_14;
    deneb_font_16 = lv_font_montserrat_16;

    deneb_font_12.fallback = &i18n_12;
    deneb_font_14.fallback = &i18n_14;
    deneb_font_16.fallback = &i18n_16;
}
