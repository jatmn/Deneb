/* SPDX-License-Identifier: MPL-2.0 */
#undef NDEBUG
#include "drivers/fb_driver.h"
#include "lvgl.h"

#include <assert.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>

enum { WIDTH = 320, HEIGHT = 240, PITCH = WIDTH + 2, DEVICE_FD = 123 };
static uint16_t framebuffer[HEIGHT][PITCH];
static bool bgr;
static unsigned flushes;

int __wrap_open(const char *path, int flags, ...)
{
    assert(strcmp(path, "/dev/fb0") == 0 && flags == O_RDWR);
    return DEVICE_FD;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == DEVICE_FD);
    va_list args;
    va_start(args, request);
    if (request == FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *info = va_arg(args, struct fb_var_screeninfo *);
        *info = (struct fb_var_screeninfo){
            .xres = WIDTH, .yres = HEIGHT, .bits_per_pixel = 16,
            .red = {bgr ? 0 : 11, 5, 0}, .green = {5, 6, 0},
            .blue = {bgr ? 11 : 0, 5, 0},
        };
    } else {
        assert(request == FBIOGET_FSCREENINFO);
        struct fb_fix_screeninfo *info = va_arg(args, struct fb_fix_screeninfo *);
        *info = (struct fb_fix_screeninfo){
            .line_length = sizeof(framebuffer[0]), .smem_len = sizeof(framebuffer),
        };
    }
    va_end(args);
    return 0;
}

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    assert(!addr && len == sizeof(framebuffer) && offset == 0);
    assert(prot == (PROT_READ | PROT_WRITE) && flags == MAP_SHARED && fd == DEVICE_FD);
    return framebuffer;
}

int __wrap_msync(void *addr, size_t len, int flags)
{
    assert(addr && len && flags == MS_ASYNC);
    flushes++;
    return 0;
}

int __wrap_munmap(void *addr, size_t len)
{
    assert(addr == framebuffer && len == sizeof(framebuffer));
    return 0;
}

int __wrap_close(int fd)
{
    assert(fd == DEVICE_FD);
    return 0;
}

static void check_display(bool swap_red_blue)
{
    bgr = swap_red_blue;
    assert(fb_driver_init() == 0);
    lv_display_t *display = lv_display_get_default();
    assert(lv_display_get_color_format(display) == LV_COLOR_FORMAT_RGB565);
    const lv_draw_buf_t *buf = lv_display_get_buf_active(display);
    assert(buf && buf->data_size == 80 * lv_draw_buf_width_to_stride(WIDTH, LV_COLOR_FORMAT_RGB565));
    assert((uintptr_t)buf->data % LV_DRAW_BUF_ALIGN == 0);

    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xff0000), 0);

    enum { X = 11, Y = 123, BOX_WIDTH = 23, BOX_HEIGHT = 13 };
    lv_obj_t *box = lv_obj_create(screen);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, X, Y);
    lv_obj_set_size(box, BOX_WIDTH, BOX_HEIGHT);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xff0000), 0);
    flushes = 0;
    lv_refr_now(display);
    assert(flushes == 3);

    flushes = 0;
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0000ff), 0);
    lv_refr_now(display);
    assert(flushes == 1);

    const uint16_t red = bgr ? 0x001f : 0xf800;
    const uint16_t blue = bgr ? 0xf800 : 0x001f;
    for (unsigned y = 0; y < HEIGHT; y++) {
        for (unsigned x = 0; x < WIDTH; x++) {
            bool in_box = x >= X && x < X + BOX_WIDTH && y >= Y && y < Y + BOX_HEIGHT;
            assert(framebuffer[y][x] == (in_box ? blue : red));
        }
        assert(framebuffer[y][WIDTH] == 0 && framebuffer[y][WIDTH + 1] == 0);
    }
    fb_driver_deinit();
}

int main(void)
{
    lv_init();
    check_display(false);
    check_display(true);
    lv_deinit();
    return 0;
}
