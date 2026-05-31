#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""
Convert a 320x240 PNG to raw RGB565 little-endian framebuffer data
for the ILI9341 SPI TFT (fb_ili9341, fb0).

Output is 153,600 bytes (320 * 240 * 2) that can be written directly:
    cat splash.rgb565 > /dev/fb0

Requires: Pillow (pip install Pillow)
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is required.  pip install Pillow", file=sys.stderr)
    sys.exit(1)


def rgb_to_rgb565(r: int, g: int, b: int) -> int:
    """Convert 8-bit RGB to 16-bit RGB565 (RRRRRGGGGGGBBBBB)."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert(input_path: Path, output_path: Path, width: int = 320, height: int = 240) -> None:
    img = Image.open(input_path).convert("RGB")

    if img.size != (width, height):
        print(f"WARNING: source image is {img.size}, expected ({width}, {height}). Resizing.")
        img = img.resize((width, height), Image.LANCZOS)

    pixels = img.load()
    buf = bytearray(width * height * 2)

    idx = 0
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            pixel = rgb_to_rgb565(r, g, b)
            struct.pack_into("<H", buf, idx, pixel)
            idx += 2

    expected = width * height * 2
    assert len(buf) == expected  # guaranteed by pre-allocation; sanity check

    output_path.write_bytes(buf)
    print(f"Wrote {len(buf)} bytes to {output_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Convert PNG to raw RGB565 framebuffer data")
    parser.add_argument("input", type=Path, help="Input PNG file (320x240)")
    parser.add_argument("output", type=Path, help="Output .rgb565 file")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    args = parser.parse_args()

    if not args.input.exists():
        print(f"ERROR: input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    convert(args.input, args.output, args.width, args.height)


if __name__ == "__main__":
    main()
