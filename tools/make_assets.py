"""Builds per-watch image resources from the source art in /art.
Run: python3 tools/make_assets.py   (needs Pillow)"""
import json, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, 'art')
OUT = os.path.join(ROOT, 'resources', 'images', 'car')

# Sprite sheet: 8 frames per row, 200x96 cells. Row 0 = trunk opening, row 1 = frunk opening.
CELL_W, CELL_H = 200, 96
CROP = (6, 5, 192, 76)          # area that holds the car in every frame (186 x 71)
CAR_W = {'basalt': 136, 'diorite': 136, 'flint': 136, 'chalk': 128, 'emery': 186, 'gabbro': 186}
SEAT_H = {'basalt': 126, 'diorite': 126, 'flint': 126, 'chalk': 116, 'emery': 186, 'gabbro': 170}
BW = {'diorite', 'flint'}


def pebble_color(im):
    """Snap to Pebble's 64-colour palette (each channel 0/85/170/255)."""
    im = im.convert('RGBA')
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            q = lambda v: int(round(v / 85.0)) * 85
            px[x, y] = (q(r), q(g), q(b), 255 if a > 127 else 0)
    return im.convert('RGB')


def finish(im, plat):
    if plat in BW:
        return im.convert('L').point(lambda v: 255 if v > 115 else 0).convert('1')  # clean two-tone
    return pebble_color(im)


def scale(im, w=None, h=None):
    if w is None:
        w = round(im.width * h / im.height)
    if h is None:
        h = round(im.height * w / im.width)
    if (w, h) == im.size:
        return im
    return im.resize((w, h), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.open(os.path.join(ART, 'model3-opening-sprites.png')).convert('RGB')
    seat = Image.open(os.path.join(ART, 'model3-seat-heater-map.png')).convert('RGB')
    resources = []
    for row, kind in ((0, 'trunk'), (1, 'frunk')):
        for f in range(8):
            cell = sheet.crop((f * CELL_W, row * CELL_H, (f + 1) * CELL_W, (row + 1) * CELL_H)).crop(CROP)
            name = '%s_%d' % (kind, f)
            for plat, w in CAR_W.items():
                finish(scale(cell, w=w), plat).save(os.path.join(OUT, '%s~%s.png' % (name, plat)))
            finish(scale(cell, w=CAR_W['basalt']), 'basalt').save(os.path.join(OUT, name + '.png'))
            resources.append({'type': 'bitmap', 'name': name.upper(), 'file': 'images/car/%s.png' % name})
    for plat, h in SEAT_H.items():
        finish(scale(seat, h=h), plat).save(os.path.join(OUT, 'seats~%s.png' % plat))
    finish(scale(seat, h=SEAT_H['basalt']), 'basalt').save(os.path.join(OUT, 'seats.png'))
    resources.append({'type': 'bitmap', 'name': 'SEATS', 'file': 'images/car/seats.png'})

    pkg_path = os.path.join(ROOT, 'package.json')
    pkg = json.load(open(pkg_path))
    media = [m for m in pkg['pebble']['resources']['media'] if not m['file'].startswith('images/car/')]
    pkg['pebble']['resources']['media'] = media + resources
    json.dump(pkg, open(pkg_path, 'w'), indent=2)
    print('wrote', len(resources), 'resources')


if __name__ == '__main__':
    main()
