/* iPSX3 Test Cart — the small slice of the PS3 environment this program uses.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Written from scratch against the emulator's own sources; no Sony SDK header, library, asset or
 * key is involved. Everything below is either an lv2 syscall number (Emu/Cell/lv2/lv2.cpp) or an
 * HLE import resolved through the tables in gen/prx_imports.S.
 */
#ifndef IPSX3_TESTCART_PS3_H
#define IPSX3_TESTCART_PS3_H

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef signed int          s32;

/* lv2 syscall numbers, from Emu/Cell/lv2/lv2.cpp's binding table. */
#define SYS_TIMER_USLEEP          141
#define SYS_TTY_WRITE             403
#define SYS_RSX_DEVICE_OPEN       666
#define SYS_RSX_MEMORY_ALLOCATE   668
#define SYS_RSX_CONTEXT_ALLOCATE  670
#define SYS_RSX_CONTEXT_ATTRIBUTE 674
#define SYS_RSX_DEVICE_MAP        675

/* sys_rsx_context_attribute package ids (Emu/Cell/lv2/sys_rsx.cpp). */
#define RSX_ATTR_FLIP_MODE      0x101
#define RSX_ATTR_FLIP           0x102
#define RSX_ATTR_DISPLAY_BUFFER 0x104
#define RSX_CONTEXT_ID          0x55555555u

static inline long lv2_syscall4(u64 n, u64 a, u64 b, u64 c, u64 d)
{
    register u64 r11 __asm__("r11") = n;
    register u64 r3  __asm__("r3")  = a;
    register u64 r4  __asm__("r4")  = b;
    register u64 r5  __asm__("r5")  = c;
    register u64 r6  __asm__("r6")  = d;
    __asm__ volatile ("sc"
                      : "+r"(r3), "+r"(r4), "+r"(r5), "+r"(r6)
                      : "r"(r11)
                      : "r0", "r7", "r8", "r9", "r10", "r12", "cr0", "memory");
    return (long)r3;
}

/* Seven arguments is what the sys_rsx calls take. */
static inline long lv2_syscall7(u64 n, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f, u64 g)
{
    register u64 r11 __asm__("r11") = n;
    register u64 r3  __asm__("r3")  = a;
    register u64 r4  __asm__("r4")  = b;
    register u64 r5  __asm__("r5")  = c;
    register u64 r6  __asm__("r6")  = d;
    register u64 r7  __asm__("r7")  = e;
    register u64 r8  __asm__("r8")  = f;
    register u64 r9  __asm__("r9")  = g;
    __asm__ volatile ("sc"
                      : "+r"(r3), "+r"(r4), "+r"(r5), "+r"(r6), "+r"(r7), "+r"(r8), "+r"(r9)
                      : "r"(r11)
                      : "r0", "r10", "r12", "cr0", "memory");
    return (long)r3;
}

static inline void tty_write(const char *s, u32 len)
{
    u32 written = 0;
    lv2_syscall4(SYS_TTY_WRITE, 0, (u64)(unsigned long)s, len, (u64)(unsigned long)&written);
}

static inline void usleep_us(u64 us) { lv2_syscall4(SYS_TIMER_USLEEP, us, 0, 0, 0); }

/* RSX local memory is mapped into the guest address space at a fixed base; a display-buffer offset
   is relative to it (Emu/RSX/rsx_utils.h: rsx::constants::local_mem_base). */
#define RSX_LOCAL_BASE  0xC0000000u

/* cellPad digital button bits (Emu/Cell/Modules/cellPad.h). button[2] is digital-1, [3] digital-2. */
#define PAD_D1_SELECT   0x01
#define PAD_D1_L3       0x02
#define PAD_D1_R3       0x04
#define PAD_D1_START    0x08
#define PAD_D1_UP       0x10
#define PAD_D1_RIGHT    0x20
#define PAD_D1_DOWN     0x40
#define PAD_D1_LEFT     0x80

#define PAD_D2_L2       0x01
#define PAD_D2_R2       0x02
#define PAD_D2_L1       0x04
#define PAD_D2_R1       0x08
#define PAD_D2_TRIANGLE 0x10
#define PAD_D2_CIRCLE   0x20
#define PAD_D2_CROSS    0x40
#define PAD_D2_SQUARE   0x80

/* CellPadData: a length followed by the button words. Only the first handful are read here. */
typedef struct { s32 len; u16 button[64]; } CellPadData;

#endif
