"""Builds per-watch image resources from the source art in /art, plus src/c/art_geo.h.
Run: python3 tools/make_assets.py   (needs Pillow)

Car sprite sheets: 8 frames per row, 200x96 cells. Row 0 = trunk/tailgate opening, row 1 = frunk
opening, optional row 2 = Cybertruck tonneau opening. Frame 0 is the closed car.
Each model is stored as one closed image plus small "patches" covering only the part that moves,
which keeps the app small and lets the frunk and trunk animate independently."""
import json, os
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, 'art')
OUT = os.path.join(ROOT, 'resources', 'images', 'car')
HEADER = os.path.join(ROOT, 'src', 'c', 'art_geo.h')

CELL_W, CELL_H = 200, 96
PLATS = ['basalt', 'chalk', 'diorite', 'emery', 'flint', 'gabbro']
BW = {'diorite', 'flint'}
FIT = {'basalt': (136, 56), 'diorite': (136, 56), 'flint': (136, 56), 'chalk': (124, 46),
       'emery': (186, 84), 'gabbro': (186, 90)}
SEAT_H = {'basalt': 126, 'diorite': 126, 'flint': 126, 'chalk': 116, 'emery': 186, 'gabbro': 170}

# index order used by the watch: 0 Model 3, 1 Model S, 2 Model X, 3 Model Y, 4 Cybertruck
MODELS = [('model3', (177, 46)), ('modelS', (179, 49)), ('modelX', (180, 44)), ('modelY', (179, 41)),
          ('cybertruck', (172, 50))]

# Seat maps (200x228 art). Boxes are x, y, w, h in art pixels.
# Items: wheel, driver, passenger, rear L, rear C, rear R, third L, third R, windshield, rear window (bed on Cybertruck)
NONE = (0, 0, 0, 0)
SEATMAPS = [
    ('model3-seat-heater-map', [(70, 53, 20, 20), (68, 72, 24, 38), (107, 72, 24, 38), (67, 115, 22, 35), (91, 115, 18, 35),
                                (111, 115, 22, 35), NONE, NONE, (65, 42, 68, 9), (74, 185, 52, 13)]),
    ('modelS-seat-heater-map', [(66, 64, 21, 21), (68, 87, 22, 28), (110, 87, 22, 28), (65, 123, 22, 30), (89, 123, 22, 30),
                                (113, 123, 22, 30), NONE, NONE, (62, 35, 76, 11), (68, 187, 65, 15)]),
    ('modelY-seat-heater-map', [(66, 64, 21, 21), (68, 87, 22, 28), (110, 87, 22, 28), (65, 123, 22, 30), (89, 123, 22, 30),
                                (113, 123, 22, 30), NONE, NONE, (62, 35, 76, 11), (68, 187, 65, 15)]),
    ('modelY-seat-heater-map-7-seat', [(66, 64, 21, 21), (68, 87, 22, 28), (110, 87, 22, 28), (65, 123, 22, 26), (89, 123, 22, 26),
                                       (113, 123, 22, 26), (77, 158, 21, 26), (102, 158, 21, 26), (62, 35, 76, 11), (68, 187, 65, 15)]),
    ('modelX-seat-heater-map', [(64, 64, 21, 21), (62, 88, 27, 28), (111, 88, 27, 28), (63, 125, 28, 26), NONE,
                                (109, 125, 28, 26), (72, 160, 26, 27), (102, 160, 26, 27), (58, 37, 85, 11), (64, 192, 73, 14)]),
    ('modelX-seat-heater-map-7-seat', [(64, 64, 21, 21), (62, 88, 27, 28), (111, 88, 27, 28), (59, 125, 25, 26), (88, 125, 24, 26),
                                       (116, 125, 25, 26), (72, 160, 26, 27), (102, 160, 26, 27), (58, 37, 85, 11), (64, 192, 73, 14)]),
    ('cybertruck-seat-heater-map', [(64, 58, 21, 21), (62, 81, 27, 27), (111, 81, 27, 27), (59, 116, 25, 25), (88, 116, 24, 25),
                                    (116, 116, 25, 25), NONE, NONE, (60, 37, 81, 10), (61, 153, 79, 53)]),
]


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


