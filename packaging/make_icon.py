#!/usr/bin/env python3
# Génère packaging/neost.png (256×256) — icône AppImage/.app de NeoST.
# Lettres « ST » en police pixel 5×7 vert TOS sur fond sombre, scanlines CRT.
# Régénérer : python3 packaging/make_icon.py  (nécessite Pillow).
from PIL import Image, ImageDraw

SIZE = 256
BG = (18, 18, 30, 255)        # fond sombre
FG = (0, 190, 60, 255)        # vert « desktop TOS »
SHADOW = (0, 80, 25, 255)

GLYPHS = {
    "S": [".####",
          "#....",
          "#....",
          ".###.",
          "....#",
          "....#",
          "####."],
    "T": ["#####",
          "..#..",
          "..#..",
          "..#..",
          "..#..",
          "..#..",
          "..#.."],
}

img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=40, fill=BG)

cell = 18
text = "ST"
cols = len(text) * 5 + (len(text) - 1)          # 5 colonnes/glyphe + 1 d'espace
x0 = (SIZE - cols * cell) // 2
y0 = (SIZE - 7 * cell) // 2
for i, ch in enumerate(text):
    gx = x0 + i * 6 * cell
    for row, line in enumerate(GLYPHS[ch]):
        for col, c in enumerate(line):
            if c == "#":
                x, y = gx + col * cell, y0 + row * cell
                d.rectangle([x + 3, y + 3, x + cell + 1, y + cell + 1], fill=SHADOW)
                d.rectangle([x, y, x + cell - 2, y + cell - 2], fill=FG)

# Scanlines CRT discrètes (1 px sombre toutes les 4 lignes, dans le carré arrondi).
mask = Image.new("L", (SIZE, SIZE), 0)
ImageDraw.Draw(mask).rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=40, fill=255)
scan = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
sd = ImageDraw.Draw(scan)
for y in range(0, SIZE, 4):
    sd.line([(0, y), (SIZE, y)], fill=(0, 0, 0, 60))
img.paste(Image.alpha_composite(img, scan), (0, 0), mask)

out = __file__.rsplit("/", 1)[0] + "/neost.png"
img.save(out)
print("écrit :", out)
