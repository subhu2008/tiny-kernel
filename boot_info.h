/* boot_info.h -- hand-off from the boot stub to the kernel.
 *
 * Shared between the UEFI stub (clang, efi/boot.c) and the kernel (tcc).
 * _start passes %eax/%ebx to kernel_main in the multiboot style; on the
 * UEFI path %eax carries BOOT_MAGIC_UEFI and %ebx points at this struct.
 */
#ifndef BOOT_INFO_H
#define BOOT_INFO_H

/* "UEFI" -- replaces the multiboot magic (0x2BADB002) on the EFI path */
#define BOOT_MAGIC_UEFI 0x55454649

typedef struct {
    unsigned int fb_addr;  /* physical address of the GOP framebuffer, < 4 GiB */
    unsigned int fb_pitch; /* bytes per scanline */
    unsigned int fb_width; /* pixels */
    unsigned int fb_height;/* pixels */
    unsigned int fb_format;/* 0 = BGRX, 1 = RGBX (8-bit channels) */
    unsigned int acpi_rsdp; /* physical address of the ACPI RSDP published by
                             * the firmware, 0 if none (the UEFI stub looks it
                             * up in the configuration table; on the BIOS path
                             * there is no hand-off, so the kernel scans for
                             * the RSDP itself) */
} boot_info_t;

#endif
