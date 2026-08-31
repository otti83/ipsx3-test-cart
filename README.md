# iPSX3 Test Cart

A small original PS3 program, written for one purpose: **so that someone who has no PS3 game can
still tell whether iPSX3 is working.**

It draws an input monitor, a HUD whose frame counter keeps climbing, and a two-button 2D action
game. Press anything on the on-screen pad and the matching box lights up immediately; move the
paddle with the D-pad or the left stick, catch the green blocks, avoid the red ones, press START
after GAME OVER.

## What it is not

- It contains **no Sony code, assets, keys or SDK headers**, and none were used to build it.
- It is **not** a commercial game, and it needs no game data.
- It **does not need the PS3 system software installed.** It imports nothing from `dev_flash` — its
  graphics go through the `sys_rsx_*` lv2 calls and its input through the `cellPad` HLE module — so
  it runs on a device that has never downloaded firmware.

## Licence

GPL-2.0-or-later, the same as iPSX3 (a derivative of RPCS3). Every file here is original work.

## Building

You do **not** need a PS3 toolchain (no PSL1GHT, no ps3toolchain, no cross-GCC). All that is
required is a clang that can target big-endian PowerPC 64 and an ELF linker:

```
make            # -> dist/iPSX3 Test Cart/PS3_GAME/{PARAM.SFO,ICON0.PNG,USRDIR/EBOOT.BIN}
make check      # verify the PS3/RPCS3 loader invariants without packaging
```

The Makefile defaults to Homebrew LLVM's `clang` (its `llc --version` lists `ppc64 - PowerPC 64`)
and to `rust-lld` from a rustup toolchain, because those are what this project's development
machine already has. Override on the command line if yours live elsewhere:

```
make CC=/path/to/clang LD=/path/to/ld.lld
```

Apple's own clang has **no** PowerPC target, which is why the Makefile refuses make's default `CC`.

## Installing it

Copy the whole `iPSX3 Test Cart` folder into the app's `Documents/Games/` (Files app ▸ On My
iPad ▸ iPSX3 ▸ Games). It then appears in the Games tab.

## How it works, and the traps that cost time

Written against the emulator's own sources; each of these was measured, not assumed.

- **Entry point.** RPCS3 reads `e_entry` as an **8-byte** descriptor `{u32 code, u32 rtoc}`
  (`Emu/Cell/PPUThread.h: ppu_func_opd_t`), not the 24-byte ELFv1 OPD a PowerPC toolchain emits.
  `src/crt0.S` writes that descriptor by hand and the C is compiled `-mabi=elfv2`, where a function
  symbol is its own code address.
- **Segments.** Each `PT_LOAD` is `vm::falloc`ed at 64 KiB granularity, so two segments inside one
  64 KiB page collide — the first attempt died with
  `ppu_load_exec(): vm::falloc() failed (addr=0x10060, memsz=0x20)`.
- **Imports.** HLE functions are declared the way a PS3 executable does: a program header of type
  `0x60000002` pointing at a `ppu_proc_prx_param_t` (magic `0x1b434cec`) whose libstub range holds
  44-byte module records. `tools/mkimports.py` generates them and **computes** the NIDs with the
  same algorithm RPCS3 uses (`SHA-1(name ‖ suffix)`, first four bytes little-endian) instead of
  copying a table — checked against two values from a real boot log.
- **Calling an import.** The loader writes a **function descriptor** address into the slot, not a
  code address (the table is built as a "double-purpose fake OPD" whose words are
  `{code = self + 4, rtoc = 0}`). Branching to the slot value lands on the entry registered as a
  null function: `Segfault executing location cccccccccccccccc`. Dereference it first.
- **Graphics via syscalls, not cellGcmSys.** Every `sys_rsx_*` entry point sets `cpu_flag::wait`
  before touching vm, which is what `vm::writer_lock` requires of its caller. `_cellGcmInitBody`
  does not, so calling it from a plain guest thread terminates that thread with
  *"vm::writer_lock is being used without cpu_flag::wait set by the caller"*. The syscall route is
  also the one that needs no firmware.
- **Pad reads report changes only.** `cellPadGetData` returns `CELL_OK` with
  `len == CELL_PAD_LEN_NO_CHANGE` (0) when nothing moved. Treating that as "no pad" is what made
  the first working build show NO PAD DETECTED while the pad was fine.

`tools/check_elf.py` asserts all of the loader-facing invariants above, so a regression names the
rule it broke instead of showing up as a black screen.
