/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "lvgl.h"
#include "misc/lv_array.h"
#include "misc/lv_text_private.h"

#include <assert.h>

static void test_memory_paths(void)
{
    char data[] = "Deneb";
    lv_fs_path_ex_t path;
    void *buffer = NULL;
    uint32_t size = 0;

    lv_fs_make_path_from_buffer(&path, 'A', data, sizeof(data), "bin");
    assert(lv_fs_get_buffer_from_path(&path, &buffer, &size) == LV_RESULT_OK);
    assert(buffer == data);
    assert(size == sizeof(data));

    lv_fs_make_path_from_buffer(&path, 'A', data, sizeof(data), NULL);
    assert(lv_fs_get_buffer_from_path(&path, &buffer, &size) == LV_RESULT_OK);
    assert(buffer == data);
    assert(size == sizeof(data));

    path = (lv_fs_path_ex_t){.path = "A:123"};
    assert(lv_fs_get_buffer_from_path(&path, &buffer, &size) == LV_RESULT_INVALID);
    assert(buffer == NULL);
    assert(size == 0);
}

static void test_array_copy_and_growth(void)
{
    const uint32_t values[] = {0x11223344, 0x55667788};
    lv_array_t array;
    lv_array_t copy = {0};
    assert(lv_array_init(&array, 2, sizeof(values[0])) == LV_RESULT_OK);
    assert(lv_array_push_back(&array, &values[0]) == LV_RESULT_OK);
    assert(lv_array_push_back(&array, &values[1]) == LV_RESULT_OK);
    lv_array_copy(&copy, &array);
    assert(lv_array_concat(&array, &copy) == LV_RESULT_OK);
    assert(lv_array_size(&array) == 4);
    for (uint32_t i = 0; i < lv_array_size(&array); ++i) {
        const uint32_t *value = lv_array_at(&array, i);
        assert(value && *value == values[i % 2]);
    }
    lv_array_deinit(&copy);
    lv_array_deinit(&array);
}

static bool get_test_glyph(const lv_font_t *font, lv_font_glyph_dsc_t *glyph,
                           uint32_t letter, uint32_t next_letter)
{
    (void)font;
    glyph->adv_w = letter == 'A' && next_letter == 'V' ? 6 : 9;
    return true;
}

static void test_text_prefix_kerning(void)
{
    lv_text_attributes_t attributes;
    lv_text_attributes_init(&attributes);
    const lv_font_t font = {.get_glyph_dsc = get_test_glyph, .kerning = LV_FONT_KERNING_NORMAL};
    assert(lv_text_get_width("AV", 1, &font, &attributes) == 6);
    assert(lv_text_get_width("AV", 2, &font, &attributes) == 15);
}

int main(void)
{
    lv_init();
    test_memory_paths();
    test_array_copy_and_growth();
    test_text_prefix_kerning();
    lv_deinit();
    return 0;
}
