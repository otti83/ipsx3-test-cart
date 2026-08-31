/* iPSX3 Test Cart — PS3 PPU entry stub.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * RPCS3 reads e_entry as an 8-byte descriptor {u32 code, u32 rtoc} (ppu_func_opd_t,
 * Emu/Cell/PPUThread.h), NOT as the 24-byte ELFv1 OPD entry a PowerPC toolchain emits. So the
 * descriptor is written out by hand here and e_entry points at it.
 *
 * The C is compiled -mabi=elfv2, where a function symbol IS its code address. The global entry
 * point derives r2 from r12, so this stub loads r12 with the callee address before branching, and
 * also sets r2 itself so the descriptor's rtoc and the register agree.
 */
    .section .opd_ps3, "a", @progbits
    .p2align 3
    .globl  __ps3_entry
__ps3_entry:
    .long   __ps3_start
    .long   __ps3_toc_base

    .section .text.crt0, "ax", @progbits
    .p2align 2
    .globl  __ps3_start
__ps3_start:
    lis     2,  __ps3_toc_base@ha
    addi    2,  2, __ps3_toc_base@l
    lis     12, ps3_main@ha
    addi    12, 12, ps3_main@l
    mtctr   12
    bctrl
    /* ps3_main never returns; if it ever did, park rather than run off the end. */
1:  b       1b
