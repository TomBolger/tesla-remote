"""Draws the app-store icons (144x144 and 48x48): a smooth T emblem inside a watch-face ring.
Run: python3 tools/make_store_icon.py   (needs Pillow). The 25px watch launcher icon is made by make_icon.py."""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 1024  # drawn large, then scaled down smoothly


def vgradient(size, top, bottom):
    g = Image.new('RGB', (1, 256))
    for y in range(256):
        t = y / 255
        g.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)))
    return g.resize(size, Image.BICUBIC)


def crescent(outer, inner):
    m = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(m)
    d.ellipse(outer, fill=255)
    d.ellipse(inner, fill=0)
    return m


def emblem():
    m = Image.new('L', (S, S), 0)
    # roof: a wide arc, thick in the middle and tapering to sharp tips
    roof = crescent((150, 316, 874, 1060), (150, 372, 874, 1180))
    roof.paste(0, (0, 452, S, S))  # keep only the top of the arc
    m = ImageChops.lighter(m, roof)
    # cap: a shorter arc under the roof, leaving a clear gap
    cap = crescent((272, 402, 752, 900), (272, 444, 752, 1000))
    cap.paste(0, (0, 492, S, S))
    m = ImageChops.lighter(m, cap)
    # stem: rises from the cap and narrows to a rounded point
    d = ImageDraw.Draw(m)
    d.polygon([(468, 420), (556, 420), (542, 736), (512, 782), (482, 736)], fill=255)
    return m


def build():
    bg_mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(bg_mask).rounded_rectangle((0, 0, S - 1, S - 1), radius=228, fill=255)
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    icon.paste(vgradient((S, S), (26, 34, 48), (8, 10, 15)), (0, 0), bg_mask)

    # watch-face ring with a soft blue glow
    ring = Image.new('L', (S, S), 0)
    rd = ImageDraw.Draw(ring)
    rd.ellipse((112, 112, 912, 912), outline=255, width=46)
    glow = ring.filter(ImageFilter.GaussianBlur(36)).point(lambda v: int(v * 0.55))
    blue = vgradient((S, S), (110, 180, 255), (38, 110, 235))
    icon.paste((62, 140, 255, 255), (0, 0), glow)
    icon.paste(blue, (0, 0), ring)

    # emblem: white to cool gray, with a faint shadow for depth
    em = emblem()
    shadow = em.filter(ImageFilter.GaussianBlur(14)).point(lambda v: int(v * 0.6))
    icon.paste((0, 0, 0, 255), (0, 14), shadow)
    icon.paste(vgradient((S, S), (255, 255, 255), (196, 206, 222)), (0, 0), em)

    # keep everything inside the rounded square
    alpha = ImageChops.multiply(icon.split()[3], bg_mask)
    icon.putalpha(alpha)
    return icon


def main():
    out = os.path.join(ROOT, 'store')
    os.makedirs(out, exist_ok=True)
    big = build()
    big.resize((144, 144), Image.LANCZOS).save(os.path.join(out, 'icon-large-144.png'))
    big.resize((48, 48), Image.LANCZOS).save(os.path.join(out, 'icon-small-48.png'))
    big.resize((512, 512), Image.LANCZOS).save('/tmp/v/icon512.png')
    print('store icons written')


if __name__ == '__main__':
    main()