def dither_bw(im):
    """Two-tone with a 2x2 ordered pattern, so mid-gray car panels stay visible as texture."""
    g = im.convert('L'); px = g.load()
    out = Image.new('1', g.size, 0); op = out.load()
    th = ((32, 160), (224, 96))
    for y in range(g.height):
        for x in range(g.width):
            op[x, y] = 1 if px[x, y] > th[y & 1][x & 1] else 0
    return out


def finish(im, plat, dither=False):
    if plat in BW and dither:
        return dither_bw(im)
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


def union(boxes):
    boxes = [b for b in boxes if b]
    if not boxes:
        return None
    return (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))


def save_variants(img_for_plat, name, resources):
    for plat in PLATS:
        img_for_plat(plat).save(os.path.join(OUT, '%s~%s.png' % (name, plat)))
    img_for_plat('basalt').save(os.path.join(OUT, name + '.png'))
    resources.append({'type': 'bitmap', 'name': name.upper(), 'file': 'images/car/%s.png' % name})


def build_cars(resources):
    geo = {p: [] for p in PLATS}
    has_tonneau = False
    for mi, (key, port) in enumerate(MODELS):
        sheet = Image.open(os.path.join(ART, key + '-opening-sprites.png')).convert('RGB')
        rows = sheet.height // CELL_H
        frames = [[sheet.crop((f * CELL_W, r * CELL_H, (f + 1) * CELL_W, (r + 1) * CELL_H)) for f in range(8)] for r in range(rows)]
        closed = frames[0][0]
        # area holding the car in any frame
        crop = union([fr.getbbox() for row in frames for fr in row])
        crop = (max(0, crop[0] - 1), max(0, crop[1] - 1), min(CELL_W, crop[2] + 1), min(CELL_H, crop[3] + 1))
        cw, chh = crop[2] - crop[0], crop[3] - crop[1]
        # moving part of each row (source pixels, relative to crop), padded for the resampling filter
        moving = []
        for r in range(min(rows, 3)):
            bb = union([ImageChops.difference(closed, fr).getbbox() for fr in frames[r][1:]])
            if r == 2 and bb:
                has_tonneau = True
            moving.append(None if not bb else (bb[0] - crop[0] - 3, bb[1] - crop[1] - 3, bb[2] - crop[0] + 3, bb[3] - crop[1] + 3))
        for plat in PLATS:
            mw, mh = FIT[plat]
            k = min(mw / cw, mh / chh)
            w, h = round(cw * k), round(chh * k)
            entry = {'w': w, 'h': h, 'port': (round((port[0] - crop[0]) * k), round((port[1] - crop[1]) * k)), 'patch': []}
            for r in range(3):
                mv = moving[r] if r < len(moving) else None
                if not mv:
                    entry['patch'].append((0, 0, 0, 0)); continue
                x0, y0 = max(0, int(mv[0] * k) - 1), max(0, int(mv[1] * k) - 1)
                x1, y1 = min(w, int(mv[2] * k + 0.999) + 1), min(h, int(mv[3] * k + 0.999) + 1)
                entry['patch'].append((x0, y0, x1 - x0, y1 - y0))
            geo[plat].append(entry)
        def scaled(fr, plat):
            g = geo[plat][mi]
            return finish(scale(fr.crop(crop), w=g['w'], h=g['h']), plat, dither=True)
        save_variants(lambda plat: scaled(closed, plat), 'car%d_base' % mi, resources)
        for r, tag in ((0, 't'), (1, 'f'), (2, 'n')):
            if r >= rows or not moving[r]:
                continue
            for f in range(1, 8):
                def patch(plat, fr=frames[r][f], r=r):
                    x, y, pw, ph = geo[plat][mi]['patch'][r]
                    return scaled(fr, plat).crop((x, y, x + pw, y + ph))
                save_variants(patch, 'car%d_%s%d' % (mi, tag, f), resources)
    return geo, has_tonneau


