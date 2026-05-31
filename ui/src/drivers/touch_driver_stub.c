/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Stub touch driver for host-side build testing.
 */

#include "touch_driver.h"
#include <stdio.h>
#include "lvgl.h"

static lv_indev_t *indev = NULL;

static void touch_read_cb(lv_indev_t *ind, lv_indev_data_t *data)
{
    (void)ind;
    data->point.x = 0;
    data->point.y = 0;
    data->state = LV_INDEV_STATE_RELEASED;
}

int touch_driver_init(void)
{
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    fprintf(stderr, "touch_driver: stub initialized (host build)\n");
    return 0;
}

void touch_driver_deinit(void)
{
    if (indev) {
        lv_indev_delete(indev);
        indev = NULL;
    }
}
