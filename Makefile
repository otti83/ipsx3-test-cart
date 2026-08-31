# iPSX3 Test Cart — build a PS3 PPU executable on a machine that has no PS3 toolchain.
# SPDX-License-Identifier: GPL-2.0-or-later
#
# All this needs is a clang with the ppc64 (big-endian PowerPC) target and an ELF linker.
# On this project's development machine both already exist:
#   CC = Homebrew LLVM's clang      (llc --version lists "ppc64 - PowerPC 64")
#   LD = rustup's bundled rust-lld  (LLD, GNU flavor)
# Override either on the command line if yours live elsewhere.
#
# Apple's clang does NOT have the PowerPC target, so `CC ?=` is not enough: make defines CC as
# "cc" by default, which would silently pick the wrong compiler and fail with an unknown triple.

ifeq ($(origin CC),default)
CC := /opt/homebrew/opt/llvm/bin/clang
endif
ifeq ($(origin LD),default)
LD := $(shell ls -d $(HOME)/.rustup/toolchains/*/lib/rustlib/*/bin/rust-lld 2>/dev/null | head -1)
endif

TARGET  := powerpc64-unknown-linux-gnu
# -mabi=elfv2 so a function symbol IS its code address; the PS3-format 8-byte entry descriptor is
# written by hand in crt0.S. Freestanding: there is no libc here.
CFLAGS  := --target=$(TARGET) -mabi=elfv2 -ffreestanding -fno-builtin -nostdlib -O2 \
           -fno-asynchronous-unwind-tables -fno-stack-protector -Wall -Wextra
ASFLAGS := --target=$(TARGET) -mabi=elfv2 -c

BUILD := build
GEN   := gen
# Staged without spaces because make cannot express a target path containing one; the shipped
# folder name is applied by the `package` step.
STAGE := $(BUILD)/stage
DIST  := dist
CART  := iPSX3 Test Cart

OBJS := $(BUILD)/crt0.o $(BUILD)/prx_imports.o $(BUILD)/main.o $(BUILD)/gfx.o

.PHONY: all package iso check clean
all: package iso

$(GEN)/prx_imports.S $(GEN)/imports.h: tools/mkimports.py
	python3 tools/mkimports.py $(GEN)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/crt0.o: src/crt0.S | $(BUILD)
	$(CC) $(ASFLAGS) $< -o $@

$(BUILD)/prx_imports.o: $(GEN)/prx_imports.S | $(BUILD)
	$(CC) $(ASFLAGS) $< -o $@

$(BUILD)/main.o: src/main.c src/ps3.h src/gfx.h $(GEN)/imports.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gfx.o: src/gfx.c src/gfx.h src/ps3.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/testcart.elf: $(OBJS) ps3.ld
	$(LD) -flavor gnu -T ps3.ld -z max-page-size=0x10000 --no-rosegment -o $@ $(OBJS)

# Assert the PS3 loader invariants before packaging, so a regression names the rule it broke
# instead of showing up as a black screen.
check: $(BUILD)/testcart.elf
	@python3 tools/check_elf.py $(BUILD)/testcart.elf

package: check
	@rm -rf "$(DIST)/$(CART)"
	@mkdir -p "$(DIST)/$(CART)/PS3_GAME/USRDIR"
	@cp $(BUILD)/testcart.elf "$(DIST)/$(CART)/PS3_GAME/USRDIR/EBOOT.BIN"
	@python3 tools/mksfo.py "$(DIST)/$(CART)/PS3_GAME/PARAM.SFO"
	@python3 tools/mkicon.py "$(DIST)/$(CART)/PS3_GAME/ICON0.PNG"
	@echo "packaged: $(DIST)/$(CART)"

# A single-file disc image. This is what gets handed to someone who just wants to try it: copy one
# file into the app's Games folder, with no archive to unpack. RPCS3 requires PS3_DISC.SFB at the
# root of a disc image or it refuses the boot (Emu/System.cpp IsValidSfb).
iso: package
	@rm -rf $(BUILD)/isoroot "$(DIST)/$(CART).iso"
	@mkdir -p $(BUILD)/isoroot
	@cp -R "$(DIST)/$(CART)/PS3_GAME" $(BUILD)/isoroot/
	@python3 tools/mksfb.py $(BUILD)/isoroot/PS3_DISC.SFB >/dev/null
	@hdiutil makehybrid -quiet -iso -joliet -default-volume-name IPSX3TESTCART \
		-o "$(DIST)/$(CART).iso" $(BUILD)/isoroot
	@echo "packaged: $(DIST)/$(CART).iso"

clean:
	rm -rf $(BUILD) $(GEN) $(DIST)
