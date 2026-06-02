/**
 * SPDX-License-Identifier: MPL-2.0
 * Framebuffer driver header for ILI9341 SPI TFT (LVGL v9)
 */
#ifndef FB_DRIVER_H
#define FB_DRIVER_H

/**
 * Initialize the framebuffer device and register LVGL v9 display.
 * Returns 0 on success, -1 on failure.
 */
int fb_driver_init(void);

/**
 * Cleanup: unmap framebuffer and close device.
 */
void fb_driver_deinit(void);

/**
 * Save the current framebuffer contents as a PPM image.
 * Intended for host screenshot generation and lightweight diagnostics.
 * Returns 0 on success, -1 on failure.
 */
int fb_driver_save_ppm(const char *path);

#endif /* FB_DRIVER_H */
