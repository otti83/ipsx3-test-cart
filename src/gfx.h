/* iPSX3 Test Cart — CPU rasteriser for a 1280x720 XRGB8 framebuffer.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * There is no shader and no RSX draw here on purpose: rectangles and a bitmap font are enough for
 * what this program has to show, and they need no vertex/fragment microcode.
 */
#ifndef IPSX3_TESTCART_GFX_H
#define IPSX3_TESTCART_GFX_H

#include "ps3.h"

#define FB_W      1280
#define FB_H      720
#define FB_PITCH  (FB_W * 4)
#define FB_BYTES  (FB_PITCH * FB_H)

#define RGB(r, g, b) ((u32)0xFF000000u | ((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

void gfx_set_target(u32 *fb);
void gfx_clear(u32 colour);
void gfx_rect(int x, int y, int w, int h, u32 colour);
void gfx_frame(int x, int y, int w, int h, int t, u32 colour);
/* 5x7 glyphs, scaled by `s`. Returns the x just past the string. */
int  gfx_text(int x, int y, int s, u32 colour, const char *str);
int  gfx_text_width(int s, const char *str);
void gfx_number(int x, int y, int s, u32 colour, long value, int min_digits);

#endif
