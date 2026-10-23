/*
 * STAR BREAKER - a simple block-breaking game for MSX2 (MSX-DOS .COM)
 *
 * Built with z88dk (sccz80) + msxalib / msxclib by yamayamaru
 * (https://github.com/yamayamaru/yamayamaru-storage-place
 *  source-code_2023-09-30, files used unmodified).
 *
 * v2 (MSX2 only):
 *   - SCREEN 5 (256x212 bitmap, V9938) with a custom palette,
 *     gradient/starfield background and glossy blocks drawn with
 *     VDP commands (boxfil / line_m via msxalib).
 *   - Text is drawn with cpym2v (LMMC) from an embedded 8x8 font
 *     (MSXPen "8x8 ORIGINAL FONT 1 fix", ASCII 0x20-0x7F).
 *   - Paddle / ball are 16x16 sprites (sprite mode 2) with
 *     per-line colors set by colspr.
 *   - BGM + sound effects on the PSG (direct port writes, v3)
 *     (WRTPSG): channel A = melody, B = bass, C = effects.
 *   - Input: cursor keys and joystick port 1 (GTSTCK/GTTRIG via
 *     msxclib + SNSMAT fallback), latched while waiting for a
 *     frame so short key pulses are not missed.
 *   - Frame sync: JIFFY (0xFC9E), 1 frame = 1/60 sec (NTSC).
 *   - SPACE / trigger A: launch ball, ESC: exit to MSX-DOS.
 */

#include <msxalib.h>
#include <msxclib.h>

/* ---- system variables ---- */
#define JIFFY   (*(volatile unsigned char *)0xfc9e)

/* ---- palette indices (SCREEN 5, redefined with palett) ---- */
#define C_BLACK   0
#define C_BG      1    /* deep blue        */
#define C_PANEL   2    /* panel blue       */
#define C_STEEL_L 3    /* wall highlight   */
#define C_STEEL_D 4    /* wall shadow      */
#define C_PADDLE  5    /* paddle mid blue  */
#define C_YELLOW  6
#define C_ORANGE  7
#define C_PINK    8
#define C_GREEN   9
#define C_CYAN    10
#define C_WHITE   11
#define C_RED     12
#define C_LBLUE   13
#define C_STEEL   14   /* wall base        */
#define C_PURPLE  15

/* ---- field geometry (pixels) ---- */
#define WALL_L    8          /* left wall inner edge          */
#define WALL_R    248        /* right wall inner edge         */
#define WALL_T    24         /* top wall inner edge (bottom)  */
#define HUD_H     16
#define PADDLE_Y  186        /* sprite register Y             */
#define PADDLE_W  32
#define PADDLE_MIN_X 8
#define PADDLE_MAX_X 216     /* 248 - 32 */
#define PADDLE_SPEED 3
#define PADDLE_TOP   193     /* visual top of the paddle bar  */
#define BALL_SPEED 2

/* ball logical box: sprite is 16x16, ball drawn at offset (3,3), 10x10 */
#define BALL_OX 3
#define BALL_OY 4            /* +1: sprite Y is displayed at Y+1 */

/* ---- blocks ---- */
#define BLK_ROWS  5
#define BLK_COLS  13
#define BLK_X0    11
#define BLK_Y0    40
#define BLK_W     16
#define BLK_H     10
#define BLK_PX    18         /* horizontal pitch */
#define BLK_PY    13         /* vertical pitch   */

static const unsigned char blk_main[BLK_ROWS] = {
    C_YELLOW, C_ORANGE, C_PINK, C_GREEN, C_CYAN
};

/* ---- stars (background decoration) ---- */
#define NSTARS 56
static unsigned char star_x[NSTARS];
static unsigned char star_y[NSTARS];
static unsigned char star_c[NSTARS];

/* ---- font captured from SCREEN 1 ---- */
/* 8x8 font: "8x8 ORIGINAL FONT 1 fix" from the MSXPen shared
   code (https://msxpen.com/codes/-Nq8q6wabU6mJ4onDmYE),
   glyphs 0x20..0x7F embedded, replacing the BIOS font. */
