/* efi/boot.c -- UEFI stub for the tiny kernel.
 *
 * Compiled as a freestanding x86-64 PE32+ EFI application (BOOTX64.EFI,
 * see the Makefile).  It:
 *   1. prints a banner on the UEFI console *and* COM1, so both the
 *      graphical firmware console and `qemu -nographic` show something,
 *   2. loads the embedded multiboot kernel.elf at its link address (1 MiB),
 *   3. exits boot services, copies the kernel segments into place and
 *      jumps to the kernel's 32-bit entry point (via switch32.S), passing
 *      the GOP framebuffer in a boot_info_t so the kernel can draw on it
 *      (the firmware console disappears with ExitBootServices).
 *
 * No UEFI headers are used; only the handful of table layouts we need are
 * spelled out below (offsets are for x86-64).
 */

#include "../boot_info.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef void *EFI_HANDLE;
typedef u64 EFI_STATUS;
#define EFI_ERROR(s)        (((s) >> 63) != 0)

typedef struct { u32 Data1; u16 Data2; u16 Data3; u8 Data4[8]; } EFI_GUID;
typedef struct {
    u64 Signature; u32 Revision; u32 HeaderSize; u32 CRC32; u32 Reserved;
} EFI_TABLE_HEADER;

/* ---- COM1, for headless visibility (same as the kernel's) ---- */
#define COM1 0x3F8

static void outb(u16 port, u8 value)
{ __asm__ volatile ("outb %0,%1" : : "a"(value), "Nd"(port)); }

static u8 inb(u16 port)
{ u8 value; __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port)); return value; }

static void serial_init(void)
{
    outb(COM1 + 1, 0x00); outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01); outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); outb(COM1 + 2, 0xC7); outb(COM1 + 4, 0x0B);
}

static void serial_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            while (!(inb(COM1 + 5) & 0x20)) ;
            outb(COM1, '\r');
        }
        while (!(inb(COM1 + 5) & 0x20)) ;
        outb(COM1, (u8)*s++);
    }
}

static void serial_puthex(u64 v)
{
    static const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        serial_puts((char[]){ hex[(v >> i) & 0xF], 0 });
}

/* ---- UEFI console (simple text output protocol) ---- */
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, u16 *String);
    void *TestString; void *QueryMode; void *SetMode; void *SetAttribute;
    void *ClearScreen; void *SetCursorPosition; void *EnableCursor; void *Mode;
};

struct EFI_BOOT_SERVICES; /* forward */

typedef struct {
    EFI_TABLE_HEADER Hdr;
    u16 *FirmwareVendor;
    u32 FirmwareRevision;
    void *ConsoleInHandle;
    void *ConIn;
    void *ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    void *StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    struct EFI_BOOT_SERVICES *BootServices;
    u64 NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct { EFI_GUID Guid; void *VendorTable; } EFI_CONFIGURATION_TABLE;

typedef struct {
    u32 Type;
    u32 Pad; /* x64 alignment */
    u64 PhysicalStart;
    u64 VirtualStart;
    u64 NumberOfPages;
    u64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Only the entries up to (and including) LocateProtocol are used. */
struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL; void *RestoreTPL;
    void *AllocatePages; void *FreePages;
    EFI_STATUS (*GetMemoryMap)(u64 *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap,
                               u64 *MapKey, u32 *DescriptorSize, u32 *DescriptorVersion);
    void *AllocatePool; void *FreePool;
    void *CreateEvent; void *SetTimer; void *WaitForEvent; void *SignalEvent;
    void *CloseEvent; void *CheckEvent;
    void *InstallProtocolInterface; void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, void *Protocol, void **Interface);
    void *Reserved; void *RegisterProtocolNotify;
    void *LocateHandle; void *LocateDevicePath; void *InstallConfigurationTable;
    void *LoadImage; void *StartImage; void *Exit; void *UnloadImage;
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE ImageHandle, u64 MapKey);
    void *GetNextMonotonicCount; void *Stall; void *SetWatchdogTimer;
    void *ConnectController; void *DisconnectController;
    void *OpenProtocol; void *CloseProtocol; void *OpenProtocolInformation;
    void *ProtocolsPerHandle; void *LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(void *Protocol, void *Registration, void **Interface);
};

