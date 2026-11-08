#!/usr/bin/env python3
"""Draws the Wira Bandar program icon (os/windows/wirabandar.ico): a city skyline and a red bus on a blue tile.

Run from this folder: python3 make_wirabandar_icon.py
"""

from PIL import Image, ImageDraw

S = 1024  # drawn large, then scaled down for smooth edges
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# The tile: blue, lighter at the top.
tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
td = ImageDraw.Draw(tile)
for y in range(S):
    t = y / S
    colour = (int(52 + (20 - 52) * t), int(118 + (58 - 118) * t), int(196 + (112 - 196) * t), 255)
    td.line([(0, y), (S, y)], fill=colour)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([24, 24, S - 24, S - 24], radius=190, fill=255)
img.paste(tile, (0, 0), mask)

# The skyline: towers in white and pale grey, the tallest with a spire.
towers = [
    (150, 470, 290, (205, 216, 230)),
    (300, 330, 430, (240, 244, 248)),
    (445, 200, 585, (255, 255, 255)),
    (600, 360, 720, (225, 232, 240)),
    (735, 430, 875, (205, 216, 230)),
]
for x0, top, x1, colour in towers:
    d.rectangle([x0, top, x1, 760], fill=colour)
    # Windows, lit.
    for wy in range(top + 40, 720, 70):
        for wx in range(x0 + 25, x1 - 30, 50):
            d.rectangle([wx, wy, wx + 22, wy + 32], fill=(255, 205, 64))
d.polygon([(495, 200), (515, 90), (535, 200)], fill=(255, 255, 255))

# The road, with its centre line.
d.rectangle([24, 760, S - 24, 880], fill=(52, 56, 62))
for x in range(80, S - 80, 120):
    d.rectangle([x, 815, x + 60, 827], fill=(250, 250, 250))

# A red bus.
d.rounded_rectangle([250, 640, 780, 830], radius=40, fill=(222, 48, 44))
for wx in range(290, 700, 95):
    d.rounded_rectangle([wx, 670, wx + 70, 735], radius=10, fill=(40, 52, 70))
d.rounded_rectangle([700, 670, 755, 790], radius=10, fill=(40, 52, 70))
d.rectangle([250, 760, 780, 772], fill=(255, 214, 64))
for cx in (360, 670):
    d.ellipse([cx - 52, 790, cx + 52, 894], fill=(30, 30, 30))
    d.ellipse([cx - 22, 820, cx + 22, 864], fill=(170, 170, 170))

# Keep it inside the tile.
clipped = Image.new("RGBA", (S, S), (0, 0, 0, 0))
clipped.paste(img, (0, 0), mask)
master = clipped.resize((256, 256), Image.LANCZOS)
master.save("wirabandar.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
master.save("wirabandar.png")
print("wirabandar.ico written")