static const unsigned char font_buf[96 * 8] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 0x20  */
    0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00,  /* 0x21 ! */
    0x24,0x24,0x48,0x00,0x00,0x00,0x00,0x00,  /* 0x22 " */
    0x44,0xfe,0x44,0x44,0x44,0xfe,0x44,0x00,  /* 0x23 # */
    0x10,0x7c,0x90,0x7c,0x12,0xfc,0x10,0x00,  /* 0x24 $ */
    0xe2,0xa4,0xe8,0x10,0x2e,0x4a,0x8e,0x00,  /* 0x25 % */
    0x30,0x48,0x30,0x62,0x94,0x8c,0x72,0x00,  /* 0x26 & */
    0x20,0x20,0x40,0x00,0x00,0x00,0x00,0x00,  /* 0x27 ' */
    0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00,  /* 0x28 ( */
    0x20,0x10,0x08,0x08,0x08,0x10,0x20,0x00,  /* 0x29 ) */
    0x00,0x00,0x28,0x10,0x28,0x00,0x00,0x00,  /* 0x2a * */
    0x00,0x00,0x10,0x38,0x10,0x00,0x00,0x00,  /* 0x2b + */
    0x00,0x00,0x00,0x00,0x40,0x40,0x80,0x00,  /* 0x2c , */
    0x00,0x00,0x00,0x7c,0x00,0x00,0x00,0x00,  /* 0x2d - */
    0x00,0x00,0x00,0x00,0x00,0x40,0x00,0x00,  /* 0x2e . */
    0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x00,  /* 0x2f / */
    0x7c,0x86,0x8a,0x92,0xa2,0xc2,0x7c,0x00,  /* 0x30 0 */
    0x10,0x30,0x50,0x10,0x10,0x10,0x10,0x00,  /* 0x31 1 */
    0xfc,0x02,0x02,0x7c,0x80,0x80,0xfe,0x00,  /* 0x32 2 */
    0xfc,0x02,0x02,0x7c,0x02,0x02,0xfc,0x00,  /* 0x33 3 */
    0x82,0x82,0x82,0xfe,0x02,0x02,0x02,0x00,  /* 0x34 4 */
    0xfe,0x80,0x80,0xfc,0x02,0x02,0xfc,0x00,  /* 0x35 5 */
    0x7c,0x80,0x80,0xfc,0x82,0x82,0x7c,0x00,  /* 0x36 6 */
    0xfe,0x82,0x82,0x02,0x02,0x02,0x02,0x00,  /* 0x37 7 */
    0x7c,0x82,0x82,0x7c,0x82,0x82,0x7c,0x00,  /* 0x38 8 */
    0x7c,0x82,0x82,0x7e,0x02,0x02,0x7c,0x00,  /* 0x39 9 */
    0x00,0x30,0x30,0x00,0x30,0x30,0x00,0x00,  /* 0x3a : */
    0x00,0x30,0x30,0x00,0x30,0x30,0x60,0x00,  /* 0x3b ; */
    0x06,0x18,0x60,0x80,0x60,0x18,0x06,0x00,  /* 0x3c < */
    0x00,0x00,0xfe,0x00,0x00,0xfe,0x00,0x00,  /* 0x3d = */
    0xc0,0x30,0x0c,0x02,0x0c,0x30,0xc0,0x00,  /* 0x3e > */
    0x38,0x44,0x44,0x08,0x10,0x00,0x10,0x00,  /* 0x3f ? */
    0x38,0x44,0x9a,0xaa,0xaa,0xac,0x38,0x00,  /* 0x40 @ */
    0x7c,0x82,0x82,0xfe,0x82,0x82,0x82,0x00,  /* 0x41 A */
    0xfc,0x82,0x82,0xfc,0x82,0x82,0xfc,0x00,  /* 0x42 B */
    0x7e,0x80,0x80,0x80,0x80,0x80,0x7e,0x00,  /* 0x43 C */
    0xf8,0x84,0x82,0x82,0x82,0x84,0xf8,0x00,  /* 0x44 D */
    0xfe,0x80,0x80,0xfc,0x80,0x80,0xfe,0x00,  /* 0x45 E */
    0xfe,0x80,0x80,0xfc,0x80,0x80,0x80,0x00,  /* 0x46 F */
    0x7c,0x80,0x80,0x9e,0x82,0x82,0x7c,0x00,  /* 0x47 G */
    0x82,0x82,0x82,0xfe,0x82,0x82,0x82,0x00,  /* 0x48 H */
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,  /* 0x49 I */
    0x02,0x02,0x02,0x02,0x82,0x82,0x7c,0x00,  /* 0x4a J */
    0x82,0x84,0x88,0xf0,0x88,0x84,0x82,0x00,  /* 0x4b K */
    0x80,0x80,0x80,0x80,0x80,0x80,0xfe,0x00,  /* 0x4c L */
    0x82,0xc6,0xaa,0x92,0x82,0x82,0x82,0x00,  /* 0x4d M */
    0x82,0xc2,0xa2,0x92,0x8a,0x86,0x82,0x00,  /* 0x4e N */
    0x7c,0x82,0x82,0x82,0x82,0x82,0x7c,0x00,  /* 0x4f O */
    0xfc,0x82,0x82,0xfc,0x80,0x80,0x80,0x00,  /* 0x50 P */
    0x7c,0x82,0x82,0x92,0x8a,0x84,0x7a,0x00,  /* 0x51 Q */
    0xfc,0x82,0x82,0xfc,0x88,0x84,0x82,0x00,  /* 0x52 R */
    0x7c,0x82,0x80,0x7c,0x02,0x82,0x7c,0x00,  /* 0x53 S */
    0xfe,0x10,0x10,0x10,0x10,0x10,0x10,0x00,  /* 0x54 T */
    0x82,0x82,0x82,0x82,0x82,0x82,0x7c,0x00,  /* 0x55 U */
    0x82,0x82,0x44,0x44,0x28,0x28,0x10,0x00,  /* 0x56 V */
    0x92,0x92,0x92,0xaa,0xaa,0x44,0x44,0x00,  /* 0x57 W */
    0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x00,  /* 0x58 X */
    0x82,0x44,0x28,0x10,0x10,0x10,0x10,0x00,  /* 0x59 Y */
    0xfe,0x04,0x08,0x10,0x20,0x40,0xfe,0x00,  /* 0x5a Z */
    0x38,0x20,0x20,0x20,0x20,0x20,0x38,0x00,  /* 0x5b [ */
    0x82,0x44,0x28,0x7c,0x10,0x7c,0x10,0x00,  /* 0x5c \ */
    0x38,0x08,0x08,0x08,0x08,0x08,0x38,0x00,  /* 0x5d ] */
    0x10,0x28,0x44,0x82,0x00,0x00,0x00,0x00,  /* 0x5e ^ */
    0x00,0x00,0x00,0x00,0x00,0x00,0xfe,0x00,  /* 0x5f _ */
    0x20,0x10,0x08,0x00,0x00,0x00,0x00,0x00,  /* 0x60 ` */
    0x00,0x00,0x38,0x04,0x3c,0x44,0x3c,0x00,  /* 0x61 a */
    0x40,0x40,0x78,0x44,0x44,0x44,0x78,0x00,  /* 0x62 b */
    0x00,0x00,0x3c,0x40,0x40,0x40,0x3c,0x00,  /* 0x63 c */
    0x04,0x04,0x3c,0x44,0x44,0x44,0x3c,0x00,  /* 0x64 d */
    0x00,0x00,0x38,0x44,0x7c,0x40,0x38,0x00,  /* 0x65 e */
    0x0c,0x10,0x7c,0x10,0x10,0x10,0x10,0x00,  /* 0x66 f */
    0x00,0x00,0x3c,0x44,0x3c,0x04,0x38,0x00,  /* 0x67 g */
    0x40,0x40,0x78,0x44,0x44,0x44,0x44,0x00,  /* 0x68 h */
    0x10,0x00,0x10,0x10,0x10,0x10,0x10,0x00,  /* 0x69 i */
    0x08,0x00,0x08,0x08,0x08,0x48,0x30,0x00,  /* 0x6a j */
    0x20,0x20,0x24,0x28,0x30,0x28,0x24,0x00,  /* 0x6b k */
    0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,  /* 0x6c l */
    0x00,0x00,0x68,0x54,0x54,0x54,0x54,0x00,  /* 0x6d m */
    0x00,0x00,0x78,0x44,0x44,0x44,0x44,0x00,  /* 0x6e n */
    0x00,0x00,0x38,0x44,0x44,0x44,0x38,0x00,  /* 0x6f o */
    0x00,0x00,0x78,0x44,0x44,0x78,0x40,0x00,  /* 0x70 p */
    0x00,0x00,0x38,0x44,0x44,0x3c,0x04,0x00,  /* 0x71 q */
    0x00,0x00,0x58,0x64,0x40,0x40,0x40,0x00,  /* 0x72 r */
    0x00,0x00,0x3c,0x40,0x38,0x04,0x78,0x00,  /* 0x73 s */
    0x00,0x10,0x7c,0x10,0x10,0x10,0x0c,0x00,  /* 0x74 t */
    0x00,0x00,0x44,0x44,0x44,0x44,0x3c,0x00,  /* 0x75 u */
    0x00,0x00,0x44,0x44,0x28,0x28,0x10,0x00,  /* 0x76 v */
    0x00,0x00,0x54,0x54,0x54,0x54,0x28,0x00,  /* 0x77 w */
    0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x00,  /* 0x78 x */
    0x00,0x00,0x44,0x44,0x3c,0x04,0x38,0x00,  /* 0x79 y */
    0x00,0x00,0x7c,0x08,0x10,0x20,0x7c,0x00,  /* 0x7a z */
    0x18,0x20,0x20,0x40,0x20,0x20,0x18,0x00,  /* 0x7b { */
    0x10,0x10,0x10,0x00,0x10,0x10,0x10,0x00,  /* 0x7c | */
    0x30,0x08,0x08,0x04,0x08,0x08,0x30,0x00,  /* 0x7d } */
    0x22,0x54,0x88,0x00,0x00,0x00,0x00,0x00,  /* 0x7e ~ */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,  /* 0x7f  */
};

