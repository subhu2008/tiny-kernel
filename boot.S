# boot.S
.code32
.global _start

# Define Multiboot Magic Constants
.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    0x00000003  # Align modules + memory info
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.section .multiboot
.align 4
.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM

.section .text
_start:
    # Set up a stable 32-bit stack pointer
    mov $stack_top, %esp

    # Jump straight to your C logic
    call kernel_main

halt_loop:
    cli
    hlt
    jmp halt_loop

.section .bss
.align 16
stack_bottom:
.skip 16384 # Reserve 16 KB for the kernel stack
stack_top:
