/* fb.c -- text output on the GOP pixel framebuffer (the UEFI/GOP path).
 *
 * The UEFI stub hands kernel_main a boot_info_t; when it carries a valid
 * framebuffer the kernel draws its text here as well as on VGA + serial.
 * Glyphs come from the 8x8 font in font.c, drawn at double height (8x16)
 * so the text has a sane aspect ratio.
 */
#include "boot_info.h"

extern const unsigned char font8x8[95][8];

#define FONT_W 8
#define FONT_H 16 /* 8x8 font rows drawn twice */

static volatile unsigned char *fb;
static unsigned fb_pitch, fb_width, fb_height, fb_rgb;
static unsigned cur_x, cur_y;

static void plot(unsigned x, unsigned y, unsigned char r,
                 unsigned char g, unsigned char b)
{
    volatile unsigned char *p = fb + y * fb_pitch + x * 4;
    if (fb_rgb) { p[0] = r; p[1] = g; p[2] = b; } /* RGBX */
    else        { p[2] = r; p[1] = g; p[0] = b; } /* BGRX */
    p[3] = 0xFF;
}

static void fb_clear(void)
{
    unsigned x, y;
    for (y = 0; y < fb_height; y++)
        for (x = 0; x < fb_width; x++)
            plot(x, y, 0, 0, 0);
}

void fb_init(const boot_info_t *bi)
{
    if (!bi || !bi->fb_addr || !bi->fb_pitch ||
        !bi->fb_width || bi->fb_height < FONT_H)
        return;
    fb        = (volatile unsigned char *)bi->fb_addr;
    fb_pitch  = bi->fb_pitch;
    fb_width  = bi->fb_width;
    fb_height = bi->fb_height;
    fb_rgb    = bi->fb_format == 1;
    fb_clear();
    cur_x = cur_y = 0;
}

static void fb_scroll(void)
{
    unsigned i, n = (fb_height - FONT_H) * fb_pitch;
    for (i = 0; i < n; i++)
        fb[i] = fb[i + FONT_H * fb_pitch];
    for (i = n; i < fb_height * fb_pitch; i++)
        fb[i] = 0;
    cur_y = fb_height - FONT_H;
}

static void fb_glyph(unsigned char c, unsigned x, unsigned y)
{
    const unsigned char *glyph;
    unsigned row, col, bits;

    if (c < 32 || c > 126)
        c = '?';
    glyph = font8x8[c - 32];
    for (row = 0; row < 8; row++) {
        bits = glyph[row];
        for (col = 0; col < 8; col++)
            if (bits & (1 << col)) {               /* bit 0 is the leftmost pixel */
                plot(x + col, y + 2 * row,     255, 255, 255);
                plot(x + col, y + 2 * row + 1, 255, 255, 255);
            }
    }
}

void fb_putchar(unsigned char c)
{
    if (!fb)
        return;
    if (c == '\n') {
        cur_x = 0;
        cur_y += FONT_H;
    } else {
        if (cur_x + FONT_W > fb_width) {
            cur_x = 0;
            cur_y += FONT_H;
        }
        fb_glyph(c, cur_x, cur_y);
        cur_x += FONT_W;
    }
    if (cur_y + FONT_H > fb_height)
        fb_scroll();
}
