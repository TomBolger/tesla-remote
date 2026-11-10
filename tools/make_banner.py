"""Builds the 720x320 store banner from watch screenshots.
Usage: python3 tools/make_banner.py emery_home.png chalk_climate.png out.png"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 720, 320
BG_TOP, BG_BOT = (12, 14, 18), (28, 34, 44)
ACCENT = (62, 140, 255)
FONT_B = '/usr/share/fonts/opentype/inter/Inter-Bold.otf'
FONT_R = '/usr/share/fonts/opentype/inter/Inter-Medium.otf'


def gradient():
    im = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOT)))
    return im


def watch(shot, round_face=False, scale=1):
    """Simple Pebble-like bezel around a native-size screenshot."""
    s = shot.resize((shot.width * scale, shot.height * scale), Image.NEAREST)
    pad, strap = 14 * scale // 1, 0
    bw, bh = s.width + 2 * pad, s.height + 2 * pad
    body = Image.new('RGBA', (bw, bh), (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    if round_face:
        d.ellipse([0, 0, bw - 1, bh - 1], fill=(58, 62, 70), outline=(110, 116, 126), width=3)
        mask = Image.new('L', s.size, 0)
        ImageDraw.Draw(mask).ellipse([0, 0, s.width - 1, s.height - 1], fill=255)
        body.paste(s, (pad, pad), mask)
    else:
        d.rounded_rectangle([0, 0, bw - 1, bh - 1], radius=22 * scale, fill=(58, 62, 70), outline=(110, 116, 126), width=3)
        body.paste(s, (pad, pad))
    return body


def main():
    a, b, out = sys.argv[1:4]
    im = gradient().convert('RGBA')
    d = ImageDraw.Draw(im)
    tf, sf, mf = ImageFont.truetype(FONT_B, 44), ImageFont.truetype(FONT_R, 19), ImageFont.truetype(FONT_R, 15)
    d.text((34, 70), 'Tesla Remote', font=tf, fill=(255, 255, 255))
    d.rectangle([34, 128, 94, 132], fill=ACCENT)
    d.text((34, 148), 'Lock, climate, seats, frunk,', font=sf, fill=(205, 212, 222))
    d.text((34, 174), 'trunk and charging on your wrist.', font=sf, fill=(205, 212, 222))
    d.text((34, 226), 'Free & open source  ·  No subscription', font=mf, fill=ACCENT)
    d.text((34, 248), 'Model 3 · S · X · Y · Cybertruck', font=mf, fill=(150, 158, 170))
    w1 = watch(Image.open(a).convert('RGB'))
    w2 = watch(Image.open(b).convert('RGB'), round_face=True)
    im.alpha_composite(w2, (366, (H - w2.height) // 2 + 14))
    im.alpha_composite(w1, (W - w1.width - 14, (H - w1.height) // 2))
    im.convert('RGB').save(out)
    print('banner', out)


if __name__ == '__main__':
    main()
