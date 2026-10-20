/* kernel.c */
#include "boot_info.h"

void fb_init(const boot_info_t *bi);
void fb_putchar(unsigned char c);

/* ---------------- VGA text buffer (visible when QEMU has a display) ------- */
#define VGA_ATTR 0x0F /* 0x0F = White text on Black background */

/* ---------------- COM1 16550 UART (so `qemu -nographic` shows it too) ----- */
#define COM1 0x3F8

static inline void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* enable DLAB (divisor latch access) */
    outb(COM1 + 0, 0x01); /* divisor low  -> 115200 baud */
    outb(COM1 + 1, 0x00); /* divisor high -> 115200 baud */
    outb(COM1 + 3, 0x03); /* 8 data bits, no parity, 1 stop bit, DLAB off */
    outb(COM1 + 2, 0xC7); /* enable FIFO, clear it, 14-byte trigger level */
    outb(COM1 + 4, 0x0B); /* DTR + RTS + OUT2 */
}

static void serial_putchar(char c)
{
    if (c == '\n')
        serial_putchar('\r'); /* terminals want CRLF */
    while (!(inb(COM1 + 5) & 0x20)) /* wait until transmitter is idle */
        ;
    outb(COM1, (unsigned char)c);
}

/* Write the same character to every active screen at once: VGA text,
 * COM1 serial, and the GOP framebuffer when the UEFI stub provided one. */
static void print(const char *str)
{
    volatile char *video_memory = (volatile char *)0xB8000;

    while (*str) {
        *video_memory++ = *str;   /* Set character byte */
        *video_memory++ = VGA_ATTR; /* Set color byte (White on Black) */
        serial_putchar(*str);
        fb_putchar((unsigned char)*str);
        str++;
    }
}

void kernel_main(unsigned int magic, unsigned int info)
{
    serial_init();
    /* On the UEFI path %ebx carries a boot_info_t* with the GOP
     * framebuffer; on the BIOS path there is none (VGA text instead). */
    fb_init(magic == BOOT_MAGIC_UEFI ? (const boot_info_t *)info : (const boot_info_t *)0);
    print("Hello from your 32-bit TCC Kernel!");
    serial_putchar('\n'); /* finish the serial line */
    fb_putchar('\n');
}
