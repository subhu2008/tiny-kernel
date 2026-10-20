# Makefile -- extremely tiny i386 kernel
#
# Usage:
#   make                  build in this directory
#   make O=out            build into ./out (created if it does not exist)
#   make run              build, then boot the kernel in QEMU (with a display)
#   make O=out run        same, but boot the copy built into ./out
#   make run-nographic    boot headless; the kernel prints over serial too
#                         (quit QEMU with Ctrl-A then X)
#   make efi              also build the UEFI app: <O>/esp/EFI/BOOT/BOOTX64.EFI
#                         (needs clang + lld-link; embeds the kernel above)
#   make run-uefi         boot the UEFI build in QEMU/OVMF (add QEMUFLAGS=-nographic
#                         for a serial-only boot)
#   make clean            remove build output
#
# The compiler is i386-tcc, matching the i386 target of this kernel.

# NB: plain '=' so this beats make's built-in CC=cc; `make CC=...` still wins.
CC      = i386-tcc

# Output directory: O=out (or OUTDIR=out). Empty/unset means build in-tree.
# `override` is needed because command-line variables (make O=) beat plain
# makefile assignments; the RHS still sees the user's value before normalizing.
override O := $(or $(strip $(O)),$(strip $(OUTDIR)),.)

# Where the kernel is linked to live (multiboot loaders honour the ELF
# addresses, so this is where GRUB/QEMU will place it).
KERN_LOADADDR ?= 0x100000

CFLAGS  ?= -Wall -O2
LDFLAGS ?= -nostdlib -static -Wl,-Ttext=$(KERN_LOADADDR)

QEMU    ?= qemu-system-i386
QEMUFLAGS ?=

TARGET  := kernel.elf
OBJS    := $(O)/boot.o $(O)/kernel.o $(O)/fb.o $(O)/font.o

# tcc has no usable dep generation, so list the shared-header users by hand
$(O)/kernel.o $(O)/fb.o: boot_info.h

.PHONY: all run run-nographic clean

all: $(O)/$(TARGET)

# Link
$(O)/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Assemble the multiboot boot stub
$(O)/%.o: %.S | $(O)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile the kernel proper
$(O)/%.o: %.c | $(O)
	$(CC) $(CFLAGS) -c $< -o $@

# Create the output directory on demand (order-only prerequisite,
# so a touched directory never forces a rebuild)
$(O):
	mkdir -p $@

run: $(O)/$(TARGET)
	$(QEMU) $(QEMUFLAGS) -kernel $(O)/$(TARGET)

# Headless boot: the kernel mirrors its output to COM1, which -nographic
# wires to stdio, so the text shows up right in the terminal.
run-nographic: $(O)/$(TARGET)
	$(QEMU) $(QEMUFLAGS) -nographic -kernel $(O)/$(TARGET)

# ------------------------- UEFI (x86-64) -------------------------------
# A freestanding PE32+ EFI application that embeds kernel.elf, loads it at
# its link address, exits boot services and jumps into its 32-bit entry
# point.  The same kernel.elf still boots the legacy BIOS/multiboot way.

EFI_CC     = clang -target x86_64-unknown-windows -ffreestanding \
             -fno-stack-protector -fshort-wchar -mno-red-zone -O2 -Wall
EFI_LINK   = lld-link
EFI_DIR    = $(O)/uefi
EFI_OBJS   = $(EFI_DIR)/boot.o $(EFI_DIR)/switch32.o $(EFI_DIR)/embed_kernel.o
ESPDIR     = $(O)/esp
EFIAPP     = $(ESPDIR)/EFI/BOOT/BOOTX64.EFI

# firmware for `make run-uefi` (override with OVMF=/path/to/OVMF.fd)
OVMF ?= $(firstword $(wildcard /usr/share/ovmf/OVMF.fd /usr/share/qemu/OVMF.fd /usr/share/OVMF/OVMF.fd))
QEMU64 ?= qemu-system-x86_64

.PHONY: efi run-uefi

efi: $(EFIAPP)

$(EFI_DIR)/boot.o: efi/boot.c boot_info.h | $(O)
	mkdir -p $(EFI_DIR)
	$(EFI_CC) -c $< -o $@

$(EFI_DIR)/switch32.o: efi/switch32.S | $(O)
	mkdir -p $(EFI_DIR)
	$(EFI_CC) -c $< -o $@

# .incbin searches -I paths, hence -I$(O); depends on the kernel it embeds
$(EFI_DIR)/embed_kernel.o: efi/embed_kernel.S $(O)/$(TARGET) | $(O)
	mkdir -p $(EFI_DIR)
	$(EFI_CC) -I$(O) -c $< -o $@

$(EFIAPP): $(EFI_OBJS)
	mkdir -p $(dir $@)
	$(EFI_LINK) /subsystem:efi_application /machine:x64 /entry:efi_main \
	            /out:$@ $(EFI_OBJS)

# Boots the ESP directory straight from the host via QEMU's vvfat driver;
# OVMF finds \EFI\BOOT\BOOTX64.EFI on it like on a real FAT ESP.
run-uefi: $(EFIAPP)
	$(QEMU64) $(QEMUFLAGS) -bios $(OVMF) -drive format=raw,file=fat:rw:$(ESPDIR)

clean:
ifeq ($(O),.)
	rm -f $(OBJS) $(O)/$(TARGET)
	rm -rf $(EFI_DIR) $(ESPDIR)
else
	rm -rf $(O)
endif