def build_seats(resources):
    for li, (key, _) in enumerate(SEATMAPS):
        seat = Image.open(os.path.join(ART, key + '.png')).convert('RGB')
        save_variants(lambda plat: finish(scale(seat, h=SEAT_H[plat]), plat), 'seats%d' % li, resources)


def build_icons(resources):
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


def build_charger(resources):
    # Charger cord: 8 frames, 40x56 cells. Black becomes transparent. Scaled like the Model 3.
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
        for plat in PLATS:
            k = FIT[plat][0] / 186.0
            if plat == 'chalk':
                k = 128 / 186.0
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


def write_header(geo, has_tonneau, made):
    L = ['// Generated by tools/make_assets.py - do not edit.', '#pragma once', '']
    L.append('#define HAS_TONNEAU_ART %d' % (1 if has_tonneau else 0))
    L.append('// Car sprites, by model: 0 Model 3, 1 S, 2 X, 3 Y, 4 Cybertruck. patch[r] = x, y, w, h of the moving')
    L.append('// part for row r (0 trunk/tailgate, 1 frunk, 2 tonneau); w = 0 means none. port = charge port.')
    L.append('typedef struct { int16_t w, h, port[2], patch[3][4]; } CarGeo;')
    for i, plat in enumerate(PLATS):
        L.append('#%s defined(PBL_PLATFORM_%s)' % ('if' if i == 0 else 'elif', plat.upper()))
        L.append('static const CarGeo CAR_GEO[5] = {')
        for g in geo[plat]:
            L.append('  { %d, %d, { %d, %d }, { %s } },' % (g['w'], g['h'], g['port'][0], g['port'][1],
                     ', '.join('{ %d, %d, %d, %d }' % p for p in g['patch'])))
        L.append('};')
    L.append('#endif')
    L.append('')
    L.append('// Seat maps (200 x 228 art): 0 Model 3, 1 S, 2 Y, 3 Y 7-seat, 4 X 6-seat, 5 X 7-seat, 6 Cybertruck.')
    L.append('// Items: wheel, driver, passenger, rear L, rear C, rear R, third L, third R, windshield, rear window/bed.')
    L.append('static const int16_t SEAT_BOXES[7][10][4] = {')
    for key, boxes in SEATMAPS:
        L.append('  { %s },  // %s' % (', '.join('{ %d, %d, %d, %d }' % b for b in boxes), key))
    L.append('};')
    L.append('// CAR_RES[model][row][frame]: frame 0 = closed car (same for every row), 1-7 = patches; 0 = no art.')
    L.append('static const uint32_t CAR_RES[5][3][8] = {')
    for mi in range(len(MODELS)):
        rows = []
        for tag in 'tfn':
            ids = ['RESOURCE_ID_CAR%d_BASE' % mi] + [('RESOURCE_ID_CAR%d_%s%d' % (mi, tag.upper(), f)) if ('car%d_%s%d' % (mi, tag, f)) in made else '0' for f in range(1, 8)]
            rows.append('{ ' + ', '.join(ids) + ' }')
        L.append('  { ' + ', '.join(rows) + ' },')
    L.append('};')
    open(HEADER, 'w').write('\n'.join(L) + '\n')


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith('.png'):
            os.remove(os.path.join(OUT, f))
    resources = []
    geo, has_tonneau = build_cars(resources)
    build_seats(resources)
    build_icons(resources)
    build_charger(resources)
    write_header(geo, has_tonneau, {r['name'].lower() for r in resources})
    pkg_path = os.path.join(ROOT, 'package.json')
    pkg = json.load(open(pkg_path))
    media = [m for m in pkg['pebble']['resources']['media'] if not m['file'].startswith('images/car/')]
    pkg['pebble']['resources']['media'] = media + resources
    json.dump(pkg, open(pkg_path, 'w'), indent=2)
    total = {p: sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT) if f.endswith('~%s.png' % p)) for p in PLATS}
    print('wrote', len(resources), 'resources; bytes per platform:', total, 'tonneau art:', has_tonneau)


if __name__ == '__main__':
    main()
