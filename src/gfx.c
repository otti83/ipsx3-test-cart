/* iPSX3 Test Cart — CPU rasteriser.  SPDX-License-Identifier: GPL-2.0-or-later */
#include "gfx.h"

static u32 *g_fb;

void gfx_set_target(u32 *fb) { g_fb = fb; }

void gfx_clear(u32 colour)
{
    u32 *p = g_fb;
    for (int i = 0; i < FB_W * FB_H; i++) p[i] = colour;
}

void gfx_rect(int x, int y, int w, int h, u32 colour)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *row = g_fb + (long)(y + j) * FB_W + x;
        for (int i = 0; i < w; i++) row[i] = colour;
    }
}

void gfx_frame(int x, int y, int w, int h, int t, u32 colour)
{
    gfx_rect(x, y, w, t, colour);
    gfx_rect(x, y + h - t, w, t, colour);
    gfx_rect(x, y, t, h, colour);
    gfx_rect(x + w - t, y, t, h, colour);
}

/* A 5x7 font, one byte per column-of-5-bits per row.  Only the glyphs this program prints exist;
   anything else renders as a blank.  Index = ASCII - 32. */
#define GLYPH_FIRST 32
#define GLYPH_LAST  95
static const u8 k_font[GLYPH_LAST - GLYPH_FIRST + 1][7] = {
    {0,0,0,0,0,0,0},                            /* space */
    {4,4,4,4,4,0,4},                            /* ! */
    {10,10,0,0,0,0,0},                          /* " */
    {10,31,10,31,10,0,0},                       /* # */
    {4,15,20,14,5,30,4},                        /* $ */
    {25,26,2,4,8,11,19},                        /* % */
    {12,18,20,8,21,18,13},                      /* & */
    {4,4,0,0,0,0,0},                            /* ' */
    {2,4,8,8,8,4,2},                            /* ( */
    {8,4,2,2,2,4,8},                            /* ) */
    {0,10,4,31,4,10,0},                         /* * */
    {0,4,4,31,4,4,0},                           /* + */
    {0,0,0,0,0,4,8},                            /* , */
    {0,0,0,31,0,0,0},                           /* - */
    {0,0,0,0,0,0,4},                            /* . */
    {1,2,2,4,8,8,16},                           /* / */
    {14,17,19,21,25,17,14},                     /* 0 */
    {4,12,4,4,4,4,14},                          /* 1 */
    {14,17,1,2,4,8,31},                         /* 2 */
    {31,2,4,2,1,17,14},                         /* 3 */
    {2,6,10,18,31,2,2},                         /* 4 */
    {31,16,30,1,1,17,14},                       /* 5 */
    {6,8,16,30,17,17,14},                       /* 6 */
    {31,1,2,4,8,8,8},                           /* 7 */
    {14,17,17,14,17,17,14},                     /* 8 */
    {14,17,17,15,1,2,12},                       /* 9 */
    {0,4,0,0,0,4,0},                            /* : */
    {0,4,0,0,0,4,8},                            /* ; */
    {2,4,8,16,8,4,2},                           /* < */
    {0,0,31,0,31,0,0},                          /* = */
    {8,4,2,1,2,4,8},                            /* > */
    {14,17,1,2,4,0,4},                          /* ? */
    {14,17,1,13,21,21,14},                      /* @ */
    {14,17,17,31,17,17,17},                     /* A */
    {30,17,17,30,17,17,30},                     /* B */
    {14,17,16,16,16,17,14},                     /* C */
    {28,18,17,17,17,18,28},                     /* D */
    {31,16,16,30,16,16,31},                     /* E */
    {31,16,16,30,16,16,16},                     /* F */
    {14,17,16,23,17,17,15},                     /* G */
    {17,17,17,31,17,17,17},                     /* H */
    {14,4,4,4,4,4,14},                          /* I */
    {7,2,2,2,2,18,12},                          /* J */
    {17,18,20,24,20,18,17},                     /* K */
    {16,16,16,16,16,16,31},                     /* L */
    {17,27,21,21,17,17,17},                     /* M */
    {17,17,25,21,19,17,17},                     /* N */
    {14,17,17,17,17,17,14},                     /* O */
    {30,17,17,30,16,16,16},                     /* P */
    {14,17,17,17,21,18,13},                     /* Q */
    {30,17,17,30,20,18,17},                     /* R */
    {15,16,16,14,1,1,30},                       /* S */
    {31,4,4,4,4,4,4},                           /* T */
    {17,17,17,17,17,17,14},                     /* U */
    {17,17,17,17,17,10,4},                      /* V */
    {17,17,17,21,21,21,10},                     /* W */
    {17,17,10,4,10,17,17},                      /* X */
    {17,17,10,4,4,4,4},                         /* Y */
    {31,1,2,4,8,16,31},                         /* Z */
    {14,8,8,8,8,8,14},                          /* [ */
    {16,8,8,4,2,2,1},                           /* \ */
    {14,2,2,2,2,2,14},                          /* ] */
    {4,10,17,0,0,0,0},                          /* ^ */
    {0,0,0,0,0,0,31},                           /* _ */
};

static void draw_glyph(int x, int y, int s, u32 colour, char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < GLYPH_FIRST || c > GLYPH_LAST) return;
    const u8 *g = k_font[c - GLYPH_FIRST];
    for (int r = 0; r < 7; r++)
        for (int col = 0; col < 5; col++)
            if (g[r] & (1 << (4 - col)))
                gfx_rect(x + col * s, y + r * s, s, s, colour);
}

int gfx_text(int x, int y, int s, u32 colour, const char *str)
{
    for (const char *p = str; *p; p++) {
        draw_glyph(x, y, s, colour, *p);
        x += 6 * s;
    }
    return x;
}

int gfx_text_width(int s, const char *str)
{
    int n = 0;
    for (const char *p = str; *p; p++) n++;
    return n * 6 * s;
}

void gfx_number(int x, int y, int s, u32 colour, long value, int min_digits)
{
    char buf[24];
    int n = 0;
    int neg = value < 0;
    unsigned long v = (unsigned long)(neg ? -value : value);
    do { buf[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < 20);
    while (n < min_digits && n < 20) buf[n++] = '0';
    if (neg && n < 20) buf[n++] = '-';
    for (int i = n - 1; i >= 0; i--) { draw_glyph(x, y, s, colour, buf[i]); x += 6 * s; }
}
