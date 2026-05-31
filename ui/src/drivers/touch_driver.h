/**
 * SPDX-License-Identifier: MPL-2.0
 * Touchscreen input driver header (LVGL v9)
 */
#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

/**
 * Initialize touchscreen input device.
 * Returns 0 on success, -1 on failure.
 */
int touch_driver_init(void);

/**
 * Cleanup: close input device.
 */
void touch_driver_deinit(void);

#endif /* TOUCH_DRIVER_H */
