/* iPSX3 Test Cart — PS3 PPU link script.  SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Two loadable segments, deliberately far apart:
 *   - the program at 0x00010000 (where a PS3 user executable starts)
 *   - the 1 MiB gcm IO area at 0x00200000, which cellGcmInit requires to be 1 MiB aligned
 * RPCS3 vm::falloc()s each PT_LOAD at 64 KiB page granularity, so two segments inside one 64 KiB
 * page collide ("vm::falloc() failed (addr=0x10060, memsz=0x20)"); keeping them megabytes apart
 * makes that impossible.
 *
 * The extra header of type 0x60000002 is how a PS3 executable declares its HLE imports; RPCS3
 * reads it at Emu/Cell/PPUModule.cpp:2506.
 */
ENTRY(__ps3_entry)

PHDRS {
  main PT_LOAD FLAGS(7);
  gcmio PT_LOAD FLAGS(6);
  prx 0x60000002;
}

SECTIONS {
  . = 0x00010000;

  .text      : { *(.text.crt0) *(.text .text.*) }        :main
  .rodata    : { *(.rodata .rodata.*) }                  :main
  . = ALIGN(8);
  .opd_ps3   : { *(.opd_ps3) }                           :main
  .opd       : { *(.opd) }                               :main
  .prx_param : { *(.prx_param) }                         :main :prx
  .lib.stub  : { *(.lib.stub) }                          :main
  .toc       : { *(.toc .toc.*) *(.got) *(.branch_lt) }  :main
  __ps3_toc_base = ADDR(.toc) + 0x8000;
  .data      : { *(.data.imports) *(.data .data.*) *(.sdata*) } :main
  .bss       : { *(.bss .bss.*) *(COMMON) }              :main

  . = 0x00200000;
  .gcm_io (NOLOAD) : { *(.gcm_io) }                      :gcmio

  /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame*) *(.gnu.hash) *(.hash) *(.dynsym) *(.dynstr) }
}