static unsigned char pix[4 + 24 * 24];   /* cpym2v buffer, up to scale 3 */

/* ---- sprites (16x16: left 8 cols x 16 rows, then right 8 cols) ---- */
static const unsigned char pat_paddle_l[32] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0x7f,0xff,0xff,0x7f,0x3f,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00
};
static const unsigned char pat_paddle_r[32] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfe,0xff,0xff,0xfe,0xfc,0x00,0x00,0x00,0x00
};
static const unsigned char pat_ball[32] = {
    0x00,0x00,0x00,0x07,0x0f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x0f,0x07,0x00,0x00,0x00,
    0x00,0x00,0x00,0xe0,0xf0,0xf8,0xf8,0xf8,0xf8,0xf8,0xf8,0xf0,0xe0,0x00,0x00,0x00
};
static const unsigned char col_paddle[16] = {
    0,0,0,0,0,0, C_WHITE, C_LBLUE, C_CYAN, C_CYAN, C_PADDLE, C_STEEL_D, 0,0,0,0
};
static const unsigned char col_ball[16] = {
    0,0,0, C_WHITE, C_WHITE, C_YELLOW, C_YELLOW, C_YELLOW,
    C_YELLOW, C_YELLOW, C_YELLOW, C_ORANGE, C_ORANGE, 0,0,0
};

/* ---- game state ---- */
static unsigned char blocks[BLK_ROWS][BLK_COLS];
static int remaining;
static int score;
static int lives;
static int paddle_x;
static int ball_x, ball_y, ball_dx, ball_dy;
static int ball_on_paddle;
static unsigned char last_jiffy;

enum { ST_TITLE, ST_PLAY, ST_OVER, ST_CLEAR, ST_EXIT, ST_DEMO };

static int demo_mode;   /* attract demo: AI paddle, any key -> title */


#ifdef BENCH
/* Autopilot benchmark (not in the release build): play_game() runs
   with an AI paddle for BENCH_FRAMES frames while JIFFY ticks are
   counted; the effective fps is printed after returning to DOS.
   Build with -DBENCH. */
#define BENCH_FRAMES 1800
static long bench_frames;
static long bench_slips;   /* frames that took >= 2 JIFFY ticks */
static long bench_hits;    /* blocks destroyed */
static long bench_hit_slips;  /* frames right after a hit that slipped */
static int bench_after_hit;
static unsigned int bench_prev_tick;
static int bench_tick_started;
#endif

/* ============================================================
 * Audio (PSG: direct OUT to ports 0xA0/0xA1, register shadow)
 *   A: melody (BGM)   B: bass (BGM)   C: sound effects
 * ============================================================ */
static const unsigned int note_period[12] = {
    428, 404, 381, 360, 340, 320, 302, 285, 269, 254, 240, 226  /* C4..B4 */
};

static unsigned int period_for(int midi)
{
    unsigned int p = note_period[midi % 12];
    int oct = midi / 12 - 1;               /* MIDI 60 (C4) -> octave 4 */
    while (oct > 4) { p >>= 1; oct--; }
    while (oct < 4) { p <<= 1; oct++; }
    return p;
}

/* Futuristic synthwave-style BGM: a fast 16-step arpeggio
   (3 frames per step) cycling Am - F - C - G on channel A with a
   plucky decay, over an octave-bouncing pulse bass on channel B. */
#define BGM_STEPS 64
#define BGM_STEP_FRAMES 3
static const unsigned char bgm_chords[4][4] = {
    { 69, 72, 76, 81 },   /* Am: A4 C5 E5 A5 */
    { 65, 69, 72, 77 },   /* F : F4 A4 C5 F5 */
    { 72, 76, 79, 84 },   /* C : C5 E5 G5 C6 */
    { 67, 71, 74, 79 }    /* G : G4 B4 D5 G5 */
};
static const unsigned char arp_idx[16] = {
    0, 1, 2, 3,  0, 1, 2, 3,  0, 1, 2, 3,  2, 1, 2, 3
};
static const unsigned char bgm_roots[4] = { 45, 41, 36, 43 };  /* A2 F2 C2 G2 */
static int bgm_step;
static int bgm_frame;
static int bgm_on;

