/* iPSX3 Test Cart — an original PS3 homebrew for verifying that iPSX3 runs guest software and
 * responds to input.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 the iPSX3 project.
 *
 * 100% self-authored.  No Sony SDK, library, asset or key is used, and none is required to build
 * it: the toolchain is clang (powerpc64 big-endian) plus an ELF linker.
 *
 * What it does, in the order a person sees it:
 *   1. an input monitor across the top — every pad button lights while held, both sticks show as a
 *      dot inside a box.  This is the part that answers "does it react to me?".
 *   2. a HUD with SCORE / LIVES / a frame counter that keeps climbing, which is the cheapest proof
 *      that emulation is actually running rather than a still image.
 *   3. a small 2D action game: catch the green blocks, avoid the red ones.
 *
 * Rendering is a CPU-written XRGB8 framebuffer in RSX local memory, flipped with cellGcmSetFlip.
 * Nothing is drawn by the RSX, so no vertex/fragment microcode is involved.
 */
#include "ps3.h"
#include "gfx.h"
#include "../gen/imports.h"

/* ---------------------------------------------------------------- RSX bring-up */

/* Display is set up with the raw lv2 sys_rsx calls rather than cellGcmSys.
 *
 * Every sys_rsx entry point starts with `cpu.state += cpu_flag::wait` (Emu/Cell/lv2/sys_rsx.cpp),
 * which is what vm::writer_lock demands of its caller (vm.cpp:1290).  _cellGcmInitBody does not,
 * so calling it from a plain guest thread terminates that thread with "vm::writer_lock is being
 * used without cpu_flag::wait set by the caller" -- measured, not assumed.  The syscalls are also
 * what a firmware-free program should use: they need no PRX at all.
 *
 * Nothing is drawn by the RSX.  The two display buffers live at the start of local memory, the CPU
 * writes pixels straight into them, and package 0x102 flips.  So there is no FIFO to feed, no
 * command buffer, no vertex or fragment microcode.
 */
#define LOCAL_MEM_SIZE 0x0F900000u    /* what cellGcmSys asks lv2 for; well past two buffers */
#define BUF0_OFFSET    0u
#define BUF1_OFFSET    ((u32)FB_BYTES)

static u32 g_rsx_handle;
static u64 g_rsx_local_addr;
static u32 g_rsx_context;
static u64 g_dma_control, g_driver_info, g_reports;

static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    tty_write(s, n);
}

/* Returns 0 on success. */
static int rsx_init(void)
{
    lv2_syscall4(SYS_RSX_DEVICE_OPEN, 0, 0, 0, 0);

    long rc = lv2_syscall7(SYS_RSX_MEMORY_ALLOCATE,
                           (u64)(unsigned long)&g_rsx_handle,
                           (u64)(unsigned long)&g_rsx_local_addr,
                           LOCAL_MEM_SIZE, 8, 0x300000, 16, 8);
    if (rc != 0) { say("iPSX3 Test Cart: sys_rsx_memory_allocate failed\n"); return 1; }

    u64 dev_addr = 0, dev_a2 = 0;
    lv2_syscall4(SYS_RSX_DEVICE_MAP, (u64)(unsigned long)&dev_addr,
                 (u64)(unsigned long)&dev_a2, 8, 0);

    rc = lv2_syscall7(SYS_RSX_CONTEXT_ALLOCATE,
                      (u64)(unsigned long)&g_rsx_context,
                      (u64)(unsigned long)&g_dma_control,
                      (u64)(unsigned long)&g_driver_info,
                      (u64)(unsigned long)&g_reports,
                      0 /* mem_ctx */, 0 /* system_mode */, 0);
    if (rc != 0) { say("iPSX3 Test Cart: sys_rsx_context_allocate failed\n"); return 2; }

    /* Register both display buffers: a4 packs width<<32 | height, a5 packs pitch<<32 | offset. */
    for (u32 id = 0; id < 2; id++) {
        u32 off = id ? BUF1_OFFSET : BUF0_OFFSET;
        lv2_syscall7(SYS_RSX_CONTEXT_ATTRIBUTE, RSX_CONTEXT_ID, RSX_ATTR_DISPLAY_BUFFER, id,
                     ((u64)FB_W << 32) | (u64)FB_H,
                     ((u64)FB_PITCH << 32) | (u64)off, 0, 0);
    }

    /* a4 == 2 selects vsync (sys_rsx.cpp, package 0x101). */
    lv2_syscall7(SYS_RSX_CONTEXT_ATTRIBUTE, RSX_CONTEXT_ID, RSX_ATTR_FLIP_MODE, 0, 2, 0, 0, 0);
    return 0;
}

