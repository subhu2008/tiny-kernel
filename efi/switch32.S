# efi/switch32.S -- long mode -> 32-bit protected mode (compatibility mode)
#
# void efi_switch_to_32(u32 entry);   /* MS x64 ABI: entry arrives in %rcx */
#
# The UEFI stub runs in 64-bit mode with identity-mapped page tables.  We
# install a tiny flat GDT, load 32-bit segment descriptors, then use a far
# return (lretq) to drop into compatibility mode at the kernel's entry
# point.  Paging stays on (OVMF identity-maps all RAM), which is fine:
# the 32-bit kernel only touches identity addresses and port I/O.

    .text
    .code64
    .global efi_switch_to_32
efi_switch_to_32:
    cli

    lgdt gdt_ptr(%rip)

    movw $0x10, %ax          # 32-bit flat data selector
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl %ecx, %eax          # kernel entry point (fits in 32 bits)
    pushq $0x08              # 32-bit flat code selector
    pushq %rax               # entry point
    lretq                    # far return -> compatibility mode, CS=0x08, EIP=entry

    .align 8
gdt:
    .quad 0x0000000000000000 # 0x00: null descriptor
    .quad 0x00CF9B000000FFFF # 0x08: 32-bit code, base 0, limit 4 GiB
    .quad 0x00CF93000000FFFF # 0x10: 32-bit data, base 0, limit 4 GiB
gdt_ptr:
    .word gdt_ptr - gdt - 1  # limit
    .quad gdt                # base