/* SE step sequencer on channel C: periods + frames per step */
static const unsigned int *se_notes;
static const unsigned char *se_durs;
static int se_len, se_pos, se_timer, se_active;

static void se_start(const unsigned int *notes, const unsigned char *durs, int len)
{
    se_notes = notes;
    se_durs = durs;
    se_len = len;
    se_pos = 0;
    se_timer = 0;
    se_active = 1;
}

static const unsigned int se_paddle_n[] = { 180 };
static const unsigned char se_paddle_d[] = { 3 };
static const unsigned int se_block_n[] = { 110 };
static const unsigned char se_block_d[] = { 3 };
static const unsigned int se_wall_n[] = { 230 };
static const unsigned char se_wall_d[] = { 2 };
static const unsigned int se_launch_n[] = { 300, 200, 140 };
static const unsigned char se_launch_d[] = { 2, 2, 3 };
static const unsigned int se_miss_n[] = { 200, 260, 340, 460, 620 };
static const unsigned char se_miss_d[] = { 4, 4, 4, 4, 6 };
static const unsigned int se_clear_n[] = { 214, 170, 143, 107, 143, 107 };
static const unsigned char se_clear_d[] = { 6, 6, 6, 6, 6, 10 };
static const unsigned int se_over_n[] = { 214, 254, 320, 428 };
static const unsigned char se_over_d[] = { 7, 7, 7, 12 };

#define SE_PADDLE se_start(se_paddle_n, se_paddle_d, 1)
#define SE_BLOCK  se_start(se_block_n, se_block_d, 1)
#define SE_WALL   se_start(se_wall_n, se_wall_d, 1)
#define SE_LAUNCH se_start(se_launch_n, se_launch_d, 3)
#define SE_MISS   se_start(se_miss_n, se_miss_d, 5)
#define SE_CLEAR  se_start(se_clear_n, se_clear_d, 6)
#define SE_OVER   se_start(se_over_n, se_over_d, 4)

/* PSG registers are written directly through ports 0xA0/0xA1
   (the BIOS WRTPSG path via msxclib's sound() costs a CALSLT per
   register). A shadow copy suppresses unchanged writes. */
static unsigned char psg_shadow[16];

static void psg_write(int reg, int val)
{
#asm
    ld hl,2
    add hl,sp
    ld e,(hl)          ; second param (val)
    inc hl
    inc hl
    ld a,(hl)          ; first param (reg)
    out (0a0h),a
    ld a,e
    out (0a1h),a
#endasm
}

static void psg_set(int reg, int val)
{
    if (psg_shadow[reg] != (unsigned char)val) {
        psg_shadow[reg] = (unsigned char)val;
        psg_write(reg, val);
    }
}

static void psg_tone(int ch, unsigned int period, int vol)
{
    psg_set(ch * 2, period & 0xff);
    psg_set(ch * 2 + 1, (period >> 8) & 0x0f);
    psg_set(8 + ch, vol);
}

static void audio_init(void)
{
    int i;
    for (i = 0; i < 16; i++) psg_shadow[i] = 0xff;  /* force first writes */
    gicini();
    psg_set(7, 0x38);          /* tone A/B/C on, noise off */
    psg_set(8, 0);
    psg_set(9, 0);
    psg_set(10, 0);
    bgm_step = 0;
    bgm_frame = 0;
    bgm_on = 1;
    se_active = 0;
}

static void audio_stop(void)
{
    psg_set(8, 0);
    psg_set(9, 0);
    psg_set(10, 0);
    psg_set(7, 0x3f);
}

/* called once per frame from wait_frame() */
static void audio_update(void)
{
    /* ---- SE (channel C) ---- */
    if (se_active) {
        if (se_timer == 0) {
            if (se_pos >= se_len) {
                se_active = 0;
                psg_set(10, 0);
            } else {
                psg_tone(2, se_notes[se_pos], 13);
                se_timer = se_durs[se_pos];
                se_pos++;
            }
        }
        if (se_active) se_timer--;
    }
    /* ---- BGM (channels A/B) ---- */
    if (bgm_on) {
        int bar = bgm_step >> 4;
        if (bgm_frame == 0) {
            /* arpeggio note, retriggered every step (plucky synth) */
            psg_tone(0, period_for(bgm_chords[bar][arp_idx[bgm_step & 15]]),
                     12);
            /* pulse bass: new note every 4 steps, octave bouncing */
            if ((bgm_step & 3) == 0) {
                int sub = (bgm_step >> 2) & 3;
                psg_tone(1, period_for(bgm_roots[bar] + ((sub & 1) ? 12 : 0)),
                         9);
            }
        } else {
            psg_set(8, 12 - bgm_frame * 3);         /* fast pluck decay */
        }
        /* bass volume sag across its 12-frame note span */
        psg_set(9, 9 - (((bgm_step & 3) * 3 + bgm_frame) >> 2));
        bgm_frame++;
        if (bgm_frame >= BGM_STEP_FRAMES) {
            bgm_frame = 0;
            bgm_step = (bgm_step + 1) & (BGM_STEPS - 1);
        }
    }
}

/* ============================================================
 * Input
 *   Keyboard: the matrix is read DIRECTLY through the PPI
 *   (port 0xAA = row select, 0xA9 = row data) with a few Z80
 *   instructions -- no BIOS CALSLT. It is polled during the
 *   frame wait and latched, so short key pulses are not missed.
 *   Joystick: read once per frame via BIOS (gtstck/gttrig).
 *   Row 8: bit7=RIGHT bit4=LEFT bit0=SPACE, row 7: bit2=ESC
 *   (a bit is 0 while the key is pressed).
 * ============================================================ */
static unsigned char kbd_row(int row)
{
#asm
    ld hl,2
    add hl,sp
    ld e,(hl)          ; row
    in a,(0aah)
    ld d,a             ; save PPI control
    and 0f0h           ; keep the high nibble (other functions)
    or e               ; select the row in the low nibble
    out (0aah),a
    in a,(0a9h)        ; matrix data for the row
    ld l,a
    ld h,0
    ld a,d
    out (0aah),a       ; restore PPI control
#endasm
}

