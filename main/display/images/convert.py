#!/usr/bin/env python3
"""
Convert PNG/JPG to RGB565 C headers for ST7789 display.
Generates both portrait (172x320) and landscape (320x172) versions.

Usage:
    python convert.py <input_image> [name]

Examples:
    python convert.py logo.png              -> img_logo.h + img_logo_land.h
    python convert.py banner.png myimg      -> img_myimg.h + img_myimg_land.h
"""
import sys
import os
import struct
from PIL import Image

PORT_W, PORT_H = 172, 320
LAND_W, LAND_H = 320, 172


def write_header(img, dst, name, w, h):
    """Write an RGB565 C header from a PIL Image."""
    img = img.resize((w, h)).convert("RGB")
    with open(dst, "w") as f:
        f.write(f"// Generated from convert.py - do not edit\n")
        f.write(f"#pragma once\n#include <stdint.h>\n\n")
        f.write(f"#define IMG_{name.upper()}_W {w}\n")
        f.write(f"#define IMG_{name.upper()}_H {h}\n\n")
        f.write(f"static const uint16_t img_{name}[] = {{\n")
        for i, (r, g, b) in enumerate(img.getdata()):
            rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            be = struct.unpack("<H", struct.pack(">H", rgb565))[0]
            f.write(f"0x{be:04X},")
            if (i + 1) % 12 == 0:
                f.write("\n")
        f.write(f"\n}};\n")
    print(f"  {dst} ({w}x{h}, {w*h*2} bytes)")


def convert(src, name=None):
    if name is None:
        name = os.path.splitext(os.path.basename(src))[0]
        name = name.replace(" ", "_").replace("-", "_").lower()
        # strip parentheses and numbers like "banner (1)"
        import re
        name = re.sub(r'[^a-z0-9_]', '', name)
        if not name:
            name = "logo"

    script_dir = os.path.dirname(os.path.abspath(__file__))
    img = Image.open(src)
    print(f"Source: {src} ({img.size[0]}x{img.size[1]})")

    # Portrait: 172x320
    port_path = os.path.join(script_dir, f"img_{name}.h")
    write_header(img, port_path, name, PORT_W, PORT_H)

    # Landscape: 320x172
    land_name = f"{name}_land"
    land_path = os.path.join(script_dir, f"img_{land_name}.h")
    write_header(img, land_path, land_name, LAND_W, LAND_H)

    print("Done!")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <input_image> [name]")
        print(f"  Generates img_<name>.h (portrait {PORT_W}x{PORT_H})")
        print(f"       and img_<name>_land.h (landscape {LAND_W}x{LAND_H})")
        sys.exit(1)
    name = sys.argv[2] if len(sys.argv) > 2 else None
    convert(sys.argv[1], name)