static void rsx_flip(u32 buffer_id)
{
    /* Package 0x102 with the high bit CLEAR means "flip to the buffer whose offset this is", which
       sys_rsx matches against the registered display buffers. */
    u32 off = buffer_id ? BUF1_OFFSET : BUF0_OFFSET;
    lv2_syscall7(SYS_RSX_CONTEXT_ATTRIBUTE, RSX_CONTEXT_ID, RSX_ATTR_FLIP, 0, off, 0, 0, 0);
}

/* ---------------------------------------------------------------- input */

/* cellPad HLE signatures (Emu/Cell/Modules/cellPad.cpp). */
typedef s32 (*fn_pad_init)(u32 max_connect);
typedef s32 (*fn_pad_clear_buf)(u32 port);
typedef s32 (*fn_pad_get_data)(u32 port, CellPadData *data);

typedef struct {
    u32 digital1, digital2;     /* held this frame */
    u32 pressed1, pressed2;     /* newly pressed this frame */
    int lx, ly, rx, ry;         /* sticks, -128..127, 0 = centred */
    int connected;
} pad_state;

/* cellPadGetData reports the pad ONLY WHEN SOMETHING CHANGED: with nothing pressed it returns
 * CELL_OK with len == CELL_PAD_LEN_NO_CHANGE (0), and leaves the buttons untouched
 * (Emu/Cell/Modules/cellPad.cpp:505,704,715).  Treating len == 0 as "no pad" is therefore wrong,
 * and it is what made the first run of this program show NO PAD DETECTED while the pad was fine.
 * So the last reported sample is cached, and only a non-zero return code means no device. */
static void pad_read(pad_state *p)
{
    static u32 prev1, prev2;
    static u32 cur1, cur2;
    static int cur_lx = 0, cur_ly = 0, cur_rx = 0, cur_ry = 0;

    p->connected = 0;
    if (IMP_RESOLVED(IMP_cellPadGetData)) {
        CellPadData d;
        for (unsigned i = 0; i < sizeof(d) / sizeof(u32); i++) ((u32 *)&d)[i] = 0;
        s32 r = IMP_CALL(IMP_cellPadGetData, fn_pad_get_data)(0, &d);
        if (r == 0) {
            p->connected = 1;
            if (d.len > 0) {
                cur1 = d.button[2];
                cur2 = d.button[3];
                cur_rx = (int)d.button[4] - 128;
                cur_ry = (int)d.button[5] - 128;
                cur_lx = (int)d.button[6] - 128;
                cur_ly = (int)d.button[7] - 128;
            }
        }
    }
    if (!p->connected) { cur1 = cur2 = 0; cur_lx = cur_ly = cur_rx = cur_ry = 0; }

    p->pressed1 = cur1 & ~prev1;
    p->pressed2 = cur2 & ~prev2;
    prev1 = cur1; prev2 = cur2;
    p->digital1 = cur1; p->digital2 = cur2;
    p->lx = cur_lx; p->ly = cur_ly;
    p->rx = cur_rx; p->ry = cur_ry;
}

/* ---------------------------------------------------------------- palette */

#define C_BG        RGB(14, 16, 26)
#define C_PANEL     RGB(26, 30, 46)
#define C_EDGE      RGB(60, 68, 96)
#define C_TEXT      RGB(226, 232, 245)
#define C_DIM       RGB(120, 130, 158)
#define C_ACCENT    RGB(90, 170, 255)
#define C_GOOD      RGB(80, 210, 120)
#define C_BAD       RGB(232, 78, 92)
#define C_PLAYER    RGB(120, 190, 255)
#define C_WARN      RGB(245, 190, 70)

/* ---------------------------------------------------------------- input monitor */