static unsigned char lat_left, lat_right, lat_trig, lat_esc;

static void poll_input(void)
{
    unsigned char r8 = kbd_row(8);
    unsigned char r7;
    if ((r8 & 0x80) == 0) lat_right = 1;
    if ((r8 & 0x10) == 0) lat_left = 1;
    if ((r8 & 0x01) == 0) lat_trig = 1;
    r7 = kbd_row(7);
    if ((r7 & 0x04) == 0) lat_esc = 1;
}

/* joystick, once per frame: merge into the keyboard latches */
static void merge_joystick(void)
{
    unsigned char st = gtstck(1);
    if (st == 6 || st == 7 || st == 8) lat_left = 1;
    if (st == 2 || st == 3 || st == 4) lat_right = 1;
    if (gttrig(1)) lat_trig = 1;
}

static void wait_frame(void)
{
    unsigned char now;
    lat_left = lat_right = lat_trig = lat_esc = 0;
    do {
        poll_input();
        now = JIFFY;
    } while (now == last_jiffy);
    last_jiffy = now;
    audio_update();
}

/* wait for trigger press (edge), 1 = trigger, 0 = ESC */
static int wait_trigger_or_esc(void)
{
    for (;;) {                       /* wait for release first */
        wait_frame();
        merge_joystick();
        if (lat_esc) return 0;
        if (!lat_trig) break;
    }
    for (;;) {
        wait_frame();
        merge_joystick();
        if (lat_esc) return 0;
        if (lat_trig) return 1;
    }
}

/* ============================================================
 * Graphics (SCREEN 5)
 * ============================================================ */
static void setup_palette(void)
{
    palett(0, 0, 0, 0);
    palett(1, 0, 0, 2);
    palett(2, 0, 1, 4);
    palett(3, 6, 6, 7);
    palett(4, 2, 2, 3);
    palett(5, 0, 4, 6);
    palett(6, 7, 6, 0);
    palett(7, 7, 3, 0);
    palett(8, 7, 2, 5);
    palett(9, 2, 7, 2);
    palett(10, 0, 6, 7);
    palett(11, 7, 7, 7);
    palett(12, 7, 0, 0);
    palett(13, 3, 5, 7);
    palett(14, 4, 4, 5);
    palett(15, 5, 0, 7);
}

/* draw one character with cpym2v (LMMC), scale = 1..3 */
static void draw_char5(int x, int y, unsigned char ch, int fg, int bg, int scale)
{
    const unsigned char *glyph;
    int w = 8 * scale;
    int r, c, i, j, k;
    k = 4;
    if (ch < 0x20 || ch > 0x7f) ch = 0x20;
    glyph = (const unsigned char *)font_buf + (ch - 0x20) * 8;
    pix[0] = (unsigned char)w;
    pix[1] = 0;
    pix[2] = (unsigned char)w;
    pix[3] = 0;
    for (r = 0; r < 8; r++) {
        unsigned char bits = glyph[r];
        for (i = 0; i < scale; i++) {
            for (c = 0; c < 8; c++) {
                unsigned char col = (bits & (0x80 >> c)) ? (unsigned char)fg
                                                         : (unsigned char)bg;
                for (j = 0; j < scale; j++)
                    pix[k++] = col;
            }
        }
    }
    cpym2v((char *)pix, 0, x, y, 0, 0);
}

static int text_width(const char *s, int scale)
{
    int n = 0;
    while (s[n]) n++;
    if (n == 0) return 0;
    return n * (8 * scale) + (n - 1) * (scale > 1 ? 2 : 1);
}

static void draw_text5(int x, int y, const char *s, int fg, int bg, int scale)
{
    while (*s) {
        draw_char5(x, y, (unsigned char)*s, fg, bg, scale);
        x += 8 * scale + (scale > 1 ? 2 : 1);
        s++;
    }
}

static void draw_text5_centered(int y, const char *s, int fg, int bg, int scale)
{
    draw_text5((256 - text_width(s, scale)) / 2, y, s, fg, bg, scale);
}

/* fill a rectangle with the background (stripes + stars) */
static void fill_bg_rect(int x, int y, int w, int h)
{
    int i, y2 = y + h - 1;
    boxfil(x, y, x + w - 1, y2, C_BG, 0);
    for (i = 24; i < 212; i += 32) {         /* panel bands */
        int iy1 = y > i ? y : i;
        int iy2 = y2 < i + 15 ? y2 : i + 15;
        if (iy1 <= iy2)
            boxfil(x, iy1, x + w - 1, iy2, C_PANEL, 0);
    }
    for (i = 0; i < NSTARS; i++) {
        if (star_x[i] >= x && star_x[i] < x + w &&
            star_y[i] >= y && star_y[i] < y + h)
            boxfil(star_x[i], star_y[i], star_x[i] + 1, star_y[i] + 1,
                   star_c[i], 0);
    }
}

static void make_stars(void)
{
    int i;
    static const unsigned char sc[3] = { C_STEEL, C_LBLUE, C_WHITE };
    for (i = 0; i < NSTARS; i++) {
        star_x[i] = (unsigned char)(10 + rnd(236));
        star_y[i] = (unsigned char)(28 + rnd(176));
        star_c[i] = sc[rnd(3)];
    }
}

static void draw_background(void)
{
    int i;
    boxfil(0, HUD_H, 255, 211, C_BG, 0);
    for (i = 24; i < 212; i += 32)
        boxfil(WALL_L, i, WALL_R - 1, i + 15, C_PANEL, 0);
    for (i = 0; i < NSTARS; i++)
        boxfil(star_x[i], star_y[i], star_x[i] + 1, star_y[i] + 1,
               star_c[i], 0);
}

