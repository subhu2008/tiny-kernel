/* shutdown.c -- power the machine off, the way an OS does it.
 *
 * Both SeaBIOS and OVMF publish ACPI, and QEMU powers the guest off when
 * SLP_EN is written to the PM1a control register with SLP_TYP 0 (its
 * "soft power off" mapping, see hw/acpi/core.c acpi_pm_cnt_write).  We
 * find that register the proper way -- RSDP -> RSDT/XSDT -> FACP ->
 * PM1a_CNT_BLK -- taking the RSDP from the UEFI stub's boot_info_t or,
 * on the legacy BIOS path, scanning for it ourselves where ACPI says it
 * must live: the first 1 KiB of the EBDA, or the BIOS ROM area
 * 0xE0000..0xFFFFF.  If ACPI turns out to be unusable we fall back to
 * QEMU's default PM I/O base (0x600 + 4) and finally to pulsing the
 * keyboard controller's reset line.
 *
 * pause_2s() waits on the ACPI power-management timer (3.579545 MHz,
 * free-running, no interrupts needed) so the caller's output stays
 * readable for a moment before the machine goes away.
 */
#include "boot_info.h"

/* ------------------------- I/O ports ---------------------------------- */

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void outw(unsigned short port, unsigned short value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned int inl(unsigned short port)
{
    unsigned int value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ------------------------- ACPI plumbing ------------------------------ */

static unsigned short pm1a_cnt_port; /* PM1a control register, 0 = unknown */
static unsigned short pm_tmr_port;   /* ACPI PM timer port,   0 = unknown */

static int checksum_ok(const unsigned char *p, unsigned int n)
{
    unsigned char sum = 0;
    while (n--)
        sum += *p++;
    return sum == 0;
}

/* Validate a candidate "RSD PTR " signature (revision 1 or 2). */
static const unsigned char *rsdp_check(const unsigned char *p)
{
    if (p[0] != 'R' || p[1] != 'S' || p[2] != 'D' || p[3] != ' ' ||
        p[4] != 'P' || p[5] != 'T' || p[6] != 'R' || p[7] != ' ')
        return 0;
    if (!checksum_ok(p, 20))
        return 0;
    if (p[15] >= 2) { /* rev 2+: Length covers the whole structure */
        unsigned int len = *(const unsigned int *)(p + 20);
        if (len < 36 || len > 4096 || !checksum_ok(p, len))
            return 0;
    }
    return p;
}

/* Scan `len` bytes at `start` for a valid RSDP (16-byte aligned). */
static const unsigned char *rsdp_scan(unsigned int start, unsigned int len)
{
    unsigned int a;
    for (a = start; a + 20 <= start + len; a += 16) {
        const unsigned char *p = rsdp_check((const unsigned char *)a);
        if (p)
            return p;
    }
    return 0;
}

static const unsigned char *find_rsdp(const boot_info_t *bi)
{
    const unsigned char *p;
    unsigned int ebda;

    /* UEFI path: the stub read the RSDP out of the firmware's
     * configuration table and handed it to us in boot_info_t. */
    if (bi && bi->acpi_rsdp) {
        p = rsdp_check((const unsigned char *)bi->acpi_rsdp);
        if (p)
            return p;
    }

    /* Legacy path (and UEFI without a hint): the EBDA segment lives at
     * 0x40:0x0E, and every address below 1 MiB is mapped anyway. */
    ebda = (unsigned int)(*(const volatile unsigned short *)0x40E) << 4;
    if (ebda && ebda < 0x100000) {
        p = rsdp_scan(ebda, 1024);
        if (p)
            return p;
    }
    return rsdp_scan(0xE0000, 0x20000); /* BIOS read-only memory area */
}

/* Check an ACPI root table header ("RSDT" or "XSDT") and return it. */
static const unsigned char *root_check(const unsigned char *t, const char *sig,
                                       unsigned int ent_size)
{
    unsigned int i, len;
    if (!t)
        return 0;
    for (i = 0; i < 4; i++)
        if (t[i] != (unsigned char)sig[i])
            return 0;
    len = *(const unsigned int *)(t + 4);
    if (len < 36 + ent_size || len > 0x4000 || !checksum_ok(t, len))
        return 0;
    return t;
}

/* Walk RSDT/XSDT and return the FACP (a.k.a. FADT), if there is one. */
static const unsigned char *find_facp(const unsigned char *rsdp)
{
    const unsigned char *root = 0;
    unsigned int ent_size = 4, len, n, i;

    if (rsdp[15] >= 2) { /* rev 2+: prefer the 64-bit XSDT */
        unsigned long long x = *(const unsigned long long *)(rsdp + 24);
        if (x < 0x100000000ULL)
            root = root_check((const unsigned char *)(unsigned int)x,
                              "XSDT", 8);
        if (root)
            ent_size = 8;
    }
    if (!root) /* ACPI 1.0 RSDT, or a broken XSDT: try the 32-bit one */
        root = root_check((const unsigned char *)
                              *(const unsigned int *)(rsdp + 16),
                          "RSDT", 4);
    if (!root)
        return 0;

    len = *(const unsigned int *)(root + 4);
    n = (len - 36) / ent_size;
    for (i = 0; i < n; i++) {
        unsigned long long e = ent_size == 8
            ? *(const unsigned long long *)(root + 36 + i * 8)
            : *(const unsigned int *)(root + 36 + i * 4);
        const unsigned char *entry;
        if (!e || e >= 0x100000000ULL)
            continue;
        entry = (const unsigned char *)(unsigned int)e;
        if (entry[0] != 'F' || entry[1] != 'A' ||
            entry[2] != 'C' || entry[3] != 'P')
            continue;
        len = *(const unsigned int *)(entry + 4);
        if (len < 116 || len > 0x1000 || !checksum_ok(entry, len))
            return 0; /* the FACP exists but is corrupt */
        return entry;
    }
    return 0;
}

/* FACP offsets (ACPI table header included, hence 36 more than the
 * struct-relative values in the spec):
 *   64  PM1a_CNT_BLK   (u32, System I/O port)
 *   76  PM_TMR_BLK     (u32)
 *  172  X_PM1a_CNT_BLK (generic address structure: space id + u64 address)
 *  208  X_PM_TMR_BLK   (same) */
static unsigned int facp_port(const unsigned char *facp, unsigned int len,
                              unsigned int legacy_off, unsigned int x_off)
{
    unsigned int port = *(const unsigned int *)(facp + legacy_off);
    if (!port && len >= x_off + 16 && facp[x_off] == 1 /* System I/O */) {
        unsigned long long a = *(const unsigned long long *)(facp + x_off + 4);
        if (a <= 0xFFFF)
            port = (unsigned int)a;
    }
    return port <= 0xFFFF ? port : 0; /* rejects 0 too */
}

/* Look up the ACPI power registers once; called before shutdown(). */
void shutdown_init(const boot_info_t *bi)
{
    const unsigned char *rsdp = find_rsdp(bi);
    const unsigned char *facp;
    unsigned int len;

    if (!rsdp)
        return;
    facp = find_facp(rsdp);
    if (!facp)
        return;

    len = *(const unsigned int *)(facp + 4);

    pm1a_cnt_port = (unsigned short)facp_port(facp, len, 64, 172);
    pm_tmr_port   = (unsigned short)facp_port(facp, len, 76, 208);
}

/* ------------------------- the pause ---------------------------------- */

#define PM_TICKS_PER_2S 7159090u /* 3.579545 MHz * 2 s */

void pause_2s(void)
{
    unsigned int start, prev, stall = 0;

    if (!pm_tmr_port)
        return; /* no ACPI PM timer: carry on immediately */
    start = inl(pm_tmr_port) & 0xFFFFFF;
    prev = start;
    for (;;) {
        unsigned int now = inl(pm_tmr_port) & 0xFFFFFF;
        unsigned int elapsed = now >= start ? now - start
                                            : 0x1000000u - start + now;
        if (elapsed >= PM_TICKS_PER_2S)
            break;
        if (now == prev) {
            if (++stall > 10000000u)
                break; /* the timer never moved; don't spin forever */
        } else {
            stall = 0;
        }
        prev = now;
    }
}

/* ------------------------- the shutdown ------------------------------- */

/* Powers the machine off.  Only returns if every attempt failed, in
 * which case boot.S's halt loop takes over. */
void shutdown(void)
{
    /* ACPI PM1a sleep control: SLP_EN (bit 13) with a sleep type.
     * QEMU treats SLP_TYP 0 as soft power off; 5 is the customary _S5_
     * type on real hardware, so try both. */
    if (pm1a_cnt_port) {
        outw(pm1a_cnt_port, 0x2000);
        outw(pm1a_cnt_port, 0x3400);
    }
    /* QEMU's default PM I/O base, in case ACPI was unusable. */
    if (pm1a_cnt_port != 0x604)
        outw(0x604, 0x2000);
    /* Last resort: pulse the CPU reset line through the keyboard
     * controller (run QEMU with -no-reboot so this ends the session
     * instead of rebooting into it). */
    outb(0x64, 0xFE);
}
