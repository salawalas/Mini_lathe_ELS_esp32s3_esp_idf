#!/usr/bin/env python3
"""
generate_logo.py  –  Generates a 48x48 gear-style test bitmap
for the Mini Lathe controller.

Output: components/display/font/logo.raw
        (raw RGB565 with 4-byte w/h header, automatically flashed via SPIFFS)
"""

import struct, math, os

W, H = 48, 48
cx, cy = W // 2, H // 2

# ── helpers ──
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

BLACK = 0x0000
BLUE  = rgb565(0, 0, 255)
CYAN  = rgb565(0, 255, 255)
WHITE = 0xFFFF
RED   = rgb565(255, 0, 0)

# ── render ──
pixels = []
for y in range(H):
    for x in range(W):
        dx, dy = x - cx, y - cy
        r = math.sqrt(dx*dx + dy*dy)
        if r > 22:
            c = BLACK
        elif r > 18:
            # outer ring with gear teeth
            angle = math.atan2(dy, dx)
            tooth_pos = ((angle + math.pi) / (2 * math.pi)) * 8 % 1
            c = BLACK if abs(tooth_pos - 0.5) < 0.35 else BLUE
        elif r > 12:
            c = CYAN
        elif r > 6:
            c = WHITE
        else:
            c = RED
        pixels.append(c)

# ── write .raw (header + data) ──
out_dir = os.path.join(os.path.dirname(__file__), "..", "components", "display", "font")
os.makedirs(out_dir, exist_ok=True)
out_path = os.path.join(out_dir, "logo.raw")

with open(out_path, "wb") as f:
    f.write(struct.pack("<HH", W, H))
    f.write(struct.pack(f"<{len(pixels)}H", *pixels))

print(f"OK {out_path}  ({len(pixels)} px = {os.path.getsize(out_path)} bytes)")