/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "lvgl.h"
#include "image/lv_image_decoder_private.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Two 16-bit RGBA pixels, red and half-opacity blue. */
static const unsigned char rgba16_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x10, 0x06, 0x00, 0x00, 0x00, 0xa4, 0xb2, 0xa3, 0xc9, 0x00, 0x00, 0x00,
    0x11, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xff, 0x9f, 0x01,
    0x08, 0x60, 0x64, 0x43, 0x03, 0x00, 0x3a, 0x58, 0x06, 0xfb, 0x6c, 0xab,
    0xe5, 0x18, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42,
    0x60, 0x82
};

/* Two 1-bit grayscale pixels, black and white, in separate Adam7 passes. */
static const unsigned char gray_adam7_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x01, 0xab, 0x5e, 0x72, 0xb1, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x68, 0x00,
    0x00, 0x00, 0x84, 0x00, 0x81, 0xc9, 0x75, 0xee, 0x92, 0x00, 0x00, 0x00,
    0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

static void expect_image(const void *source, const unsigned char pixels[8])
{
    lv_image_decoder_dsc_t decoded = {0};
    lv_image_decoder_args_t args = {.no_cache = 1};

    assert(lv_image_decoder_open(&decoded, source, &args) == LV_RESULT_OK);
    assert(decoded.decoded != NULL);
    assert(decoded.decoded->header.w == 2);
    assert(decoded.decoded->header.h == 1);
    assert(decoded.decoded->header.stride == 8);
    assert(decoded.decoded->header.cf == LV_COLOR_FORMAT_ARGB8888);
    assert(memcmp(decoded.decoded->data, pixels, 8) == 0);
    lv_image_decoder_close(&decoded);
}

int main(void)
{
    const unsigned char rgba_bgra[] = {0, 0, 255, 255, 255, 0, 0, 128};
    const unsigned char gray_bgra[] = {0, 0, 0, 255, 255, 255, 255, 255};
    const lv_image_dsc_t rgba_image = {
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .data_size = sizeof(rgba16_png),
        .data = rgba16_png,
    };
    const lv_image_dsc_t gray_image = {
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .data_size = sizeof(gray_adam7_png),
        .data = gray_adam7_png,
    };

    lv_init();
    expect_image(&rgba_image, rgba_bgra);
    expect_image(&gray_image, gray_bgra);

    char file[] = "./deneb-lodepng-XXXXXX.png";
    int fd = mkstemps(file, 4);
    assert(fd >= 0);
    FILE *output = fdopen(fd, "wb");
    assert(output != NULL);
    assert(fwrite(rgba16_png, 1, sizeof(rgba16_png), output) == sizeof(rgba16_png));
    assert(fclose(output) == 0);
    char path[sizeof(file) + 2];
    snprintf(path, sizeof(path), "A:%s", file);
    expect_image(path, rgba_bgra);
    assert(unlink(file) == 0);

    lv_deinit();
    return 0;
}
