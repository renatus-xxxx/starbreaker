#!/usr/bin/env python3
"""Render the STAR BREAKER title screen as a teaser PNG, using the
game's own data (font, sprite patterns, palette, layout constants)
parsed straight out of src/main.c. Not a screenshot: a faithful
re-render (star positions are randomized like the game's rnd())."""
import random, re, sys
from PIL import Image

src = open(sys.argv[1] if len(sys.argv) > 1 else 'src/main.c').read()

def parse_bytes(name):
    m = re.search(name + r'\[[^\]]*\] = \{(.*?)\};', src, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    return [int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', body)]

font = parse_bytes('font_buf')
pat_l = parse_bytes('pat_paddle_l')
pat_r = parse_bytes('pat_paddle_r')
pat_ball = parse_bytes('pat_ball')

# palette: palett(idx, r, g, b) with 3-bit components
pal = {}
for i, r, g, b in re.findall(r'palett\((\d+), (\d+), (\d+), (\d+)\)', src):
    v = lambda n: round(int(n) * 255 / 7)
    pal[int(i)] = (v(r), v(g), v(b))
C_BLACK, C_BG, C_PANEL = 0, 1, 2
C_STEEL_L, C_STEEL_D, C_STEEL = 3, 4, 14
C_LBLUE, C_WHITE = 13, 11

W, H = 256, 212
img = Image.new('RGB', (W, H), pal[C_BLACK])
px = img.load()

def boxfil(x1, y1, x2, y2, c):
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            if 0 <= x < W and 0 <= y < H:
                px[x, y] = pal[c]

def line(x1, y1, x2, y2, c):
    boxfil(min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2), c)

# background: base + panel stripes + stars
boxfil(0, 16, 255, 211, C_BG)
for i in range(24, 212, 32):
    boxfil(8, i, 247, i + 15, C_PANEL)
random.seed(16)
for _ in range(56):
    sx, sy = 10 + random.randrange(236), 28 + random.randrange(176)
    sc = random.choice([C_STEEL, C_LBLUE, C_WHITE])
    boxfil(sx, sy, sx + 1, sy + 1, sc)
# walls
boxfil(0, 16, 255, 23, C_STEEL)
boxfil(0, 16, 7, 211, C_STEEL)
boxfil(248, 16, 255, 211, C_STEEL)
line(0, 16, 255, 16, C_STEEL_L); line(0, 16, 0, 211, C_STEEL_L); line(255, 16, 255, 211, C_STEEL_L)
line(0, 23, 255, 23, C_STEEL_D); line(7, 16, 7, 211, C_STEEL_D); line(248, 16, 248, 211, C_STEEL_D)
# HUD band
boxfil(0, 0, 255, 15, C_BLACK)
# decorative block row (row 0 = yellow)
for c in range(13):
    x, y = 11 + c * 18, 40
    boxfil(x, y, x + 15, y + 9, 6)
    line(x, y, x + 15, y, C_WHITE)
    line(x, y + 9, x + 15, y + 9, C_BLACK)

def draw_char(x, y, ch, fg, bg, scale):
    g = font[(ord(ch) - 0x20) * 8:(ord(ch) - 0x20) * 8 + 8]
    for r in range(8):
        for c in range(8):
            col = fg if g[r] & (0x80 >> c) else bg
            boxfil(x + c * scale, y + r * scale,
                   x + c * scale + scale - 1, y + r * scale + scale - 1, col)

def draw_text(x, y, s, fg, bg, scale):
    for ch in s:
        draw_char(x, y, ch, fg, bg, scale)
        x += 8 * scale + 2

def centered(y, s, fg, bg, scale):
    w = len(s) * 8 * scale + (len(s) - 1) * 2
    draw_text((256 - w) // 2, y, s, fg, bg, scale)

title_cols = [6, 7, 8, 9, 10, 13, 15, 6, 7, 8, 9]
ci = 0
for li, s in enumerate(['STAR', 'BREAKER']):
    w = len(s) * 24 + (len(s) - 1) * 2
    x = (256 - w) // 2
    for ch in s:
        draw_char(x, 54 + li * 30, ch, title_cols[ci], C_BG, 3)
        x += 26
        ci += 1
centered(152, 'CURSOR KEYS OR JOYSTICK', C_LBLUE, C_BG, 1)
centered(166, 'ESC : EXIT TO DOS', C_LBLUE, C_BG, 1)

def sprite(x, y, pat, cols):
    for row in range(16):
        c = cols[row]
        if c == 0:
            continue
        for half in range(2):
            bits = pat[row + half * 16]
            for b in range(8):
                if bits & (0x80 >> b):
                    px[x + half * 8 + b, y + row] = pal[c]

col_paddle = [0,0,0,0,0,0,11,13,10,10,5,4,0,0,0,0]
col_ball = [0,0,0,11,11,6,6,6,6,6,6,7,7,0,0,0]
sprite(112, 187, pat_l, col_paddle)
sprite(128, 187, pat_r, col_paddle)
sprite(123, 174, pat_ball, col_ball)

img = img.resize((W * 3, H * 3), Image.NEAREST)
out = sys.argv[2] if len(sys.argv) > 2 else 'images/teaser.png'
img.save(out)
print('wrote', out, img.size)
