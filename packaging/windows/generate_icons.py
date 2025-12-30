#!/usr/bin/env python3
"""
Generate retro-style CP/M icons for Windows Store packaging.
Black background with green dot-matrix text.

Requires: pip install Pillow
"""

from PIL import Image, ImageDraw, ImageFont
import os

# Colors
BLACK = (0, 0, 0)
GREEN = (0, 255, 0)  # Classic terminal green

# Dot matrix font pattern (5x7 characters)
# Each character is a list of 7 rows, each row is 5 bits
FONT_5X7 = {
    'A': [
        0b01110,
        0b10001,
        0b10001,
        0b11111,
        0b10001,
        0b10001,
        0b10001,
    ],
    'C': [
        0b01110,
        0b10001,
        0b10000,
        0b10000,
        0b10000,
        0b10001,
        0b01110,
    ],
    'E': [
        0b11111,
        0b10000,
        0b10000,
        0b11110,
        0b10000,
        0b10000,
        0b11111,
    ],
    'M': [
        0b10001,
        0b11011,
        0b10101,
        0b10101,
        0b10001,
        0b10001,
        0b10001,
    ],
    'P': [
        0b11110,
        0b10001,
        0b10001,
        0b11110,
        0b10000,
        0b10000,
        0b10000,
    ],
    'U': [
        0b10001,
        0b10001,
        0b10001,
        0b10001,
        0b10001,
        0b10001,
        0b01110,
    ],
    '>': [
        0b10000,
        0b01000,
        0b00100,
        0b00010,
        0b00100,
        0b01000,
        0b10000,
    ],
    '/': [
        0b00001,
        0b00010,
        0b00010,
        0b00100,
        0b01000,
        0b01000,
        0b10000,
    ],
    ' ': [
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
        0b00000,
    ],
}


def draw_char_dotmatrix(draw, char, x, y, dot_size, dot_spacing, color):
    """Draw a single character using dot matrix pattern."""
    if char not in FONT_5X7:
        return 0

    pattern = FONT_5X7[char]
    char_width = 5 * (dot_size + dot_spacing)

    for row_idx, row in enumerate(pattern):
        for col_idx in range(5):
            if row & (1 << (4 - col_idx)):
                dx = x + col_idx * (dot_size + dot_spacing)
                dy = y + row_idx * (dot_size + dot_spacing)
                draw.ellipse([dx, dy, dx + dot_size - 1, dy + dot_size - 1], fill=color)

    return char_width


def draw_text_dotmatrix(draw, text, x, y, dot_size, dot_spacing, color):
    """Draw text string using dot matrix characters."""
    cursor_x = x
    char_spacing = dot_size  # Extra space between characters

    for char in text.upper():
        width = draw_char_dotmatrix(draw, char, cursor_x, y, dot_size, dot_spacing, color)
        cursor_x += width + char_spacing

    return cursor_x - x


def calculate_text_width(text, dot_size, dot_spacing):
    """Calculate the width of dot matrix text."""
    char_width = 5 * (dot_size + dot_spacing)
    char_spacing = dot_size
    return len(text) * char_width + (len(text) - 1) * char_spacing


def create_icon(size, output_path):
    """Create a single icon at the specified size."""
    img = Image.new('RGB', (size, size), BLACK)
    draw = ImageDraw.Draw(img)

    # Scale dot size based on icon size
    if size <= 50:
        dot_size = max(2, size // 20)
        dot_spacing = 1
    elif size <= 100:
        dot_size = max(3, size // 18)
        dot_spacing = max(1, dot_size // 3)
    else:
        dot_size = max(4, size // 16)
        dot_spacing = max(2, dot_size // 3)

    # Draw "A>" on top line
    line1 = "A>"
    line1_width = calculate_text_width(line1, dot_size, dot_spacing)

    # Draw "CPM" on bottom line
    line2 = "CPM"
    line2_width = calculate_text_width(line2, dot_size, dot_spacing)

    # Calculate line heights
    line_height = 7 * (dot_size + dot_spacing)
    total_height = 2 * line_height + dot_size * 2  # 2 lines + spacing

    # Center vertically
    start_y = (size - total_height) // 2

    # Draw first line (A>) - left aligned with small margin
    margin = size // 10
    draw_text_dotmatrix(draw, line1, margin, start_y, dot_size, dot_spacing, GREEN)

    # Draw second line (CPM) - centered
    line2_x = (size - line2_width) // 2
    line2_y = start_y + line_height + dot_size * 2
    draw_text_dotmatrix(draw, line2, line2_x, line2_y, dot_size, dot_spacing, GREEN)

    img.save(output_path, 'PNG')
    print(f"Created: {output_path} ({size}x{size})")


def create_wide_icon(width, height, output_path):
    """Create a wide format icon."""
    img = Image.new('RGB', (width, height), BLACK)
    draw = ImageDraw.Draw(img)

    # Use height to determine scale
    dot_size = max(4, height // 16)
    dot_spacing = max(2, dot_size // 3)

    # Draw "A> CPM EMU" or similar
    text = "A> CPM"
    text_width = calculate_text_width(text, dot_size, dot_spacing)

    line_height = 7 * (dot_size + dot_spacing)

    # Center both horizontally and vertically
    x = (width - text_width) // 2
    y = (height - line_height) // 2

    draw_text_dotmatrix(draw, text, x, y, dot_size, dot_spacing, GREEN)

    img.save(output_path, 'PNG')
    print(f"Created: {output_path} ({width}x{height})")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    assets_dir = os.path.join(script_dir, 'Assets')
    os.makedirs(assets_dir, exist_ok=True)

    print("Generating CP/M Emulator icons...")
    print()

    # Square icons for Windows Store
    square_sizes = [
        (44, 'Square44x44Logo.png'),
        (50, 'StoreLogo.png'),
        (71, 'SmallTile.png'),
        (150, 'Square150x150Logo.png'),
        (310, 'LargeTile.png'),
    ]

    for size, filename in square_sizes:
        create_icon(size, os.path.join(assets_dir, filename))

    # Scaled versions
    scaled_sizes = [
        (44, 100, 'Square44x44Logo.scale-100.png'),
        (55, 125, 'Square44x44Logo.scale-125.png'),
        (66, 150, 'Square44x44Logo.scale-150.png'),
        (88, 200, 'Square44x44Logo.scale-200.png'),
        (176, 400, 'Square44x44Logo.scale-400.png'),
        (150, 100, 'Square150x150Logo.scale-100.png'),
        (188, 125, 'Square150x150Logo.scale-125.png'),
        (225, 150, 'Square150x150Logo.scale-150.png'),
        (300, 200, 'Square150x150Logo.scale-200.png'),
        (600, 400, 'Square150x150Logo.scale-400.png'),
    ]

    for size, scale, filename in scaled_sizes:
        create_icon(size, os.path.join(assets_dir, filename))

    # Wide icon
    create_wide_icon(310, 150, os.path.join(assets_dir, 'Wide310x150Logo.png'))

    # Splash screen (620x300)
    create_wide_icon(620, 300, os.path.join(assets_dir, 'SplashScreen.png'))

    print()
    print("Done! Icons created in:", assets_dir)


if __name__ == '__main__':
    main()
