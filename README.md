[日本語版はこちら](./README.ja.md)

<img src="https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/images/teaser.png" title="teaser" />

# STAR BREAKER — simple block-breaking game for MSX

MSX-DOS `.COM` game built with **z88dk** (sccz80), in the same
build → DSK → WebMSX flow that was verified with try16.

Current version: **v6** — MSX2 (SCREEN 5) graphics + PSG BGM/SE,
performance-tuned (see "Performance" below), and a pre-rendered
title logo (see "Baked title image"). The v3 autopilot
benchmark (1800 frames of gameplay, JIFFY-counted in WebMSX):

| build | jiffy ticks | effective fps |
|---|---|---|
| v2 (before tuning) | 8659 | 12.47 |
| v3 (tuned) | 2172 | 49.72 |

## Play / Download

- [Play on WebMSX](https://webmsx.org/?MACHINE=MSXTRJ&DISKA_URL=https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.DSK&FAST_BOOT)
- [STARBRK.DSK (bootable 720KB disk image)](https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.DSK)
- [STARBRK.COM (MSX-DOS program)](https://raw.githubusercontent.com/renatus-xxxx/starbreaker/main/bin/STARBRK.COM)


## Performance (v3 tuning)

The v2 build felt heavy; a code audit found four hot spots, all
fixed inside `src/main.c` (libraries still unmodified):

1. **Block collision** scanned all 65 blocks every frame with two
   integer multiplications per block. Now the ball's position
   selects at most 2×2 candidate blocks from precomputed origin
   tables (`blk_x0[]` / `blk_y0[]`); outside the block band the
   test is a single range check.
2. **Input polling** called BIOS (CALSLT) up to 11 times per poll,
   spinning for the whole frame wait. The keyboard matrix is now
   read directly through the PPI (ports 0xAA/0xA9, high nibble of
   the control port preserved/restored) and latched during the
   wait; the joystick is merged once per frame via BIOS.
3. **HUD redraw** re-issued ~22 VDP commands on every block break.
   Labels are drawn once; only the 5 score/life digits refresh.
4. **PSG writes** went through BIOS WRTPSG (one CALSLT each).
   Registers are now written directly (ports 0xA0/0xA1) with a
   shadow array that suppresses unchanged values. Sprite
   attributes (3 planes, contiguous) are moved with one `ldirvm`
   per frame instead of three `putspr` calls.

The benchmark harness is compiled only with `-DBENCH`
(autopilot paddle, fixed 1800 frames, prints
`BENCH frames=1800 jiffy=NNNN fps=XX.XX` on the DOS screen).

A later pass removed a visible hitch when the ball broke a
block: the hit frame used to pay for the block erase (background
restore) *and* the score redraw, which pushed it past the frame
boundary so the ball appeared to stop on the block. Both are now
deferred to the next frame, and HUD digits are redrawn only when
they change. Measured with a hit-frame slip counter added to the
bench (frames following a hit that took ≥2 ticks, out of 30
hits): ~30 before, 8 with the HUD deferral, **3** with the erase
also deferred; effective fps 49.65 → 55.78 in the same run.

## Baked title image (v4)

The title lettering is no longer drawn glyph by glyph. The logo
(STAR in yellow, BREAKER in green, white gloss, black outline)
and the two small lines under it are a pre-rendered image, baked
from the game's own palette/font data by
`tools/gen_title_image.py` into PackBits RLE data
(`src/title_img.h`, 6,103 bytes) and decoded straight to VRAM by
a small assembly blitter (which first waits for the VDP command
engine to go idle — CPU VRAM writes during command execution
land at the command's address pointer). Total title draw:
**~205 → ~6 JIFFY ticks**. The blinking "PRESS SPACE TO START"
prompt at y=136 stays dynamic, between the two baked bands.

## More baked screens (v5)

The same trick now covers every heavy static draw, because the
PSG sequencer advances once per frame — any long draw is heard
as the BGM cutting out:

| draw | before | after |
|---|---|---|
| game setup (total) | 53 ticks | **16 ticks** |
| — of which HUD labels | 32 | 1 |
| — of which block field | 8 | 2 |
| GAME OVER panel | 91 ticks | **2 ticks** |
| GAME CLEAR panel | 101 ticks | **2 ticks** |

The full block field, the HUD label strip (normal/demo) and
both message panels are baked by `tools/gen_opt_images.py` into
`src/field_img.h` (7.5 KB of RLE) and blitted like the title.
The panels' text is wider than the frame and overflows it on
both sides, exactly as the glyph version drew it; the overflow
is baked as side strips so nothing is clipped. Remaining setup
cost is the HUD digits (11 ticks), which are dynamic.

## Stage intro & controls (v6)

Stage start is now a small presentation: the five block rows
cascade in from the top (one row every 4 frames, ~0.33 s) with a
rising five-note jingle on the SE channel, each note landing on
its row. Every frame still goes through the frame wait, so the
BGM never stalls; input is accepted once the cascade completes.

ESC no longer leaves the program: in game it returns to the
title screen, and the message panels offer SPACE (retry) or ESC
(title) — their hint reads "SPACE:RETRY  ESC:TITLE". The DOS
exit was removed entirely (the title screen does not advertise
or accept it anymore) to make an accidental quit impossible.

## Attract demo

Leave the title screen alone for ~5 seconds and a demo game
starts (AI paddle, "DEMO" in the HUD). Any key returns to the
title; game over / clear also returns to the title, so the demo
loops while the machine idles.

## Libraries

Graphics / sprite / input are based on **msxalib** and **msxclib**
by yamayamaru, copied unmodified into `src/`:

- Source: https://github.com/yamayamaru/yamayamaru-storage-place/tree/main/source-code_2023-09-30
- Files: `msxalib.h` (MD5 `8e8f731a3b9fc1e63f8d3aa18a2b6c6c`),
  `msxalib.c` (MD5 `7e5733cbb87c85c8923a7879160e5fc0`),
  `msxclib.h` (MD5 `b27d4e915dc55ab3a4cf8e831f4d3b39`),
  `msxclib.c` (MD5 `3809e1e7a3b11d8b744e0f97d83c1ed7`)

Used from the library: `ginit`, `vpoke`, `filvrm`, `ldirvm`,
`ldirmv`, `boxfil`, `line_m`, `cpym2v`, `palett`, `rnd`/`srnd`,
`screen`, `color`, `inispr`, `sprite`, `putspr`, `colspr`,
`gtstck`, `gttrig`, `kilbuf`, `gicini`, and `calbio` underneath
them. (Keyboard input and PSG writes bypass the BIOS in v3 for
speed — see "Performance".)

## Game spec

- MSX2 only: SCREEN 5 (256×212 bitmap, V9938) with custom palette,
  starfield/stripe background, metallic walls, glossy blocks
  (VDP commands `boxfil` / `line_m`); text rendered with `cpym2v`
  using an embedded 8x8 font: "8x8 ORIGINAL FONT 1 fix" from the
  MSXPen shared code
  (https://msxpen.com/codes/-Nq8q6wabU6mJ4onDmYE), glyphs
  0x20..0x7F in `src/main.c`
- Blocks: 13 cols × 5 rows = 65 blocks, 10 points each
- Paddle: 2 sprites (32 px wide), ball: 1 sprite, 16×16 sprite
  mode 2 with per-line colors (`colspr`)
- BGM + SE on the PSG: futuristic synthwave-style loop —
  fast arpeggio (Am–F–C–G, 3 frames/step) on ch A with a plucky
  decay over an octave-bouncing pulse bass on ch B; effects
  (ch C) for paddle/wall/block/launch/miss/clear/over
- Controls: cursor keys **or** joystick port 1; SPACE / trigger A
  launches the ball; ESC returns to the title screen
- 3 lives; ball missed = life lost; all blocks cleared = GAME CLEAR
- Frame sync on JIFFY (0xFC9E), 60 fps target (NTSC)

## Build (Linux)

```sh
make -f Makefile.linux        # -> build-linux/STARBRK.COM
make -f Makefile.linux dsk    # -> build-linux/STARBRK.DSK
```

The DSK is built from try16's shipped `TRY16.DSK` as a template
(keeps MSX-DOS), replacing `TRY16.COM` with `STARBRK.COM` and
rewriting `AUTOEXEC.BAT` to run `starbrk` (`tools/make_dsk.py`).

## Layout

```
starbreaker/
├─ Makefile.linux
├─ README.md / README.ja.md
├─ images/teaser.png
├─ bin/                 # STARBRK.COM / STARBRK.DSK (release binaries)
├─ src/
│  ├─ main.c            # the game
│  ├─ title_img.h       # baked title image (generated)
│  ├─ field_img.h       # baked field/HUD/panels (generated)
│  ├─ msxalib.{h,c}     # yamayamaru library (unmodified)
│  └─ msxclib.{h,c}     # yamayamaru library (unmodified)
├─ tools/make_dsk.py, gen_title_image.py, gen_opt_images.py
└─ build-linux/         # STARBRK.COM / STARBRK.DSK (generated)
```
