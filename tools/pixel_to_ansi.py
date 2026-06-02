#!/usr/bin/env python3
"""Convert PNG pixel art to FTXUI-compatible ANSI escape codes."""

import sys
from PIL import Image
import json

def resize_pixel_art(img, max_height=18):
    """Resize while preserving pixel grid."""
    h, w = img.size
    if h > max_height:
        scale = max_height / h
        new_w = int(w * scale)
        img = img.resize((new_w, max_height), Image.NEAREST)
    return img

def is_transparent(pixel, img_mode):
    """Check if pixel is transparent."""
    if img_mode == 'RGBA':
        return pixel[3] < 128
    return False

def rgb_to_ansi_escape(r, g, b):
    """Convert RGB to ANSI 24-bit color escape code."""
    return f"\033[38;2;{r};{g};{b}m"

def pixel_art_to_ansi(image_path, max_height=18, block_char="█"):
    """Convert pixel art to ANSI escape codes (one block per pixel)."""
    img = Image.open(image_path).convert('RGBA')
    img = resize_pixel_art(img, max_height)

    width, height = img.size
    pixels = img.load()

    lines = []
    for y in range(height):
        line = ""
        for x in range(width):
            r, g, b, a = pixels[x, y]
            if a < 128:
                line += "\033[0m "  # Transparent = space
            else:
                line += rgb_to_ansi_escape(r, g, b) + block_char + "\033[0m"
        lines.append(line)

    return lines

def generate_cpp_constant(lines, const_name="WELCOME_PIXEL_ART"):
    """Generate C++ string constant."""
    # Join with escaped newlines
    ansi_str = "\\n".join(lines)

    cpp_code = f'''constexpr const char* {const_name} =
    R"({ansi_str})";
'''
    return cpp_code

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 pixel_to_ansi.py <image.png> [max_height]")
        sys.exit(1)

    image_path = sys.argv[1]
    max_height = int(sys.argv[2]) if len(sys.argv) > 2 else 18

    lines = pixel_art_to_ansi(image_path, max_height)
    cpp_code = generate_cpp_constant(lines)

    print(cpp_code)

    # Also save to file
    output_file = image_path.replace('.png', '.cpp.h')
    with open(output_file, 'w') as f:
        f.write(f"// Generated from {image_path}\n\n")
        f.write(cpp_code)
    print(f"\nSaved to: {output_file}", file=sys.stderr)
