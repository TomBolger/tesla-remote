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

    # Control icons (28x28 white glyphs). Colour watches tint them at draw time;
    # black-and-white watches get a 1-bit white-on-black copy.
    for name in ('climate', 'seat-heat', 'frunk', 'trunk', 'lock'):
        src = Image.open(os.path.join(ART, 'icons', name + '.png')).convert('RGBA')
        a = src.split()[3].point(lambda v: 255 if v > 127 else 0)
        glyph = Image.new('RGBA', src.size, (255, 255, 255, 0)); glyph.putalpha(a)
        base = 'icon_' + name.replace('-', '_')
        glyph.save(os.path.join(OUT, base + '.png'))
        bw = Image.new('1', src.size, 0); bw.paste(1, mask=a)
        for plat in BW:
            bw.save(os.path.join(OUT, '%s~%s.png' % (base, plat)))
        resources.append({'type': 'bitmap', 'name': base.upper(), 'file': 'images/car/%s.png' % base})

    # Charger cord: 8 frames, 40x56 cells. Black becomes transparent. Scaled like the car.
    cs = Image.open(os.path.join(ART, 'charger-sprites.png')).convert('RGB')
    for f in range(8):
        cell = cs.crop((f * 40, 0, f * 40 + 40, 56)).convert('RGBA')
        px = cell.load()
        for y in range(cell.height):
            for x in range(cell.width):
                r, g, b, _ = px[x, y]
                if r + g + b < 40:
                    px[x, y] = (0, 0, 0, 0)
        name = 'charge_%d' % f
        for plat, w in CAR_W.items():
            k = w / 186.0
            im = cell.resize((round(40 * k), round(56 * k)), Image.NEAREST) if k != 1 else cell
            if plat in BW:
                a = im.split()[3]
                out = Image.new('1', im.size, 0); out.paste(1, mask=a)
                out.save(os.path.join(OUT, '%s~%s.png' % (name, plat)))
            else:
                q = pebble_color(im.convert('RGB')).convert('RGBA')
                if k != 1:  # shrunk copies: lift the dark-gray cord so it stays visible
                    qp = q.load()
                    for yy in range(q.height):
                        for xx in range(q.width):
                            r_, g_, b_, _ = qp[xx, yy]
                            if r_ == g_ == b_ and 0 < r_ < 170:
                                qp[xx, yy] = (170, 170, 170, 255)
                q.putalpha(im.split()[3].point(lambda v: 255 if v > 127 else 0))
                q.save(os.path.join(OUT, '%s~%s.png' % (name, plat)))
        Image.open(os.path.join(OUT, '%s~basalt.png' % name)).save(os.path.join(OUT, name + '.png'))
        resources.append({'type': 'bitmap', 'name': name.upper(), 'file': 'images/car/%s.png' % name})

    pkg_path = os.path.join(ROOT, 'package.json')
    pkg = json.load(open(pkg_path))
    media = [m for m in pkg['pebble']['resources']['media'] if not m['file'].startswith('images/car/')]
    pkg['pebble']['resources']['media'] = media + resources
    json.dump(pkg, open(pkg_path, 'w'), indent=2)
    print('wrote', len(resources), 'resources')


if __name__ == '__main__':
    main()
