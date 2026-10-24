#!/usr/bin/env python3
"""Bake the D4 title (logo + the two small lines) into RLE data.
Renders the D4 scene at native resolution in palette indices,
diffs against the plain scene to find the logo/lines bounding
boxes, PackBits-compresses each region row by row and emits
title_img.h for the game's asm RLE blitter.
Usage: gen_title_image.py src/main.c <out.h>"""
import sys
OUT_PATH = sys.argv[2] if len(sys.argv) > 2 else '/tmp/title_img.h'
sys.path.insert(0, '/home/hatch/workspace/msx/github/starbreaker/tools')
import importlib.util
spec = importlib.util.spec_from_file_location(
    'd', '/home/hatch/workspace/msx/github/starbreaker/tools/gen_title_ideas_d.py')
# gen_title_ideas_d.py runs saves at import; instead re-implement the
# render inline here by exec'ing only its definitions:
src_d = open('/home/hatch/workspace/msx/github/starbreaker/tools/gen_title_ideas_d.py').read()
defs = src_d.split("save(style_d_letters")[0]
g = {'__name__': 'gendefs'}
import io, os
os.makedirs('/tmp/timg', exist_ok=True)
sys.argv = ['x', sys.argv[1], '/tmp/timg']
exec(compile(defs, 'gen_title_ideas_d', 'exec'), g)

Image = g['Image']
W, H = g['W'], g['H']

plain = g['base_scene']()                      # no title at all
# variant scene without the "ESC : EXIT TO DOS" line (the DOS exit
# was removed from the game; the title must not advertise it)
src_d2 = src_d.replace("centered(166, 'ESC : EXIT TO DOS', LBLUE)", "pass")
g2 = {'__name__': 'gendefs2'}
exec(compile(src_d2.split("save(style_d_letters")[0], 'gen2', 'exec'), g2)
plain_noesc = g2['base_scene']()
logo_img = g['style_d_simple'](g['YELLOW'], g['GREEN'])

# work in palette indices
def to_index(img):
    pal = g['pal']
    lut = {pal[i]: i for i in range(16)}
    px = img.load()
    return [[lut[px[x, y]] for x in range(W)] for y in range(H)]

plain_q = g['quantize'](plain)
full_q = logo_img  # style_d_simple already quantized
pi = to_index(plain_q)
pi_noesc = to_index(g['quantize'](plain_noesc))
fi = to_index(full_q)

def diff_bbox(y0, y1):
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(W):
            if pi[y][x] != fi[y][x]:
                xs.append(x); ys.append(y)
    return min(xs), min(ys), max(xs), max(ys)

logo_bb = diff_bbox(40, 134)
print('logo bbox ', logo_bb)
# the small lines are part of base_scene in the mock (in-game they
# are glyph-drawn); bake the fixed band they live in, taken from
# the scene itself.
lines_bb = (14, 152, 241, 174)

def region(bb, pad_x=2, pad_y=2):
    x0 = max(8, (bb[0] - pad_x) & ~1)
    x1 = min(247, (bb[2] + pad_x) | 1)
    y0 = max(0, bb[1] - pad_y)
    y1 = min(211, bb[3] + pad_y)
    rows = [[fi[y][x] for x in range(x0, x1 + 1)] for y in range(y0, y1 + 1)]
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1, rows

def packbits_row(data):
    out = []
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out += [257 - run, data[i]]
            i += run
        else:
            start = i
            i += run
            while i < n:
                r = 1
                if i < n:
                    while i + r < n and data[i + r] == data[i] and r < 3:
                        r += 1
                    if r >= 3:
                        break
                i += 1
                if i - start >= 128:
                    break
            lit = data[start:i]
            out.append(len(lit) - 1)
            out += lit
    return out

def emit(name, reg):
    x0, y0, w, h, rows = reg
    assert w % 2 == 0
    packed_rows = []
    total = 0
    for row in rows:
        byts = [(row[i] << 4) | row[i + 1] for i in range(0, w, 2)]
        pb = packbits_row(byts)
        packed_rows.append(pb)
        total += len(pb)
    lines = [f'/* {name}: x={x0} y={y0} w={w} h={h} packed={total} raw={w//2*h} */',
             f'#define {name}_X {x0}', f'#define {name}_Y {y0}',
             f'#define {name}_WB {w // 2}', f'#define {name}_H {h}',
             f'static const unsigned char {name.lower()}_data[{total}] = {{']
    flat = [b for pb in packed_rows for b in pb]
    for i in range(0, len(flat), 16):
        lines.append('    ' + ','.join(str(b) for b in flat[i:i + 16]) + ',')
    lines.append('};')
    return '\n'.join(lines), total, w // 2 * h

logo_reg = region(logo_bb)
lines_reg = region(lines_bb)
# lines pixels come from the plain scene (fi == pi in that band)
lx, ly, lw, lh, _ = lines_reg
lines_reg = (lx, ly, lw, lh,
             [[pi_noesc[y][x] for x in range(lx, lx + lw)] for y in range(ly, ly + lh)])
# --- blink prompt band: "PRESS SPACE TO START" at y=136, scale 1.
# In game the prompt is drawn with draw_text5 (glyph cells filled
# C_BG, glyphs in fg); bake both states and blink by blitting.
font = g['font_data']
C_WHITE_I, C_BG_I = 11, 1
ptext = "PRESS SPACE TO START"
pw = len(ptext) * 8 + (len(ptext) - 1)
px0, py0 = (256 - pw) // 2, 136
on_grid = [[pi_noesc[y][x] for x in range(px0, px0 + pw + 1)]
           for y in range(py0, py0 + 8)]
off_grid = [row[:] for row in on_grid]
for ci, ch in enumerate(ptext):
    gx = ci * 9
    glyph = font[(ord(ch) - 0x20) * 8:(ord(ch) - 0x20) * 8 + 8]
    for r in range(8):
        bits = glyph[r]
        for c in range(8):
            on_grid[r][gx + c] = C_WHITE_I if (bits & (0x80 >> c)) else C_BG_I
            off_grid[r][gx + c] = C_BG_I
prompt_on_reg = (px0, py0, pw + 1, 8, on_grid)
prompt_off_reg = (px0, py0, pw + 1, 8, off_grid)

logo_txt, lt, lr = emit('TLOGO', logo_reg)
lines_txt, nt, nr = emit('TLINES', lines_reg)
pon_txt, pt, pr = emit('TPROMPT_ON', prompt_on_reg)
poff_txt, qf, qr = emit('TPROMPT_OFF', prompt_off_reg)
out = ('/* generated by tools/gen_title_image.py - do not edit */\n'
       '#ifndef TITLE_IMG_H\n#define TITLE_IMG_H\n\n'
       + logo_txt + '\n\n' + lines_txt + '\n\n'
       + pon_txt + '\n\n' + poff_txt + '\n\n#endif\n')
open(OUT_PATH, 'w').write(out)
print(f'TLOGO  packed={lt} raw={lr}')
print(f'TLINES packed={nt} raw={nr}')
print(f'TPROMPT packed on={pt} off={qf}')
print('wrote', OUT_PATH)
