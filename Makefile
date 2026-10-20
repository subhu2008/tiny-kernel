# Makefile -- extremely tiny i386 kernel
#
# Usage:
#   make                  build in this directory
#   make O=out            build into ./out (created if it does not exist)
#   make run              build, then boot the kernel in QEMU (with a display)
#   make O=out run        same, but boot the copy built into ./out
#   make run-nographic    boot headless; the kernel prints over serial too
#                         (quit QEMU with Ctrl-A then X)
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
OBJS    := $(O)/boot.o $(O)/kernel.o

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

clean:
ifeq ($(O),.)
	rm -f $(OBJS) $(O)/$(TARGET)
else
	rm -rf $(O)
endif
