/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * About screen. LVGL v9.
 */

#include "screen_mgr.h"
#include "locale.h"
#include "lvgl.h"
#include "printer_identity.h"

static lv_obj_t *about_screen = NULL;

#ifndef DENEB_VERSION
#define DENEB_VERSION "0.1.0-dev"
#endif

#ifndef DENEB_STOCK_BASE_VERSION
#define DENEB_STOCK_BASE_VERSION "UM2C 1.5.3 / UM2C_00100503"
#endif

static lv_obj_t *about_create(void)
{
    char printer_id[80];
    deneb_printer_identity_display_id(printer_id, sizeof(printer_id));

    about_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(about_screen, 320, 208);
    lv_obj_align(about_screen, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_set_style_bg_color(about_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_radius(about_screen, 0, 0);
    lv_obj_set_style_border_width(about_screen, 0, 0);
    lv_obj_set_style_pad_all(about_screen, 12, 0);
    lv_obj_set_flex_flow(about_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(about_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(about_screen, 6, 0);
    lv_obj_set_scroll_dir(about_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(about_screen, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(about_screen, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(about_screen, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    lv_obj_t *name_lbl = lv_label_create(about_screen);
    lv_label_set_text(name_lbl, "Deneb");
    lv_obj_set_style_text_color(name_lbl, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(name_lbl, &deneb_font_16, 0);

    lv_obj_t *ver_lbl = lv_label_create(about_screen);
    lv_label_set_text_fmt(ver_lbl, "v%s", DENEB_VERSION);
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(ver_lbl, &deneb_font_12, 0);

    lv_obj_t *base_lbl = lv_label_create(about_screen);
    lv_label_set_text_fmt(base_lbl, locale_get("about.stock_base_fmt"),
                          DENEB_STOCK_BASE_VERSION);
    lv_obj_set_width(base_lbl, 280);
    lv_label_set_long_mode(base_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(base_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(base_lbl, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(base_lbl, &deneb_font_12, 0);

    lv_obj_t *desc_lbl = lv_label_create(about_screen);
    lv_label_set_text(desc_lbl, locale_get("about.description"));
    lv_obj_set_style_text_color(desc_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(desc_lbl, &deneb_font_12, 0);
    lv_obj_set_width(desc_lbl, 280);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(desc_lbl, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *license_lbl = lv_label_create(about_screen);
    lv_label_set_text(license_lbl, locale_get("about.license"));
    lv_obj_set_style_text_color(license_lbl, lv_color_hex(0xa0a0a0), 0);
    lv_obj_set_style_text_font(license_lbl, &deneb_font_12, 0);

    lv_obj_t *gh_lbl = lv_label_create(about_screen);
    lv_label_set_text(gh_lbl, "github.com/jatmn/Deneb");
    lv_obj_set_style_text_color(gh_lbl, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(gh_lbl, &deneb_font_12, 0);

    lv_obj_t *id_title = lv_label_create(about_screen);
    lv_label_set_text(id_title, locale_get("about.printer_id"));
    lv_obj_set_style_text_color(id_title, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(id_title, &deneb_font_14, 0);

    lv_obj_t *id_lbl = lv_label_create(about_screen);
    lv_label_set_text(id_lbl, printer_id);
    lv_obj_set_width(id_lbl, 280);
    lv_label_set_long_mode(id_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(id_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(id_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(id_lbl, &deneb_font_12, 0);

    lv_obj_t *cert_title = lv_label_create(about_screen);
    lv_label_set_text(cert_title, locale_get("about.certifications"));
    lv_obj_set_style_text_color(cert_title, lv_color_hex(0x53a8b6), 0);
    lv_obj_set_style_text_font(cert_title, &deneb_font_14, 0);

    lv_obj_t *cert_lbl = lv_label_create(about_screen);
    lv_label_set_text(cert_lbl, "South Korea\nR-R-UVB-2PC");
    lv_obj_set_width(cert_lbl, 280);
    lv_label_set_long_mode(cert_lbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(cert_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(cert_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_text_font(cert_lbl, &deneb_font_12, 0);

    return about_screen;
}

static void about_destroy(void) { about_screen = NULL; }

const screen_ops_t screen_about = {
    .name = "settings.about",
    .create = about_create,
    .destroy = about_destroy,
    .show_back = true,
};
