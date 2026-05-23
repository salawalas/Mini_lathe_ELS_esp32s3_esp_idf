#!/usr/bin/env python3
"""
png_to_raw.py  –  Convert any PNG image to .raw format for the
Mini Lathe controller (RGB565 with 4-byte w/h header).

Usage:
    python tools/png_to_raw.py logo.png
    python tools/png_to_raw.py logo.png --size 48x48
    python tools/png_to_raw.py logo.png -o components/display/font/my_bmp.raw

Output: same path with .raw extension, or as specified with -o.
"""

import struct, sys, os
from PIL import Image

def rgb565_pixel(r, g, b):
    """Convert 8-bit R,G,B to 16-bit RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def convert_png_to_raw(png_path, out_path=None, resize=None):
    img = Image.open(png_path).convert("RGB")
    if resize:
        w, h = resize
        img = img.resize((w, h), Image.LANCZOS)
    w, h = img.size

    if out_path is None:
        out_path = os.path.splitext(png_path)[0] + ".raw"

    pixels = []
    for y in range(h):
        for x in range(w):
            r, g, b = img.getpixel((x, y))
            pixels.append(rgb565_pixel(r, g, b))

    with open(out_path, "wb") as f:
        f.write(struct.pack("<HH", w, h))
        f.write(struct.pack(f"<{len(pixels)}H", *pixels))

    print(f"OK  {out_path}  ({w}x{h}, {len(pixels)} px, {os.path.getsize(out_path)} bytes)")
    return out_path

# ── CLI ──
def parse_size(s):
    parts = s.lower().split("x")
    return (int(parts[0]), int(parts[1]))

if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="Convert PNG to raw RGB565 for Mini Lathe")
    ap.add_argument("png", help="Input PNG file")
    ap.add_argument("-o", "--output", help="Output .raw path (default: same name)")
    ap.add_argument("--size", help="Resize to WxH, e.g. 48x48 (default: original size)")
    args = ap.parse_args()

    resize = parse_size(args.size) if args.size else None
    convert_png_to_raw(args.png, args.output, resize)