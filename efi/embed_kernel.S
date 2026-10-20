# efi/embed_kernel.S -- embed the legacy multiboot kernel.elf into the EFI app
#
# Assembled with -I$(O) so .incbin finds the kernel built into the output
# directory.  efi/boot.c walks the ELF program headers and copies the
# segments to their physical addresses at run time.

    .section .rdata,"dr"
    .global kstart
    .global kend
kstart:
    .incbin "kernel.elf"
kend:
