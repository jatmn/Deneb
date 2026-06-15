#!/usr/bin/env python3
"""Generate LVGL v9 RGB565 embedded image assets for the status screen.
Run from the repo root (or adjust paths).
"""
import math
import os

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ASSETS_DIR = os.path.join(REPO_ROOT, "ui", "src", "assets")

try:
    from PIL import Image
except ImportError:
    Image = None

# 5/6/5-bit components -> RGB565 little-endian bytes
def rgb565(r, g, b):
    r5 = (r * 31 + 127) // 255
    g6 = (g * 63 + 127) // 255
    b5 = (b * 31 + 127) // 255
    val = (r5 << 11) | (g6 << 5) | b5
    return val & 0xFF, (val >> 8) & 0xFF

C_BG = rgb565(0x0A, 0x0A, 0x1A)
C_ACCENT = rgb565(0x53, 0xA8, 0xB6)
C_LIGHT = rgb565(0xE0, 0xE0, 0xE0)
C_MUTED = rgb565(0xA0, 0xA0, 0xA0)


def make_buffer(w, h, fill):
    return [fill[:] for _ in range(w * h)]


def set_px(buf, w, h, x, y, c):
    if 0 <= x < w and 0 <= y < h:
        buf[y * w + x] = c


def dist(x0, y0, x1, y1):
    return math.hypot(x0 - x1, y0 - y1)


def draw_circle(buf, w, h, cx, cy, r, c):
    for y in range(h):
        for x in range(w):
            if dist(x, y, cx, cy) <= r + 0.5:
                set_px(buf, w, h, x, y, c)


def draw_ring(buf, w, h, cx, cy, outer_r, inner_r, c):
    for y in range(h):
        for x in range(w):
            d = dist(x, y, cx, cy)
            if inner_r <= d <= outer_r:
                set_px(buf, w, h, x, y, c)


def draw_rect(buf, w, h, x0, y0, x1, y1, c):
    for y in range(max(0, y0), min(h, y1 + 1)):
        for x in range(max(0, x0), min(w, x1 + 1)):
            set_px(buf, w, h, x, y, c)


def draw_line(buf, w, h, x0, y0, x1, y1, c, thickness=1):
    steps = max(abs(x1 - x0), abs(y1 - y0), 1)
    for i in range(steps + 1):
        t = i / steps
        x = round(x0 + (x1 - x0) * t)
        y = round(y0 + (y1 - y0) * t)
        for dy in range(-thickness // 2, thickness // 2 + 1):
            for dx in range(-thickness // 2, thickness // 2 + 1):
                set_px(buf, w, h, x + dx, y + dy, c)


def image_to_buffer(path, w, h, bg=(0x0A, 0x0A, 0x1A)):
    if Image is None:
        return None

    img = Image.open(path).convert("RGBA")
    scale = min(w / img.width, h / img.height)
    new_size = (max(1, int(img.width * scale)),
                max(1, int(img.height * scale)))
    img = img.resize(new_size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (w, h), bg)
    x = (w - img.width) // 2
    y = (h - img.height) // 2
    canvas.paste(img.convert("RGB"), (x, y), img)
    return [rgb565(*canvas.getpixel((x, y))) for y in range(h) for x in range(w)]


def gen_logo_116():
    w, h = 116, 116
    logo_path = os.path.join(REPO_ROOT, "assets", "branding",
                             "deneb-loading-mark-48x48.png")
    if os.path.exists(logo_path):
        buf = image_to_buffer(logo_path, w, h)
        if buf is not None:
            return buf, w, h

    buf = make_buffer(w, h, C_BG)
    # Minimal fallback if Pillow or the source branding asset is unavailable.
    draw_line(buf, w, h, 18, 72, 44, 44, C_ACCENT, thickness=3)
    draw_line(buf, w, h, 44, 44, 72, 72, C_ACCENT, thickness=3)
    draw_line(buf, w, h, 72, 72, 98, 44, C_ACCENT, thickness=3)
    draw_line(buf, w, h, 18, 80, 98, 80, C_LIGHT, thickness=2)
    return buf, w, h


def gen_nozzle_16():
    w, h = 20, 20
    buf = make_buffer(w, h, C_BG)
    # 3D printer hotend: heater block, throat, tapered brass nozzle.
    draw_rect(buf, w, h, 5, 2, 14, 5, C_ACCENT)
    draw_rect(buf, w, h, 7, 6, 12, 7, C_ACCENT)
    draw_rect(buf, w, h, 4, 8, 15, 11, C_ACCENT)
    draw_line(buf, w, h, 5, 12, 14, 12, C_ACCENT, thickness=1)
    draw_line(buf, w, h, 6, 13, 13, 13, C_ACCENT, thickness=1)
    draw_line(buf, w, h, 7, 14, 12, 14, C_ACCENT, thickness=1)
    draw_line(buf, w, h, 8, 15, 11, 15, C_ACCENT, thickness=1)
    draw_line(buf, w, h, 9, 16, 10, 17, C_LIGHT, thickness=1)
    set_px(buf, w, h, 10, 18, C_LIGHT)
    return buf, w, h


def gen_bed_16():
    w, h = 20, 20
    buf = make_buffer(w, h, C_BG)
    # Heated build plate: flat bed plus three heat waves.
    draw_rect(buf, w, h, 2, 13, 17, 15, C_ACCENT)
    draw_line(buf, w, h, 4, 16, 3, 18, C_MUTED, thickness=1)
    draw_line(buf, w, h, 15, 16, 16, 18, C_MUTED, thickness=1)
    for x0 in (5, 10, 15):
        draw_line(buf, w, h, x0, 2, x0 - 1, 4, C_LIGHT, thickness=1)
        draw_line(buf, w, h, x0 - 1, 4, x0 + 1, 7, C_LIGHT, thickness=1)
        draw_line(buf, w, h, x0 + 1, 7, x0, 10, C_LIGHT, thickness=1)
    return buf, w, h


LVGL_IMAGE_HEADER = '''#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)
#include <lvgl.h>
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_CONST
#endif
'''


def emit(name, buf, w, h):
    stride = w * 2  # RGB565 unpadded
    lines = []
    for y in range(h):
        row = []
        for x in range(w):
            row.extend(f"0x{buf[y*w+x][0]:02x},0x{buf[y*w+x][1]:02x}")
        # join with commas, keep row as one line of bytes
        line_bytes = []
        for x in range(w):
            line_bytes.append(f"0x{buf[y*w+x][0]:02x},0x{buf[y*w+x][1]:02x}")
        lines.append("    " + ",".join(line_bytes) + ",")
    # trim trailing comma after last row to avoid warnings
    body = "\n".join(lines)
    return f'''{LVGL_IMAGE_HEADER}
static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST
uint8_t {name}_map[] = {{
{body}
}};

const lv_image_dsc_t {name} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_RGB565,
    .flags = 0,
    .w = {w},
    .h = {h},
    .stride = {stride},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({name}_map),
  .data = {name}_map,
  .reserved = NULL,
}};
'''


def main():
    for name, gen in [
        ("deneb_logo_116", gen_logo_116),
        ("ic_nozzle_16", gen_nozzle_16),
        ("ic_bed_16", gen_bed_16),
    ]:
        buf, w, h = gen()
        out_path = os.path.join(ASSETS_DIR, f"{name}.c")
        with open(out_path, "w") as f:
            f.write(emit(name, buf, w, h))
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
