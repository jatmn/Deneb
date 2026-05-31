/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * Touchscreen input driver for the UM2C touch panel.
 * LVGL v9 API.
 */

#include "touch_driver.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "lvgl.h"

static const char *touch_dev_paths[] = {
    "/dev/input/event0",
    "/dev/input/event1",
    "/dev/input/touchscreen0",
    NULL
};

static int touch_fd = -1;
static int epoll_fd = -1;
static lv_indev_t *indev = NULL;

static int16_t last_x = 0;
static int16_t last_y = 0;
static bool is_pressed = false;

static int find_touch_device(void)
{
    for (int i = 0; touch_dev_paths[i]; i++) {
        int fd = open(touch_dev_paths[i], O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            unsigned long evbits = 0;
            if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) >= 0 &&
                (evbits & (1 << EV_ABS))) {
                fprintf(stderr, "touch_driver: found device at %s\n",
                        touch_dev_paths[i]);
                return fd;
            }
            close(fd);
        }
    }

    DIR *dir = opendir("/dev/input");
    if (!dir)
        return -1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0)
            continue;

        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;

        unsigned long evbits = 0;
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) >= 0 &&
            (evbits & (1 << EV_ABS))) {
            fprintf(stderr, "touch_driver: found device at %s\n", path);
            closedir(dir);
            return fd;
        }
        close(fd);
    }

    closedir(dir);
    return -1;
}

/**
 * LVGL v9 input read callback.
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    struct input_event ev;

    while (1) {
        struct epoll_event pev;
        int nfds = epoll_wait(epoll_fd, &pev, 1, 0);
        if (nfds <= 0)
            break;

        if (read(touch_fd, &ev, sizeof(ev)) != sizeof(ev))
            break;

        switch (ev.type) {
        case EV_ABS:
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X)
                last_x = ev.value;
            else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y)
                last_y = ev.value;
            break;
        case EV_KEY:
            if (ev.code == BTN_TOUCH)
                is_pressed = (ev.value != 0);
            break;
        }
    }

    if (last_x < 0) last_x = 0;
    if (last_x > 319) last_x = 319;
    if (last_y < 0) last_y = 0;
    if (last_y > 239) last_y = 239;

    data->point.x = last_x;
    data->point.y = last_y;
    data->state = is_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int touch_driver_init(void)
{
    touch_fd = find_touch_device();
    if (touch_fd < 0) {
        fprintf(stderr, "touch_driver: no touchscreen device found\n");
        return -1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("touch_driver: epoll_create1");
        close(touch_fd);
        touch_fd = -1;
        return -1;
    }

    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = touch_fd
    };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, touch_fd, &ev) < 0) {
        perror("touch_driver: epoll_ctl");
        close(epoll_fd);
        close(touch_fd);
        epoll_fd = -1;
        touch_fd = -1;
        return -1;
    }

    /* LVGL v9: create indev, set type and read callback */
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    fprintf(stderr, "touch_driver: initialized\n");

    return 0;
}

void touch_driver_deinit(void)
{
    if (indev) {
        lv_indev_delete(indev);
        indev = NULL;
    }
    if (epoll_fd >= 0) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    if (touch_fd >= 0) {
        close(touch_fd);
        touch_fd = -1;
    }
}
