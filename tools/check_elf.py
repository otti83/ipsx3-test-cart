#!/usr/bin/env python3
"""Assert the PS3-specific invariants of the built ELF.

SPDX-License-Identifier: GPL-2.0-or-later

Each check here corresponds to something that actually failed while bringing this up, so a
regression says which rule it broke rather than "black screen".
"""
import struct, sys

def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def be16(b, o): return struct.unpack_from(">H", b, o)[0]
def be64(b, o): return struct.unpack_from(">Q", b, o)[0]

def main(path):
    b = open(path, "rb").read()
    errs = []

    if b[:4] != b"\x7fELF":              errs.append("not an ELF")
    if b[4] != 2:                        errs.append("not ELF64 (RPCS3 ppu_exec_object is elf64)")
    if b[5] != 2:                        errs.append("not big-endian (RPCS3 checks e_data == 2)")
    if be16(b, 16) != 2:                 errs.append("e_type is not ET_EXEC (elf_type::exec)")
    if be16(b, 18) != 0x15:              errs.append("e_machine is not EM_PPC64")

    e_entry  = be64(b, 24)
    e_phoff  = be64(b, 32)
    e_phentsize = be16(b, 54)
    e_phnum  = be16(b, 56)

    loads, prx = [], None
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        p_type = be32(b, o)
        p_off, p_vaddr = be64(b, o + 8), be64(b, o + 16)
        p_filesz, p_memsz = be64(b, o + 32), be64(b, o + 40)
        if p_type == 1: loads.append((p_vaddr, p_memsz, p_off, p_filesz))
        if p_type == 0x60000002: prx = (p_vaddr, p_off, p_filesz)

    if not loads: errs.append("no PT_LOAD segment")

    # RPCS3 vm::falloc()s each segment at 64 KiB granularity, so two segments must not share a page.
    pages = {}
    for vaddr, memsz, _, _ in loads:
        for pg in range(vaddr >> 16, (vaddr + max(memsz, 1) - 1) >> 16 or (vaddr >> 16) + 1):
            pass
        first, last = vaddr >> 16, (vaddr + max(memsz, 1) - 1) >> 16
        for pg in range(first, last + 1):
            if pg in pages:
                errs.append("two PT_LOAD segments share the 64 KiB page 0x%x "
                            "(vm::falloc would fail)" % (pg << 16))
            pages[pg] = vaddr

    # e_entry must point at an 8-byte {code, rtoc} descriptor inside a loaded segment, and the code
    # word must itself land in a loaded segment. RPCS3 does vm::_ref<ppu_func_opd_t>(e_entry).
    def file_off(va, size):
        for vaddr, memsz, off, filesz in loads:
            if vaddr <= va and va + size <= vaddr + filesz:
                return off + (va - vaddr)
        return None

    o = file_off(e_entry, 8)
    if o is None:
        errs.append("e_entry 0x%x is not inside the file image of any PT_LOAD" % e_entry)
    else:
        code, rtoc = be32(b, o), be32(b, o + 4)
        if code == 0:
            errs.append("entry descriptor code word is 0")
        if code >> 24 == 0 and code < 0x10000:
            errs.append("entry descriptor code word 0x%x looks like the high half of a 64-bit "
                        "address -- a 24-byte ELFv1 OPD was used where PS3 wants 8 bytes" % code)
        if file_off(code, 4) is None:
            errs.append("entry code 0x%x is not inside any PT_LOAD" % code)
        if rtoc == 0:
            errs.append("entry descriptor rtoc is 0")
        print("entry descriptor: code=0x%08x rtoc=0x%08x" % (code, rtoc))

    if prx is None:
        errs.append("no PT_PROC_PRX (0x60000002) header -- HLE imports will not be resolved")
    else:
        pv, po, pf = prx
        if pf < 0x28:
            errs.append("PT_PROC_PRX segment is too small (%d bytes)" % pf)
        else:
            magic = be32(b, po + 4)
            if magic != 0x1b434cec:
                errs.append("proc_prx_param magic is 0x%08x, expected 0x1b434cec" % magic)
            stub_start, stub_end = be32(b, po + 24), be32(b, po + 28)
            if stub_end <= stub_start:
                errs.append("libstub range is empty")
            elif (stub_end - stub_start) % 0x2c:
                errs.append("libstub range %d bytes is not a multiple of 44" % (stub_end - stub_start))
            else:
                print("imports: %d module(s), libstub 0x%x..0x%x"
                      % ((stub_end - stub_start) // 0x2c, stub_start, stub_end))

    for vaddr, memsz, _, _ in loads:
        print("PT_LOAD vaddr=0x%08x memsz=0x%x" % (vaddr, memsz))

    if errs:
        for e in errs: print("FAIL:", e)
        sys.exit(1)
    print("OK: %s passes every PS3/RPCS3 loader invariant checked here" % path)

if __name__ == "__main__":
    main(sys.argv[1])