typedef struct { const char *label; int word; u32 mask; int x, y, w, h; } key_box;

static const key_box k_keys[] = {
    /* d-pad cluster */
    {  "U", 1, PAD_D1_UP,        78,  74, 44, 34 },
    {  "L", 1, PAD_D1_LEFT,      30, 112, 44, 34 },
    {  "D", 1, PAD_D1_DOWN,      78, 150, 44, 34 },
    {  "R", 1, PAD_D1_RIGHT,    126, 112, 44, 34 },
    /* face buttons */
    {  "T", 2, PAD_D2_TRIANGLE, 1116, 74, 44, 34 },
    {  "S", 2, PAD_D2_SQUARE,   1068,112, 44, 34 },
    {  "X", 2, PAD_D2_CROSS,    1116,150, 44, 34 },
    {  "O", 2, PAD_D2_CIRCLE,   1164,112, 44, 34 },
    /* shoulders */
    { "L1", 2, PAD_D2_L1,        250,  74, 56, 34 },
    { "L2", 2, PAD_D2_L2,        250, 116, 56, 34 },
    { "R1", 2, PAD_D2_R1,        930,  74, 56, 34 },
    { "R2", 2, PAD_D2_R2,        930, 116, 56, 34 },
    /* centre */
    { "SELECT", 1, PAD_D1_SELECT, 470, 150, 100, 34 },
    { "START",  1, PAD_D1_START,  600, 150,  92, 34 },
    { "L3", 1, PAD_D1_L3,         360, 150, 44, 34 },
    { "R3", 1, PAD_D1_R3,         800, 150, 44, 34 },
};
#define N_KEYS ((int)(sizeof(k_keys) / sizeof(k_keys[0])))

static void draw_stick(int cx, int cy, int r, int dx, int dy, const char *label)
{
    gfx_frame(cx - r, cy - r, r * 2, r * 2, 2, C_EDGE);
    gfx_rect(cx - 1, cy - r + 3, 2, r * 2 - 6, C_PANEL);
    gfx_rect(cx - r + 3, cy - 1, r * 2 - 6, 2, C_PANEL);
    int px = cx + (dx * (r - 8)) / 128;
    int py = cy + (dy * (r - 8)) / 128;
    int live = (dx > 12 || dx < -12 || dy > 12 || dy < -12);
    gfx_rect(px - 6, py - 6, 12, 12, live ? C_ACCENT : C_DIM);
    gfx_text(cx - gfx_text_width(2, label) / 2, cy + r + 6, 2, C_DIM, label);
}

static void draw_input_monitor(const pad_state *p)
{
    gfx_rect(0, 0, FB_W, 232, C_PANEL);
    gfx_rect(0, 230, FB_W, 2, C_EDGE);
    gfx_text(30, 22, 3, C_TEXT, "IPSX3 TEST CART");
    gfx_text(30, 46, 2, C_DIM, "PRESS ANYTHING - THE BOX LIGHTS UP");

    for (int i = 0; i < N_KEYS; i++) {
        const key_box *k = &k_keys[i];
        u32 held = (k->word == 1 ? p->digital1 : p->digital2) & k->mask;
        gfx_rect(k->x, k->y, k->w, k->h, held ? C_ACCENT : C_BG);
        gfx_frame(k->x, k->y, k->w, k->h, 2, held ? C_TEXT : C_EDGE);
        gfx_text(k->x + (k->w - gfx_text_width(2, k->label)) / 2, k->y + (k->h - 14) / 2,
                 2, held ? C_BG : C_DIM, k->label);
    }

    draw_stick(470, 74, 46, p->lx, p->ly, "L STICK");
    draw_stick(800, 74, 46, p->rx, p->ry, "R STICK");

    if (!p->connected) {
        gfx_text(FB_W / 2 - gfx_text_width(2, "NO PAD DETECTED") / 2, 200, 2, C_WARN,
                 "NO PAD DETECTED");
    }
}

/* ---------------------------------------------------------------- game */

