#!/usr/bin/env python3
"""Draws the 55x56 app icon: the voxel chicken's face on sky blue.
Axis-aligned blocks keep rows identical so the LZ4-compressed icon stays tiny."""
from PIL import Image, ImageDraw

SKY = (144, 236, 252)
WHITE, LIGHT, SIDE = (255, 255, 255), (222, 222, 222), (144, 144, 144)
COMB, COMB_S = (239, 130, 131), (160, 72, 70)
BEAK, BEAK_S = (240, 146, 107), (176, 96, 60)
WATTLE, EYE = (214, 70, 72), (31, 17, 13)

im = Image.new("RGB", (55, 56), SKY)
d = ImageDraw.Draw(im)
def box(x0, y0, x1, y1, c):
    d.rectangle((x0, y0, x1 - 1, y1 - 1), fill=c)

box(8, 50, 46, 56, WHITE)       # shoulders
box(46, 50, 50, 56, SIDE)
box(12, 12, 42, 17, WHITE)      # head top
box(42, 14, 46, 50, SIDE)       # head side
box(12, 17, 42, 50, LIGHT)      # head front
box(21, 5, 33, 9, COMB)         # comb
box(21, 9, 33, 13, COMB_S)
box(16, 23, 19, 28, EYE)        # eyes
box(35, 23, 38, 28, EYE)
box(22, 29, 32, 32, BEAK)       # beak
box(22, 32, 32, 36, BEAK_S)
box(24, 36, 30, 42, WATTLE)     # wattle
im.save("assets/icon.png")