static void draw_walls(void)
{
    boxfil(0, HUD_H, 255, WALL_T - 1, C_STEEL, 0);          /* top    */
    boxfil(0, HUD_H, WALL_L - 1, 211, C_STEEL, 0);          /* left   */
    boxfil(WALL_R, HUD_H, 255, 211, C_STEEL, 0);            /* right  */
    line_m(0, HUD_H, 255, HUD_H, C_STEEL_L, 0);
    line_m(0, HUD_H, 0, 211, C_STEEL_L, 0);
    line_m(255, HUD_H, 255, 211, C_STEEL_L, 0);
    line_m(0, WALL_T - 1, 255, WALL_T - 1, C_STEEL_D, 0);
    line_m(WALL_L - 1, HUD_H, WALL_L - 1, 211, C_STEEL_D, 0);
    line_m(WALL_R, HUD_H, WALL_R, 211, C_STEEL_D, 0);
}

/* HUD: labels are drawn once per field; only the digits are
   refreshed when score/lives change (5 cpym2v instead of 22). */
static void draw_hud_static(void)
{
    boxfil(0, 0, 255, HUD_H - 1, C_BLACK, 0);
    draw_text5(8, 4, "SCORE", C_LBLUE, C_BLACK, 1);
    if (demo_mode)
        draw_text5(112, 4, "DEMO", C_PURPLE, C_BLACK, 1);
    else
        draw_text5(100, 4, "BREAKER", C_PURPLE, C_BLACK, 1);
    draw_text5(192, 4, "LIFE", C_PINK, C_BLACK, 1);
}

/* HUD values: only digits that actually changed are redrawn, and
   updates are deferred by one frame (hud_dirty) so a block-break
   frame never pays for both the block erase and the digit redraws
   -- that combination was the visible "stop" when the ball hit a
   block. The score display lags the hit by a single frame. */
static int hud_score_shown = -1;
static int hud_lives_shown = -1;
static int hud_dirty;
/* Block erase is deferred to the next frame as well: the frame
   that registers the hit should carry no VDP work at all, so the
   ball never dwells on the block it just broke. */
static int erase_pending, erase_r, erase_c;

static void draw_hud_values(void)
{
    static const int div10[4] = { 1000, 100, 10, 1 };
    int i, d;
    if (score != hud_score_shown) {
        for (i = 0; i < 4; i++) {
            d = (score / div10[i]) % 10;
            if (hud_score_shown < 0 || (hud_score_shown / div10[i]) % 10 != d)
                draw_char5(56 + i * 8, 4, (unsigned char)('0' + d),
                           C_WHITE, C_BLACK, 1);
        }
        hud_score_shown = score;
    }
    if (lives != hud_lives_shown) {
        draw_char5(232, 4, (unsigned char)('0' + lives),
                   C_WHITE, C_BLACK, 1);
        hud_lives_shown = lives;
    }
    hud_dirty = 0;
}

/* Precomputed block origins (multiplication is done once at init,
   never in the per-frame collision path). */
static int blk_x0[BLK_COLS];
static int blk_y0[BLK_ROWS];

static void init_block_tables(void)
{
    int i;
    for (i = 0; i < BLK_COLS; i++) blk_x0[i] = BLK_X0 + i * BLK_PX;
    for (i = 0; i < BLK_ROWS; i++) blk_y0[i] = BLK_Y0 + i * BLK_PY;
}

static void draw_block(int r, int c)
{
    int x = blk_x0[c];
    int y = blk_y0[r];
    boxfil(x, y, x + BLK_W - 1, y + BLK_H - 1, blk_main[r], 0);
    line_m(x, y, x + BLK_W - 1, y, C_WHITE, 0);                       /* gloss */
    line_m(x, y + BLK_H - 1, x + BLK_W - 1, y + BLK_H - 1, C_BLACK, 0);
}

static void erase_block(int r, int c)
{
    fill_bg_rect(blk_x0[c], blk_y0[r], BLK_W, BLK_H);
}

static void reset_field(void)
{
    int r, c;
    draw_background();
    draw_walls();
    draw_hud_static();
    hud_score_shown = -1;
    hud_lives_shown = -1;
    draw_hud_values();
    remaining = 0;
    for (r = 0; r < BLK_ROWS; r++)
        for (c = 0; c < BLK_COLS; c++) {
            blocks[r][c] = 1;
            remaining++;
            draw_block(r, c);
        }
}

static void setup_sprites(void)
{
    inispr(2);                        /* 16x16, no magnification */
    sprite(0, (char *)pat_paddle_l);
    sprite(4, (char *)pat_paddle_r);
    sprite(8, (char *)pat_ball);
    colspr(0, (char *)col_paddle);
    colspr(1, (char *)col_paddle);
    colspr(2, (char *)col_ball);
}

/* Sprite attributes for planes 0..2 are contiguous in VRAM, so
   they are built in RAM and moved with a single ldirvm per frame
   (one VDP setup instead of three putspr calls). */
#define ATRBAS  (*(unsigned int *)0xf928)  /* sprite attribute base */
static unsigned char spr_attr[12];

static void flush_sprites(void)
{
    ldirvm(ATRBAS, (char *)spr_attr, 12);
}

static void hide_sprites(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        spr_attr[i * 4 + 0] = 216;
        spr_attr[i * 4 + 1] = 0;
        spr_attr[i * 4 + 2] = 0;
        spr_attr[i * 4 + 3] = 0;
    }
    flush_sprites();
}

static void draw_sprites(void)
{
    spr_attr[0] = (unsigned char)PADDLE_Y;
    spr_attr[1] = (unsigned char)paddle_x;
    spr_attr[2] = 0;
    spr_attr[3] = 0;
    spr_attr[4] = (unsigned char)PADDLE_Y;
    spr_attr[5] = (unsigned char)(paddle_x + 16);
    spr_attr[6] = 4;
    spr_attr[7] = 0;
    spr_attr[8] = (unsigned char)ball_y;
    spr_attr[9] = (unsigned char)ball_x;
    spr_attr[10] = 8;
    spr_attr[11] = 0;
    flush_sprites();
}

static void reset_ball_on_paddle(void)
{
    ball_on_paddle = 1;
    ball_dx = 1;
    ball_dy = -1;
    ball_x = paddle_x + 11;
    ball_y = PADDLE_Y - 13;
}