#define PLAY_TOP    244
#define HUD_H       56          /* the SCORE/LIVES/FRAME row lives inside the field border */
#define SPAWN_TOP   (PLAY_TOP + HUD_H)
#define PLAY_BOT    (FB_H - 44)   /* leaves a strip under the field for the control hint */
#define PLAY_LEFT   12
#define PLAY_RIGHT  (FB_W - 12)

#define N_BLOCKS 10
typedef struct { int x, y, vy, w, h, bad, live; } block;

typedef struct {
    int px, pw, ph, py;
    long score;
    int lives;
    int over;
    unsigned rng;
    int spawn_timer;
    int speed;
    block blocks[N_BLOCKS];
    int flash;
    int over_timer;   /* frames spent on the GAME OVER screen */
} game_state;

static unsigned rnd(game_state *g)
{
    /* xorshift32 with a fixed seed: the demo replays identically every run, which is what you
       want when someone else has to reproduce what you saw. */
    g->rng ^= g->rng << 13;
    g->rng ^= g->rng >> 17;
    g->rng ^= g->rng << 5;
    return g->rng;
}

static void game_reset(game_state *g)
{
    g->px = FB_W / 2 - 48;
    g->pw = 96; g->ph = 26;
    g->py = PLAY_BOT - 40;
    g->score = 0;
    g->lives = 3;
    g->over = 0;
    g->rng = 0x1BADC0DEu;
    g->spawn_timer = 0;
    g->speed = 3;
    g->flash = 0;
    g->over_timer = 0;
    for (int i = 0; i < N_BLOCKS; i++) g->blocks[i].live = 0;
}

