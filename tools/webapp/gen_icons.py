#!/usr/bin/env python3
"""Draw the phone page's home-screen icons into tools/webapp/public/.

Run once by hand when the design changes (needs Pillow and an Outfit TTF);
the PNGs are committed, so neither the web build nor CI needs this script.
Design: the round glass in miniature -- black, an amber bezel arc open at the
bottom like the brew arc on Home, and a thin "T". Everything sits inside the
central 80 % so the icon survives Android's maskable crop.

  python3 tools/webapp/gen_icons.py /path/to/Outfit-Regular.ttf
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

AMBER = (0xE8, 0x94, 0x4A)
TEXT = (0xF4, 0xEE, 0xE8)
BG = (0, 0, 0)

font_path = sys.argv[1]
out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'public')
os.makedirs(out_dir, exist_ok=True)


def draw(size):
    ss = 4  # supersample, then downscale: smooth arcs without a vector tool
    s = size * ss
    im = Image.new('RGB', (s, s), BG)
    d = ImageDraw.Draw(im)
    r = s * 0.34
    c = s / 2
    w = max(2, int(s * 0.055))
    # 300 degrees, open at the bottom (PIL angles: 0 = 3 o'clock, clockwise).
    d.arc([c - r, c - r, c + r, c + r], start=120, end=60, fill=AMBER, width=w)
    font = ImageFont.truetype(font_path, int(s * 0.36))
    box = d.textbbox((0, 0), 'T', font=font)
    tw, th = box[2] - box[0], box[3] - box[1]
    d.text((c - tw / 2 - box[0], c - th / 2 - box[1]), 'T', font=font, fill=TEXT)
    return im.resize((size, size), Image.LANCZOS)


for name, size in (('icon-192.png', 192), ('icon-512.png', 512),
                   ('apple-touch-icon.png', 180)):
    path = os.path.join(out_dir, name)
    draw(size).save(path, optimize=True)
    print(path, os.path.getsize(path), 'bytes')
