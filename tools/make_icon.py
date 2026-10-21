"""Draws the icon, an amber block letter A and a cursor bar on a black screen: res/amber.ico
for Windows, res/amber.png for Linux and res/amber.icns for the Mac."""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

AMBER = (255, 176, 0, 255)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"


def draw(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = max(1, size // 8)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=(16, 10, 0, 255), outline=(90, 60, 0, 255))
    font = ImageFont.truetype(FONT, int(size * 0.72))
    box = d.textbbox((0, 0), "A", font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    x = (size * 0.42 - w / 2) - box[0]
    y = (size - h) / 2 - box[1]
    d.text((x, y), "A", font=font, fill=AMBER)
    bar = max(1, size // 14)
    cx = int(size * 0.80)
    d.rectangle([cx, int(size * 0.2), cx + bar, int(size * 0.8)], fill=AMBER)
    return img


res = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "res")
big = draw(256)
big.save(os.path.join(res, "amber.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (256, 256)],
         append_images=[draw(s) for s in (16, 24, 32, 48)])
big.save(os.path.join(res, "amber.png"))
draw(1024).save(os.path.join(res, "amber.icns"), sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256),
                                                         (512, 512), (1024, 1024)])
print("wrote amber.ico, amber.png and amber.icns in", res)