/* ============================================================
 * Screens / game flow
 * ============================================================ */
static int title_screen(void)
{
    static const unsigned char title_cols[11] = {
        C_YELLOW, C_ORANGE, C_PINK, C_GREEN,
        C_CYAN, C_LBLUE, C_PURPLE, C_YELLOW, C_ORANGE, C_PINK, C_GREEN
    };
    int i, blink = 0, frame = 0;
    draw_background();
    draw_walls();
    boxfil(0, 0, 255, HUD_H - 1, C_BLACK, 0);
    /* sample block row as decoration */
    for (i = 0; i < BLK_COLS; i++) {
        blocks[0][i] = 1;
        draw_block(0, i);
    }
    /* big two-line title "STAR / BREAKER", one color per letter
       (the full name is too wide for one line at scale 3) */
    {
        static const char *lines[2] = { "STAR", "BREAKER" };
        int li, ci = 0;
        for (li = 0; li < 2; li++) {
            const char *p = lines[li];
            int x = (256 - text_width(p, 3)) / 2;
            while (*p) {
                draw_char5(x, 54 + li * 30, (unsigned char)*p,
                           title_cols[ci], C_BG, 3);
                x += 8 * 3 + 2;
                p++;
                ci++;
            }
        }
    }
    draw_text5_centered(152, "CURSOR KEYS OR JOYSTICK", C_LBLUE, C_BG, 1);
    draw_text5_centered(166, "ESC : EXIT TO DOS", C_LBLUE, C_BG, 1);
    /* show paddle + ball as decoration */
    paddle_x = 112;
    reset_ball_on_paddle();
    draw_sprites();
    kilbuf();
    /* wait for release, then a fresh press; blink the prompt */
    for (;;) {
        wait_frame();
        merge_joystick();
        if (lat_esc) return ST_EXIT;
        if (!lat_trig) break;
    }
    {
        int idle = 0;
    for (;;) {
        wait_frame();
        merge_joystick();
        frame++;
        if (++idle >= 300) return ST_DEMO;  /* ~5 s idle: attract */
        if ((frame & 31) == 0) {
            blink = !blink;
            if (blink)
                draw_text5_centered(136, "PRESS SPACE TO START",
                                    C_BG, C_BG, 1);
            else
                draw_text5_centered(136, "PRESS SPACE TO START",
                                    C_WHITE, C_BG, 1);
        }
        if (lat_esc) return ST_EXIT;
        if (lat_trig) return ST_PLAY;
    }
    }
}

static void message_screen(const char *msg, int col)
{
    boxfil(43, 86, 212, 126, C_BLACK, 0);
    line_m(43, 86, 212, 86, C_LBLUE, 0);
    line_m(43, 126, 212, 126, C_LBLUE, 0);
    line_m(43, 86, 43, 126, C_LBLUE, 0);
    line_m(212, 86, 212, 126, C_LBLUE, 0);
    draw_text5_centered(94, msg, col, C_BLACK, 2);
    draw_text5_centered(116, "SPACE:RETRY  ESC:DOS", C_WHITE, C_BLACK, 1);
    draw_sprites();
}

