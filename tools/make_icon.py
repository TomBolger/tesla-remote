"""Draws the app's T logo (2px black pixel lines) and writes the watch menu icon plus the store icons.
Run: python3 tools/make_icon.py   (needs Pillow)"""
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
G = 24  # design grid; mirrored about its centre line

# Left half only (x 0..11); every stroke is exactly 2 pixels thick.
# Roof: an arc whose ends drop away. Cap: a shorter arc under it. Stem: 2px wide down the middle.
ROOF_TOP = {1: 7, 2: 6, 3: 6, 4: 5, 5: 5, 6: 4, 7: 4, 8: 4, 9: 3, 10: 3, 11: 3}
CAP_TOP = {3: 11, 4: 10, 5: 10, 6: 9, 7: 9, 8: 9, 9: 9, 10: 9, 11: 9}
STEM = (11, 21)  # rows the stem covers (x 11 and 12)


def logo_pixels():
    px = set()
    for x, t in list(ROOF_TOP.items()) + list(CAP_TOP.items()):
        for y in (t, t + 1):
            px.add((x, y)); px.add((G - 1 - x, y))
    for y in range(STEM[0], STEM[1] + 1):
        px.add((11, y)); px.add((12, y))
    # a short 2px tab where the cap ends turn down, for a little Pebble-style character
    for y in (12, 13):
        px.add((3, y)); px.add((G - 1 - 3, y))
    return px


def render(size, scale, ink=(0, 0, 0, 255), bg=(0, 0, 0, 0), pad=None):
    im = Image.new('RGBA', (size, size), bg)
    span = G * scale
    ox = (size - span) // 2 if pad is None else pad
    oy = (size - span) // 2 if pad is None else pad
    for x, y in logo_pixels():
        for dx in range(scale):
            for dy in range(scale):
                im.putpixel((ox + x * scale + dx, oy + y * scale + dy), ink)
    return im


def main():
    # Watch launcher icon: 25x25, black on transparent.
    render(25, 1).save(os.path.join(ROOT, 'resources', 'images', 'menu_icon.png'))
    out = os.path.join(ROOT, 'store')
    os.makedirs(out, exist_ok=True)
    white = (255, 255, 255, 255)
    render(144, 5, bg=white).save(os.path.join(out, 'icon-large-144.png'))
    render(48, 2, bg=white).save(os.path.join(out, 'icon-small-48.png'))
    print('icons written')


if __name__ == '__main__':
    main()