/* loaded-image protocol: gives us where this stub itself lives */
typedef struct {
    u32 Revision; u32 Pad;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath; void *Reserved;
    u32 LoadOptionsSize; void *LoadOptions;
    void *ImageBase;
    u64 ImageSize;
    u32 ImageCodeType; u32 ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

static const EFI_GUID guid_loaded_image =
    { 0x5b1b31a1, 0x9562, 0x11d2, { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

/* ---- GOP (graphics output protocol): where the kernel may draw ---- */
typedef struct {
    u32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat; /* 0 BGRX, 1 RGBX, 2 bitmask, 3 BLT-only */
    EFI_PIXEL_BITMASK PixelInformation;
    u32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFO;

typedef struct {
    u32 MaxMode;
    u32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFO *Info;
    u32 SizeOfInfo;
    u64 FrameBufferBase;
    u64 FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode; void *SetMode; void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

static const EFI_GUID guid_gop =
    { 0x9042a9de, 0x23dc, 0x4a38, { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

/* ACPI RSDP in the firmware configuration table (ACPI 2.0+ and 1.0) */
static const EFI_GUID guid_acpi20 =
    { 0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } };
static const EFI_GUID guid_acpi10 =
    { 0xeb9d2d30, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (int i = 0; i < 16; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

/* Framebuffer handed to the kernel via %ebx; file scope (not a stack
 * local) so its address is stable and unambiguous in the hand-off. */
static boot_info_t bi;

/* ---- embedded kernel (efi/embed_kernel.S) ---- */
extern const u8 kstart[], kend[];

#define KERNEL_PHYS 0x100000
#define KERNEL_TAIL 0x8000 /* slack for .bss (16 KiB kernel stack) */

/* the 32-bit trampoline, efi/switch32.S: enters compatibility mode with
 * %eax/%ebx set like a multiboot hand-off (magic, info) */
void efi_switch_to_32(u32 entry, u32 magic, u32 info);

static void fail(const char *msg)
{
    serial_puts("UEFI stub: ERROR: ");
    serial_puts(msg);
    serial_puts("\n");
    for (;;) __asm__ volatile ("hlt");
}

static void kcopy(u8 *dst, const u8 *src, u32 n) { while (n--) *dst++ = *src++; }
static void kzero(u8 *dst, u32 n)               { while (n--) *dst++ = 0; }

static int overlaps(u64 a, u64 b, u64 c, u64 d) { return a < d && c < b; }

/* Load an ELF32 image at its physical addresses; returns its entry point. */
static u32 load_kernel(const u8 *elf, u32 len)
{
    if (elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F')
        fail("embedded kernel is not ELF");
    if (elf[4] != 1 /* ELFCLASS32 */)
        fail("embedded kernel is not ELF32");

    u32 e_entry = *(const u32 *)(elf + 0x18);
    u32 e_phoff = *(const u32 *)(elf + 0x1C);
    u16 e_phentsize = *(const u16 *)(elf + 0x2A);
    u16 e_phnum = *(const u16 *)(elf + 0x2C);

    for (u16 i = 0; i < e_phnum; i++) {
        const u8 *ph = elf + e_phoff + (u32)i * e_phentsize;
        if (*(const u32 *)ph != 1) /* PT_LOAD */
            continue;
        u32 p_offset = *(const u32 *)(ph + 4);
        u32 p_paddr  = *(const u32 *)(ph + 12);
        u32 p_filesz = *(const u32 *)(ph + 16);
        u32 p_memsz  = *(const u32 *)(ph + 20);
        if (p_offset + p_filesz > len)
            fail("kernel segment out of bounds");
        kcopy((u8 *)(u64)p_paddr, elf + p_offset, p_filesz);
        kzero((u8 *)(u64)(p_paddr + p_filesz), p_memsz - p_filesz);
    }
    return e_entry;
}

int efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    __asm__ volatile ("cli"); /* we poll everything; no interrupts needed */

    serial_init();
    serial_puts("UEFI stub: entered from firmware, hello!\n");

    /* L"" is UTF-16 thanks to -fshort-wchar */
    st->ConOut->OutputString(st->ConOut,
        L"tiny-kernel UEFI stub - loading 32-bit kernel...\r\n");

    struct EFI_BOOT_SERVICES *bs = st->BootServices;

    /* Find the graphics framebuffer so the kernel can draw on it (the
     * firmware console is gone once boot services exit).  `bi` is file
     * scope so its address survives unchanged into the hand-off. */
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    bi.fb_addr = bi.fb_pitch = bi.fb_width = bi.fb_height = bi.fb_format = 0;
    bi.acpi_rsdp = 0;
    if (EFI_ERROR(bs->LocateProtocol((void *)&guid_gop, 0, (void **)&gop))) {
        serial_puts("UEFI stub: no GOP protocol\n");
    } else if (!gop->Mode || !gop->Mode->Info) {
        serial_puts("UEFI stub: GOP has no mode info\n");
    } else if (gop->Mode->FrameBufferBase > 0xFFFFFFFFULL) {
        serial_puts("UEFI stub: GOP framebuffer above 4 GiB: ");
        serial_puthex(gop->Mode->FrameBufferBase);
        serial_puts("\n");
    } else {
        EFI_GRAPHICS_OUTPUT_MODE_INFO *mi = gop->Mode->Info;
        serial_puts("UEFI stub: GOP framebuffer ");
        serial_puthex(gop->Mode->FrameBufferBase);
        serial_puts(", ");
        serial_puthex(mi->HorizontalResolution);
        serial_puts("x");
        serial_puthex(mi->VerticalResolution);
        serial_puts(", format ");
        serial_puthex(mi->PixelFormat);
        serial_puts("\n");
        if (mi->PixelFormat == 3) { /* BLT-only: no linear framebuffer */
            serial_puts("UEFI stub: ...but BLT-only, no linear access\n");
        } else {
            bi.fb_addr   = (u32)gop->Mode->FrameBufferBase;
            bi.fb_pitch  = mi->PixelsPerScanLine * 4;
            bi.fb_width  = mi->HorizontalResolution;
            bi.fb_height = mi->VerticalResolution;
            if (mi->PixelFormat == 1)                    bi.fb_format = 1; /* RGBX */
            else if (mi->PixelFormat == 0)               bi.fb_format = 0; /* BGRX */
            else /* bitmask: check where the red channel sits */
                bi.fb_format = (mi->PixelInformation.RedMask & 0xFF) ? 1 : 0;
        }
    }

    /* The ACPI RSDP from the firmware configuration table: the kernel
     * needs it to find the power-management register and shut down. */
    {
        EFI_CONFIGURATION_TABLE *ct =
            (EFI_CONFIGURATION_TABLE *)st->ConfigurationTable;
        for (u64 i = 0; i < st->NumberOfTableEntries; i++) {
            if (guid_eq(&ct[i].Guid, &guid_acpi20) ||
                guid_eq(&ct[i].Guid, &guid_acpi10)) {
                u64 p = (u64)ct[i].VendorTable;
                if (p < 0x100000000ULL)
                    bi.acpi_rsdp = (u32)p;
                break;
            }
        }
        serial_puts("UEFI stub: ACPI RSDP ");
        serial_puthex(bi.acpi_rsdp);
        serial_puts("\n");
    }

    /* Make sure we may stomp the kernel's load area once boot services are
     * over: it must not overlap runtime/firmware-reserved memory, nor this
     * stub's own image. */
    static u8 map_buf[64 * 1024];
    u64 map_size, map_key = 0;
    u32 desc_size, desc_ver;
    int booted = 0;

    for (int tries = 0; tries < 8 && !booted; tries++) {
        map_size = sizeof(map_buf);
        if (EFI_ERROR(bs->GetMemoryMap(&map_size, (EFI_MEMORY_DESCRIPTOR *)map_buf,
                                       &map_key, &desc_size, &desc_ver)))
            fail("GetMemoryMap failed");

        u64 ksize = (u64)(kend - kstart) + KERNEL_TAIL;
        for (u64 off = 0; off + sizeof(EFI_MEMORY_DESCRIPTOR) <= map_size; off += desc_size) {
            EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(map_buf + off);
            /* types we must not overwrite: everything that is not plain RAM */
            switch (d->Type) {
            case 1: case 2: case 3: case 4: case 7: /* loader/BS code+data, conventional */
                break;
            default:
                if (overlaps(KERNEL_PHYS, KERNEL_PHYS + ksize,
                             d->PhysicalStart,
                             d->PhysicalStart + d->NumberOfPages * 4096))
                    fail("kernel load area overlaps firmware-reserved memory");
            }
        }

        EFI_LOADED_IMAGE_PROTOCOL *li = 0;
        if (!EFI_ERROR(bs->HandleProtocol(image, (void *)&guid_loaded_image, (void **)&li))
            && overlaps(KERNEL_PHYS, KERNEL_PHYS + ksize,
                        (u64)li->ImageBase, (u64)li->ImageBase + li->ImageSize))
            fail("kernel load area overlaps this EFI stub");

        if (!EFI_ERROR(bs->ExitBootServices(image, map_key)))
            booted = 1;
        /* else: the map changed, GetMemoryMap + retry */
    }
    if (!booted)
        fail("ExitBootServices failed");

    /* From here on nothing but the kernel matters. */
    u32 entry = load_kernel(kstart, kend - kstart);

    serial_puts("UEFI stub: jumping to 32-bit kernel entry\n");
    efi_switch_to_32(entry, BOOT_MAGIC_UEFI, (u32)(u64)&bi);

    fail("returned from kernel entry?!"); /* not reached */
    return 0;
}
