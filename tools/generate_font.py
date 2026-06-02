#!/usr/bin/env python3
"""generate_font.py – Generate FONTX2 monospace bitmap fonts for ESP32.

Usage:
  python generate_font.py --size 24x48 --name ILGH48XB --output ILGH48XB.FNT
  python generate_font.py --size 32x64 --name ILGH64XB --output ILGH64XB.FNT

Requires: pip install Pillow
"""

import struct
import argparse
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("ERROR: Pillow not installed. Run: pip install Pillow", file=sys.stderr)
    sys.exit(1)

# Try to find a good monospace system font
def find_mono_font(size: int) -> str | None:
    import os
    candidates = []
    
    # Windows
    if sys.platform == "win32":
        candidates = [
            os.path.expandvars(r"%SystemRoot%\Fonts\consola.ttf"),
            os.path.expandvars(r"%SystemRoot%\Fonts\lucon.ttf"),
            os.path.expandvars(r"%SystemRoot%\Fonts\cour.ttf"),
        ]
    # Linux
    elif sys.platform == "linux":
        candidates = [
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
            "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        ]
    # macOS
    elif sys.platform == "darwin":
        candidates = [
            "/System/Library/Fonts/Menlo.ttc",
            "/System/Library/Fonts/Courier.ttc",
        ]
    
    for path in candidates:
        if os.path.exists(path):
            return path
    
    try:
        return ImageFont.load_default()
    except Exception:
        pass
    
    return None


def generate_fontx2(
    output_path: str,
    name: str,
    glyph_w: int,
    glyph_h: int,
    start_code: int = 0x20,  # space
    end_code: int = 0x7E,    # tilde
    ttf_path: str | None = None,
):
    """Generate a FONTX2 file with monospace ASCII glyphs."""
    
    # Load font
    if ttf_path:
        font = ImageFont.truetype(ttf_path, size=glyph_h)
        print(f"Using font: {ttf_path} at size {glyph_h}")
    else:
        font_path = find_mono_font(glyph_h)
        if font_path:
            font = ImageFont.truetype(font_path, size=glyph_h)
            print(f"Using font: {font_path} at size {glyph_h}")
        else:
            print(f"Using default PIL font")
            font = ImageFont.load_default()
    
    num_chars = end_code - start_code + 1
    bytes_per_glyph = (glyph_w * glyph_h + 7) // 8
    
    # Build header
    name_bytes = name.encode("ascii", errors="replace")[:8].ljust(8, b" ")
    header = b"FONTX2" + name_bytes
    header += struct.pack("<BBHH", glyph_w, glyph_h, 1, 0)  # 1 block
    
    # Block table: start_code, end_code, offset
    block_offset = len(header) + 12  # header + block table
    block_table = struct.pack("<III", start_code, end_code, block_offset)
    
    # Generate glyphs
    glyphs_data = bytearray()
    for code in range(start_code, end_code + 1):
        ch = chr(code)
        img = Image.new("1", (glyph_w, glyph_h), color=0)
        draw = ImageDraw.Draw(img)
        
        # Calculate position to center glyph
        bbox = draw.textbbox((0, 0), ch, font=font)
        text_w = bbox[2] - bbox[0]
        text_h = bbox[3] - bbox[1]
        x = (glyph_w - text_w) // 2 - bbox[0]
        y = (glyph_h - text_h) // 2 - bbox[1]
        
        draw.text((x, y), ch, font=font, fill=1)
        
        # Convert to monochrome bitmap bytes
        bitmap = bytearray(bytes_per_glyph)
        for py in range(glyph_h):
            for px in range(glyph_w):
                if img.getpixel((px, py)):
                    byte_idx = (py * glyph_w + px) // 8
                    bit_idx = 7 - ((py * glyph_w + px) % 8)
                    bitmap[byte_idx] |= (1 << bit_idx)
        
        glyphs_data.extend(bitmap)
    
    # Write file
    with open(output_path, "wb") as f:
        f.write(header)
        f.write(block_table)
        f.write(glyphs_data)
    
    file_size = len(header) + len(block_table) + len(glyphs_data)
    print(f"Generated: {output_path}")
    print(f"  Glyph size: {glyph_w}×{glyph_h} px")
    print(f"  Chars: {start_code:#04x}-{end_code:#04x} ({num_chars} glyphs)")
    print(f"  Bytes per glyph: {bytes_per_glyph}")
    print(f"  Total size: {file_size} bytes")
    print(f"  Name in header: '{name}'")


def main():
    parser = argparse.ArgumentParser(description="Generate FONTX2 bitmap font files")
    parser.add_argument("--size", required=True, help="Glyph size as WxH, e.g. 24x48")
    parser.add_argument("--name", required=True, help="Font name (6 chars max), e.g. ILGH48XB")
    parser.add_argument("--output", required=True, help="Output .FNT file path")
    parser.add_argument("--ttf", default=None, help="Optional TTF font file path")
    parser.add_argument("--start", type=lambda x: int(x, 0), default=0x20)
    parser.add_argument("--end", type=lambda x: int(x, 0), default=0x7E)
    args = parser.parse_args()
    
    # Parse size
    parts = args.size.split("x")
    if len(parts) != 2:
        print("ERROR: --size must be WxH, e.g. 24x48", file=sys.stderr)
        sys.exit(1)
    w, h = int(parts[0]), int(parts[1])
    
    if len(args.name) > 8:
        print("ERROR: --name max 8 characters", file=sys.stderr)
        sys.exit(1)
    
    generate_fontx2(
        output_path=args.output,
        name=args.name,
        glyph_w=w,
        glyph_h=h,
        start_code=args.start,
        end_code=args.end,
        ttf_path=args.ttf,
    )


if __name__ == "__main__":
    main()