static int play_game(void)
{
    score = 0;
    lives = 3;
    paddle_x = 112;
    reset_field();
    reset_ball_on_paddle();

    for (;;) {
        int prev_bx, prev_by;
        wait_frame();
#ifdef BENCH
        {
            unsigned int tick = *(volatile unsigned int *)0xfc9e;
            if (bench_tick_started &&
                (unsigned int)(tick - bench_prev_tick) >= 2)
                bench_slips++;
            if (bench_after_hit) {
                if (bench_tick_started &&
                    (unsigned int)(tick - bench_prev_tick) >= 2)
                    bench_hit_slips++;
                bench_after_hit = 0;
            }
            bench_prev_tick = tick;
            bench_tick_started = 1;
        }
        bench_frames++;
        if (bench_frames >= BENCH_FRAMES) return ST_EXIT;
#endif
        if (erase_pending) {                /* deferred block erase */
            erase_block(erase_r, erase_c);
            erase_pending = 0;
        }
        if (hud_dirty) draw_hud_values();   /* deferred from last frame */
        merge_joystick();
        if (demo_mode) {
#ifndef BENCH
            if (lat_trig || lat_esc || lat_left || lat_right)
                return ST_TITLE;            /* any key ends the demo */
#endif
            if (ball_on_paddle) lat_trig = 1;   /* AI: launch at once */
            if (ball_x + BALL_OX + 4 < paddle_x + 14)
                paddle_x -= PADDLE_SPEED;
            if (ball_x + BALL_OX + 4 > paddle_x + 18)
                paddle_x += PADDLE_SPEED;
            if (paddle_x < PADDLE_MIN_X) paddle_x = PADDLE_MIN_X;
            if (paddle_x > PADDLE_MAX_X) paddle_x = PADDLE_MAX_X;
        } else if (lat_esc) {
            return ST_EXIT;
        }

        if (lat_left)  paddle_x -= PADDLE_SPEED;
        if (lat_right) paddle_x += PADDLE_SPEED;
        if (paddle_x < PADDLE_MIN_X) paddle_x = PADDLE_MIN_X;
        if (paddle_x > PADDLE_MAX_X) paddle_x = PADDLE_MAX_X;

        if (ball_on_paddle) {
            ball_x = paddle_x + 11;
            ball_y = PADDLE_Y - 13;
            if (lat_trig) {
                ball_on_paddle = 0;
                SE_LAUNCH;
            }
        } else {
            int r, c, hit;
            prev_bx = ball_x;
            prev_by = ball_y;
            ball_x += ball_dx * BALL_SPEED;
            ball_y += ball_dy * BALL_SPEED;

            /* walls (logical box = bx+BALL_OX .. +9, by+BALL_OY .. +9) */
            if (ball_x + BALL_OX <= WALL_L) {
                ball_x = WALL_L - BALL_OX;
                ball_dx = 1;
                SE_WALL;
            }
            if (ball_x + BALL_OX + 9 >= WALL_R) {
                ball_x = WALL_R - BALL_OX - 9;
                ball_dx = -1;
                SE_WALL;
            }
            if (ball_y + BALL_OY <= WALL_T) {
                ball_y = WALL_T - BALL_OY;
                ball_dy = 1;
                SE_WALL;
            }

            /* paddle */
            if (ball_dy > 0 &&
                ball_y + BALL_OY + 9 >= PADDLE_TOP &&
                ball_y + BALL_OY + 9 <= PADDLE_TOP + 9 &&
                ball_x + BALL_OX + 9 > paddle_x &&
                ball_x + BALL_OX < paddle_x + PADDLE_W) {
                ball_y = PADDLE_TOP - BALL_OY - 10;
                ball_dy = -1;
                if (ball_x + BALL_OX + 4 < paddle_x + 10) ball_dx = -1;
                if (ball_x + BALL_OX + 4 > paddle_x + 22) ball_dx = 1;
                SE_PADDLE;
            }

            /* blocks: only when the ball is inside the block band.
               The ball (10 px) can overlap at most 2 columns x 2
               rows, found from the precomputed origin tables --
               no full 65-block scan, no multiplication here. */
            hit = 0;
            {
                int bl = ball_x + BALL_OX;
                int bt = ball_y + BALL_OY;
                if (bt + 9 >= BLK_Y0 && bt < blk_y0[BLK_ROWS - 1] + BLK_H) {
                    int c1 = -1, r1 = -1, i;
                    for (i = 0; i < BLK_COLS; i++) {
                        if (bl + 9 >= blk_x0[i] && bl < blk_x0[i] + BLK_W) {
                            if (c1 < 0) c1 = i;
                            else break;
                        }
                    }
                    for (i = 0; i < BLK_ROWS; i++) {
                        if (bt + 9 >= blk_y0[i] && bt < blk_y0[i] + BLK_H) {
                            if (r1 < 0) r1 = i;
                            else break;
                        }
                    }
                    /* candidate columns c1..c1+1, rows r1..r1+1 */
                    {
                        int cc, rr;
                        for (rr = r1; rr <= r1 + 1 && !hit; rr++) {
                            if (rr < 0 || rr >= BLK_ROWS) continue;
                            if (bt + 9 < blk_y0[rr] || bt >= blk_y0[rr] + BLK_H)
                                continue;
                            for (cc = c1; cc <= c1 + 1 && !hit; cc++) {
                                if (cc < 0 || cc >= BLK_COLS) continue;
                                if (bl + 9 < blk_x0[cc] || bl >= blk_x0[cc] + BLK_W)
                                    continue;
                                if (!blocks[rr][cc]) continue;
                                blocks[rr][cc] = 0;
                                remaining--;
                                score += 10;
#ifdef BENCH
                                bench_hits++;
                                bench_after_hit = 1;
#endif
                                erase_r = rr;    /* erase next frame */
                                erase_c = cc;
                                erase_pending = 1;
                                hud_dirty = 1;   /* digits next frame */
                                if (prev_by + BALL_OY + 9 <= blk_y0[rr] ||
                                    prev_by + BALL_OY >= blk_y0[rr] + BLK_H)
                                    ball_dy = -ball_dy;
                                else
                                    ball_dx = -ball_dx;
                                SE_BLOCK;
                                hit = 1;
                                if (remaining <= 0) {
                                    erase_block(erase_r, erase_c);
                                    erase_pending = 0;
                                    draw_hud_values();  /* final score */
                                    draw_sprites();
                                    return ST_CLEAR;
                                }
                            }
                        }
                    }
                }
            }

            /* miss */
            if (ball_y + BALL_OY > 211) {
                lives--;
                hud_dirty = 1;   /* digits next frame */
                SE_MISS;
                if (lives <= 0) {
                    draw_hud_values();  /* final life count */
                    draw_sprites();
                    return ST_OVER;
                }
                reset_ball_on_paddle();
            }
        }
        draw_sprites();
    }
}



int main(void)
{
    int state = ST_TITLE;
    ginit();
    srnd();
    color(15, 1, 1);
    screen(5);
    setup_palette();
    make_stars();
    setup_sprites();
    init_block_tables();
    audio_init();
    last_jiffy = JIFFY;

#ifdef BENCH
    {
        unsigned int start = *(volatile unsigned int *)0xfc9e;
        unsigned int ticks;
        long fps100;
        bench_frames = 0;
        bench_slips = 0;
        bench_hits = 0;
        bench_hit_slips = 0;
        bench_after_hit = 0;
        bench_tick_started = 0;
        demo_mode = 1;              /* bench drives the AI paddle */
        while (bench_frames < BENCH_FRAMES)
            play_game();
        ticks = (unsigned int)((*(volatile unsigned int *)0xfc9e - start)
                                & 0xffff);
        audio_stop();
        hide_sprites();
        color(15, 1, 1);
        screen(0);
        fps100 = ticks ? (long)BENCH_FRAMES * 6000L / (long)ticks : 0;
        printf("BENCH frames=%d jiffy=%u fps=%ld.%02ld slips=%ld hits=%ld hit_slips=%ld\n",
               BENCH_FRAMES, ticks, fps100 / 100, fps100 % 100,
               bench_slips, bench_hits, bench_hit_slips);
        return 0;
    }
#endif

    while (state != ST_EXIT) {
        if (state == ST_TITLE) {
            state = title_screen();
        } else if (state == ST_DEMO) {
            demo_mode = 1;
            play_game();            /* any key / game end -> title */
            demo_mode = 0;
            state = ST_TITLE;
        } else if (state == ST_PLAY) {
            state = play_game();
            if (state == ST_OVER) {
                SE_OVER;
                message_screen("GAME OVER", C_RED);
                state = wait_trigger_or_esc() ? ST_PLAY : ST_EXIT;
            } else if (state == ST_CLEAR) {
                SE_CLEAR;
                message_screen("GAME CLEAR!", C_GREEN);
                state = wait_trigger_or_esc() ? ST_PLAY : ST_EXIT;
            }
        }
    }

    audio_stop();
    hide_sprites();
    color(15, 1, 1);
    screen(0);
    return 0;
}