static int overlaps(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void game_step(game_state *g, const pad_state *p)
{
    if (g->flash > 0) g->flash--;

    if (g->over) {
        /* Restart on START, and also on its own after a few seconds. Nobody is holding the pad
           while a reviewer is reading the screen, and a frozen GAME OVER is the one thing that
           looks like the emulator stopped working. */
        if ((p->pressed1 & PAD_D1_START) || ++g->over_timer > 240) game_reset(g);
        return;
    }

    int move = 0;
    if (p->digital1 & PAD_D1_LEFT)  move -= 1;
    if (p->digital1 & PAD_D1_RIGHT) move += 1;
    if (p->lx > 40)  move += 1;
    if (p->lx < -40) move -= 1;

    int step = (p->digital2 & PAD_D2_CROSS) ? 18 : 9;   /* X = dash */
    g->px += move * step;
    if (g->px < PLAY_LEFT) g->px = PLAY_LEFT;
    if (g->px + g->pw > PLAY_RIGHT) g->px = PLAY_RIGHT - g->pw;

    if (--g->spawn_timer <= 0) {
        for (int i = 0; i < N_BLOCKS; i++) {
            if (!g->blocks[i].live) {
                block *b = &g->blocks[i];
                b->w = 34 + (int)(rnd(g) % 26);
                b->h = 34;
                b->x = PLAY_LEFT + (int)(rnd(g) % (unsigned)(PLAY_RIGHT - PLAY_LEFT - b->w));
                b->y = SPAWN_TOP;
                b->bad = (int)(rnd(g) % 3) == 0;
                b->vy = g->speed + (int)(rnd(g) % 3);
                b->live = 1;
                break;
            }
        }
        g->spawn_timer = 26 - (g->speed * 2);
        if (g->spawn_timer < 8) g->spawn_timer = 8;
    }

    for (int i = 0; i < N_BLOCKS; i++) {
        block *b = &g->blocks[i];
        if (!b->live) continue;
        b->y += b->vy;
        if (overlaps(b->x, b->y, b->w, b->h, g->px, g->py, g->pw, g->ph)) {
            b->live = 0;
            if (b->bad) {
                if (--g->lives <= 0) { g->lives = 0; g->over = 1; }
                g->flash = 8;
            } else {
                g->score += 10;
                if (g->score % 100 == 0 && g->speed < 9) g->speed++;
            }
        } else if (b->y + b->h > PLAY_BOT) {
            b->live = 0;
            if (!b->bad && g->score > 0) g->score -= 2;
        }
    }
}

static void game_draw(const game_state *g, unsigned long frame)
{
    gfx_rect(0, 232, FB_W, FB_H - 232, C_BG);
    gfx_frame(PLAY_LEFT - 4, PLAY_TOP - 4,
              PLAY_RIGHT - PLAY_LEFT + 8, PLAY_BOT - PLAY_TOP + 8, 2, C_EDGE);

    if (g->flash) gfx_rect(PLAY_LEFT, PLAY_TOP, PLAY_RIGHT - PLAY_LEFT, PLAY_BOT - PLAY_TOP,
                           RGB(70, 24, 30));

    for (int i = 0; i < N_BLOCKS; i++) {
        const block *b = &g->blocks[i];
        if (!b->live) continue;
        gfx_rect(b->x, b->y, b->w, b->h, b->bad ? C_BAD : C_GOOD);
        gfx_frame(b->x, b->y, b->w, b->h, 2, C_TEXT);
    }

    gfx_rect(g->px, g->py, g->pw, g->ph, C_PLAYER);
    gfx_frame(g->px, g->py, g->pw, g->ph, 3, C_TEXT);

    int x = gfx_text(30, 254, 3, C_DIM, "SCORE ");
    gfx_number(x, 254, 3, C_TEXT, g->score, 1);
    x = gfx_text(330, 254, 3, C_DIM, "LIVES ");
    gfx_number(x, 254, 3, g->lives > 1 ? C_GOOD : C_BAD, g->lives, 1);
    x = gfx_text(620, 254, 3, C_DIM, "FRAME ");
    gfx_number(x, 254, 3, C_ACCENT, (long)frame, 1);

    gfx_text(30, PLAY_BOT + 16, 2, C_DIM,
             "D-PAD/STICK MOVE   X DASH   CATCH GREEN   AVOID RED");

    if (g->over) {
        int w = gfx_text_width(6, "GAME OVER");
        gfx_rect(FB_W / 2 - w / 2 - 40, 420, w + 80, 130, C_PANEL);
        gfx_frame(FB_W / 2 - w / 2 - 40, 420, w + 80, 130, 3, C_BAD);
        gfx_text(FB_W / 2 - w / 2, 448, 6, C_TEXT, "GAME OVER");
        const char *hint = "PRESS START";
        gfx_text(FB_W / 2 - gfx_text_width(3, hint) / 2, 508, 3, C_DIM, hint);
    }
}

/* ---------------------------------------------------------------- main */

void ps3_main(void)
{
    say("iPSX3 Test Cart: starting\n");

    if (rsx_init() != 0) {
        say("iPSX3 Test Cart: FATAL - RSX bring-up failed\n");
        for (;;) usleep_us(1000000);
    }
    say("iPSX3 Test Cart: RSX ready\n");

    if (IMP_RESOLVED(IMP_cellPadInit)) IMP_CALL(IMP_cellPadInit, fn_pad_init)(7);
    if (IMP_RESOLVED(IMP_cellPadClearBuf)) IMP_CALL(IMP_cellPadClearBuf, fn_pad_clear_buf)(0);

    u32 *const buf[2] = { (u32 *)(unsigned long)(RSX_LOCAL_BASE + BUF0_OFFSET),
                          (u32 *)(unsigned long)(RSX_LOCAL_BASE + BUF1_OFFSET) };
    /* Start from a known state so a missed first flip cannot show uninitialised VRAM. */
    for (int i = 0; i < 2; i++) { gfx_set_target(buf[i]); gfx_clear(C_BG); }

    game_state game;
    game_reset(&game);
    pad_state pad;
    unsigned long frame = 0;
    int back = 0;

    say("iPSX3 Test Cart: entering main loop\n");

    for (;;) {
        pad_read(&pad);
        game_step(&game, &pad);

        gfx_set_target(buf[back]);
        gfx_clear(C_BG);
        draw_input_monitor(&pad);
        game_draw(&game, frame);

        /* Make every pixel visible to the RSX before asking for the flip. */
        __asm__ volatile ("sync" ::: "memory");

        rsx_flip((u32)back);
        back ^= 1;
        frame++;

        /* Rough 60 Hz pacing.  sys_rsx's flip is asynchronous and there is no SDK wait helper
           here; a fixed sleep is honest about that rather than pretending to be vsync-locked. */
        usleep_us(16000);
    }
}
