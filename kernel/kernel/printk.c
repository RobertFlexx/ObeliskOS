/*
 * Obelisk OS - Kernel Printf
 * From Axioms, Order.
 *
 * Provides formatted output for kernel messages.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/bootinfo.h>
#include <obelisk/zig_mul.h>
#include <mm/vmm.h>
#include <stdarg.h>

/* Output function */
extern void uart_putc(char c);
extern void uart_puts(const char *s);

/* VGA text console mirror (for local hardware debugging). */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x0F
static volatile uint16_t *vga_buf = (volatile uint16_t *)PHYS_TO_VIRT(0xB8000);
static size_t vga_row = 0;
static size_t vga_col = 0;

/* Optional framebuffer text mirror (for graphical VM/bare-metal boots). */
struct fb_console {
    bool ready;
    uint8_t *virt;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint32_t cols;
    uint32_t rows;
    uint32_t row;
    uint32_t col;
    uint32_t cell_w;
    uint32_t cell_h;
    uint32_t scale_x;
    uint32_t scale_y;
    uint32_t fg;
    uint32_t bg;
};
static struct fb_console g_fbcon;

#define FB_FONT_W 8
#define FB_FONT_H 16
static bool g_fb_ansi_esc;
static bool g_fb_ansi_csi;
static char g_fb_csi_buf[32];
static size_t g_fb_csi_len;
static bool g_fb_cursor_on;
static uint64_t g_fb_cursor_next_blink_ns;
static const uint64_t FB_CURSOR_BLINK_NS = 1500000000ULL;
static void fb_clear_rows(uint32_t y_start, uint32_t y_count);

/*
 * Built-in 8x8 bitmap glyphs for U+0020..U+007F (printable ASCII + DEL).
 * Based on public-domain VGA font data.
 */
static const uint8_t g_fb_font8x8_basic[96][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x20 ' ' */
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* 0x21 '!' */
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x22 '"' */
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 }, /* 0x23 '#' */
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00 }, /* 0x24 '$' */
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00 }, /* 0x25 '%' */
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00 }, /* 0x26 '&' */
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x27 ''' */
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00 }, /* 0x28 '(' */
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00 }, /* 0x29 ')' */
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, /* 0x2A '*' */
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00 }, /* 0x2B '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06 }, /* 0x2C ',' */
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00 }, /* 0x2D '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 }, /* 0x2E '.' */
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00 }, /* 0x2F '/' */
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 }, /* 0x30 '0' */
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 }, /* 0x31 '1' */
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 }, /* 0x32 '2' */
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 }, /* 0x33 '3' */
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 }, /* 0x34 '4' */
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 }, /* 0x35 '5' */
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 }, /* 0x36 '6' */
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 }, /* 0x37 '7' */
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 }, /* 0x38 '8' */
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 }, /* 0x39 '9' */
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00 }, /* 0x3A ':' */
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06 }, /* 0x3B ';' */
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00 }, /* 0x3C '<' */
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00 }, /* 0x3D '=' */
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 }, /* 0x3E '>' */
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00 }, /* 0x3F '?' */
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00 }, /* 0x40 '@' */
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00 }, /* 0x41 'A' */
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00 }, /* 0x42 'B' */
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00 }, /* 0x43 'C' */
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00 }, /* 0x44 'D' */
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00 }, /* 0x45 'E' */
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00 }, /* 0x46 'F' */
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00 }, /* 0x47 'G' */
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00 }, /* 0x48 'H' */
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* 0x49 'I' */
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00 }, /* 0x4A 'J' */
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00 }, /* 0x4B 'K' */
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00 }, /* 0x4C 'L' */
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00 }, /* 0x4D 'M' */
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 }, /* 0x4E 'N' */
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 }, /* 0x4F 'O' */
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00 }, /* 0x50 'P' */
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00 }, /* 0x51 'Q' */
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00 }, /* 0x52 'R' */
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00 }, /* 0x53 'S' */
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* 0x54 'T' */
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00 }, /* 0x55 'U' */
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 }, /* 0x56 'V' */
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }, /* 0x57 'W' */
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00 }, /* 0x58 'X' */
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00 }, /* 0x59 'Y' */
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00 }, /* 0x5A 'Z' */
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00 }, /* 0x5B '[' */
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 }, /* 0x5C '\' */
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 }, /* 0x5D ']' */
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00 }, /* 0x5E '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF }, /* 0x5F '_' */
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x60 '`' */
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00 }, /* 0x61 'a' */
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00 }, /* 0x62 'b' */
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00 }, /* 0x63 'c' */
    { 0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00 }, /* 0x64 'd' */
    { 0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00 }, /* 0x65 'e' */
    { 0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00 }, /* 0x66 'f' */
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F }, /* 0x67 'g' */
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00 }, /* 0x68 'h' */
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* 0x69 'i' */
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E }, /* 0x6A 'j' */
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00 }, /* 0x6B 'k' */
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 }, /* 0x6C 'l' */
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00 }, /* 0x6D 'm' */
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00 }, /* 0x6E 'n' */
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00 }, /* 0x6F 'o' */
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F }, /* 0x70 'p' */
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78 }, /* 0x71 'q' */
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00 }, /* 0x72 'r' */
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00 }, /* 0x73 's' */
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00 }, /* 0x74 't' */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00 }, /* 0x75 'u' */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 }, /* 0x76 'v' */
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 }, /* 0x77 'w' */
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00 }, /* 0x78 'x' */
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F }, /* 0x79 'y' */
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00 }, /* 0x7A 'z' */
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00 }, /* 0x7B '{' */
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 }, /* 0x7C '|' */
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00 }, /* 0x7D '}' */
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x7E '~' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }  /* 0x7F */
};

static inline void fb_write_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!g_fbcon.ready || x >= g_fbcon.width || y >= g_fbcon.height) return;
    uint8_t *p = g_fbcon.virt + (size_t)y * g_fbcon.pitch + (size_t)x * (g_fbcon.bpp / 8U);
    if (g_fbcon.bpp == 32) {
        *(uint32_t *)p = rgb;
    } else if (g_fbcon.bpp == 24) {
        p[0] = (uint8_t)(rgb & 0xFF);
        p[1] = (uint8_t)((rgb >> 8) & 0xFF);
        p[2] = (uint8_t)((rgb >> 16) & 0xFF);
    }
}

static uint32_t fb_ansi_to_rgb(unsigned code, bool bg) {
    static const uint32_t normal[8] = {
        0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
        0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA
    };
    static const uint32_t bright[8] = {
        0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
        0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
    };

    if (!bg && code >= 30U && code <= 37U) return normal[code - 30U];
    if (!bg && code >= 90U && code <= 97U) return bright[code - 90U];
    if (bg && code >= 40U && code <= 47U) return normal[code - 40U];
    if (bg && code >= 100U && code <= 107U) return bright[code - 100U];
    return bg ? 0x000000 : 0xFFFFFF;
}

static void fb_cursor_erase(void) {
    if (!g_fbcon.ready || !g_fb_cursor_on) return;
    uint32_t px = g_fbcon.col * g_fbcon.cell_w;
    uint32_t py = g_fbcon.row * g_fbcon.cell_h + (g_fbcon.cell_h - g_fbcon.scale_y);
    for (uint32_t y = 0; y < 2U; y++) {
        for (uint32_t x = 0; x < g_fbcon.cell_w; x++) {
            fb_write_pixel(px + x, py + y, g_fbcon.bg);
        }
    }
    g_fb_cursor_on = false;
}

static void fb_cursor_draw(void) {
    if (!g_fbcon.ready || g_fb_cursor_on) return;
    uint32_t px = g_fbcon.col * g_fbcon.cell_w;
    uint32_t py = g_fbcon.row * g_fbcon.cell_h + (g_fbcon.cell_h - g_fbcon.scale_y);
    for (uint32_t y = 0; y < 2U; y++) {
        for (uint32_t x = 0; x < g_fbcon.cell_w; x++) {
            fb_write_pixel(px + x, py + y, g_fbcon.fg);
        }
    }
    g_fb_cursor_on = true;
}

static void fb_apply_sgr_from_buf(void) {
    unsigned value = 0;
    bool have_value = false;
    size_t i = 0;

    if (g_fb_csi_len == 0) {
        g_fbcon.fg = 0xFFFFFF;
        g_fbcon.bg = 0x000000;
        return;
    }

    for (;;) {
        if (i < g_fb_csi_len && g_fb_csi_buf[i] >= '0' && g_fb_csi_buf[i] <= '9') {
            value = (value * 10U) + (unsigned)(g_fb_csi_buf[i] - '0');
            have_value = true;
            i++;
            continue;
        }

        if (have_value) {
            if (value == 0U) {
                g_fbcon.fg = 0xFFFFFF;
                g_fbcon.bg = 0x000000;
            } else if (value == 39U) {
                g_fbcon.fg = 0xFFFFFF;
            } else if (value == 49U) {
                g_fbcon.bg = 0x000000;
            } else if ((value >= 30U && value <= 37U) || (value >= 90U && value <= 97U)) {
                g_fbcon.fg = fb_ansi_to_rgb(value, false);
            } else if ((value >= 40U && value <= 47U) || (value >= 100U && value <= 107U)) {
                g_fbcon.bg = fb_ansi_to_rgb(value, true);
            }
            value = 0;
            have_value = false;
        }

        if (i >= g_fb_csi_len) break;
        i++;
    }
}

static unsigned fb_csi_first_param_default(unsigned def) {
    unsigned v = 0;
    bool have = false;
    for (size_t i = 0; i < g_fb_csi_len; i++) {
        char ch = g_fb_csi_buf[i];
        if (ch >= '0' && ch <= '9') {
            v = (v * 10U) + (unsigned)(ch - '0');
            have = true;
            continue;
        }
        break;
    }
    return have ? v : def;
}

static void fb_clear_cols(uint32_t row, uint32_t col_start, uint32_t col_count) {
    if (!g_fbcon.ready || row >= g_fbcon.rows || col_count == 0) return;
    if (col_start >= g_fbcon.cols) return;
    if (col_start + col_count > g_fbcon.cols) {
        col_count = g_fbcon.cols - col_start;
    }

    uint32_t x0 = col_start * g_fbcon.cell_w;
    uint32_t x1 = x0 + col_count * g_fbcon.cell_w;
    uint32_t y0 = row * g_fbcon.cell_h;
    uint32_t y1 = y0 + g_fbcon.cell_h;
    for (uint32_t y = y0; y < y1; y++) {
        for (uint32_t x = x0; x < x1; x++) {
            fb_write_pixel(x, y, g_fbcon.bg);
        }
    }
}

static void fb_handle_csi_final(char final) {
    if (final == 'm') {
        fb_apply_sgr_from_buf();
    } else if (final == 'J') {
        if (g_fb_csi_len == 0 || (g_fb_csi_len == 1 && g_fb_csi_buf[0] == '2')) {
            fb_clear_rows(0, g_fbcon.height);
            g_fbcon.row = 0;
            g_fbcon.col = 0;
        }
    } else if (final == 'K') {
        unsigned mode = fb_csi_first_param_default(0);
        if (mode == 0U) {
            fb_clear_cols(g_fbcon.row, g_fbcon.col, g_fbcon.cols - g_fbcon.col);
        } else if (mode == 1U) {
            fb_clear_cols(g_fbcon.row, 0, g_fbcon.col + 1U);
        } else if (mode == 2U) {
            fb_clear_cols(g_fbcon.row, 0, g_fbcon.cols);
        }
    } else if (final == 'H' || final == 'f') {
        g_fbcon.row = 0;
        g_fbcon.col = 0;
    }
}

static void fb_clear_rows(uint32_t y_start, uint32_t y_count) {
    if (!g_fbcon.ready) return;
    uint32_t y_end = y_start + y_count;
    if (y_end > g_fbcon.height) y_end = g_fbcon.height;
    if (g_fbcon.bpp == 32 && g_fbcon.bg == 0) {
        for (uint32_t y = y_start; y < y_end; y++) {
            uint8_t *row = g_fbcon.virt + (size_t)y * g_fbcon.pitch;
            memset(row, 0, g_fbcon.pitch);
        }
        return;
    }
    for (uint32_t y = y_start; y < y_end; y++) {
        for (uint32_t x = 0; x < g_fbcon.width; x++) {
            fb_write_pixel(x, y, g_fbcon.bg);
        }
    }
}

static void fb_scroll(void) {
    uint64_t move_bytes_u64;
    size_t line_bytes;
    size_t move_lines;

    if (!g_fbcon.ready) return;
    fb_cursor_erase();
    line_bytes = g_fbcon.pitch;
    move_lines = (g_fbcon.rows > 1) ? (size_t)(g_fbcon.rows - 1) * g_fbcon.cell_h : 0;
    if (move_lines > 0) {
        if (zig_u64_mul_ok((uint64_t)move_lines, (uint64_t)line_bytes, &move_bytes_u64) != 0 ||
            move_bytes_u64 > (uint64_t)SIZE_MAX) {
            /* Avoid printk here: printk -> fb_putc -> fb_scroll -> printk. */
            fb_clear_rows(0, g_fbcon.height);
            g_fbcon.row = 0;
            g_fbcon.col = 0;
            return;
        }
        memmove(g_fbcon.virt, g_fbcon.virt + (size_t)g_fbcon.cell_h * line_bytes, (size_t)move_bytes_u64);
    }
    fb_clear_rows((g_fbcon.rows > 0 ? g_fbcon.rows - 1 : 0) * g_fbcon.cell_h, g_fbcon.cell_h);
    if (g_fbcon.row > 0) g_fbcon.row--;
}

static void fb_draw_char(char c) {
    if (!g_fbcon.ready) return;
    uint8_t uc = (uint8_t)c;
    if (uc < 32 || uc > 127) uc = '?';
    const uint8_t *font = g_fb_font8x8_basic[uc - 32U];
    uint32_t px = g_fbcon.col * g_fbcon.cell_w;
    uint32_t py = g_fbcon.row * g_fbcon.cell_h;
    for (uint32_t y = 0; y < 8U; y++) {
        uint8_t bits = font[y];
        for (uint32_t x = 0; x < FB_FONT_W; x++) {
            uint32_t color = (bits & (1U << x)) ? g_fbcon.fg : g_fbcon.bg;
            uint32_t x0 = px + (x * g_fbcon.scale_x);
            uint32_t y0 = py + (y * 2U * g_fbcon.scale_y);
            for (uint32_t sx = 0; sx < g_fbcon.scale_x; sx++) {
                for (uint32_t sy = 0; sy < g_fbcon.scale_y; sy++) {
                    fb_write_pixel(x0 + sx, y0 + sy, color);
                    fb_write_pixel(x0 + sx, y0 + g_fbcon.scale_y + sy, color);
                }
            }
        }
    }
}

static void fb_putc(char c) {
    if (!g_fbcon.ready) return;
    fb_cursor_erase();
    if (g_fb_ansi_esc) {
        if (g_fb_ansi_csi) {
            if (g_fb_csi_len < sizeof(g_fb_csi_buf) &&
                ((c >= '0' && c <= '9') || c == ';')) {
                g_fb_csi_buf[g_fb_csi_len++] = c;
                return;
            }
            fb_handle_csi_final(c);
            g_fb_ansi_esc = false;
            g_fb_ansi_csi = false;
            g_fb_csi_len = 0;
            g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
            return;
        }
        if (c == '[') {
            g_fb_ansi_csi = true;
            g_fb_csi_len = 0;
            return;
        }
        g_fb_ansi_esc = false;
        return;
    }
    if ((uint8_t)c == 0x1B) {
        g_fb_ansi_esc = true;
        g_fb_ansi_csi = false;
        g_fb_csi_len = 0;
        g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
        return;
    }
    if (c == '\r') {
        g_fbcon.col = 0;
        g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
        return;
    }
    if (c == '\n') {
        g_fbcon.col = 0;
        g_fbcon.row++;
        if (g_fbcon.row >= g_fbcon.rows) fb_scroll();
        g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
        return;
    }
    if (c == '\b') {
        if (g_fbcon.col > 0) {
            g_fbcon.col--;
            fb_clear_cols(g_fbcon.row, g_fbcon.col, 1);
        }
        g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
        return;
    }
    if (c == '\t') {
        uint32_t next = (g_fbcon.col + 8U) & ~7U;
        if (next >= g_fbcon.cols) {
            g_fbcon.col = 0;
            g_fbcon.row++;
            if (g_fbcon.row >= g_fbcon.rows) fb_scroll();
        } else {
            g_fbcon.col = next;
        }
        g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
        return;
    }
    fb_draw_char(c);
    g_fbcon.col++;
    if (g_fbcon.col >= g_fbcon.cols) {
        g_fbcon.col = 0;
        g_fbcon.row++;
        if (g_fbcon.row >= g_fbcon.rows) fb_scroll();
    }
    g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
}

static void vga_scroll(void) {
    for (size_t r = 1; r < VGA_HEIGHT; r++) {
        for (size_t c = 0; c < VGA_WIDTH; c++) {
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
        }
    }
    for (size_t c = 0; c < VGA_WIDTH; c++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((uint16_t)VGA_ATTR << 8) | ' ';
    }
    if (vga_row > 0) vga_row--;
}

static void vga_putc(char c) {
    if (c == '\r') {
        vga_col = 0;
        return;
    }
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    vga_buf[vga_row * VGA_WIDTH + vga_col] = ((uint16_t)VGA_ATTR << 8) | (uint8_t)c;
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

/* Log buffer */
#define LOG_BUF_SIZE    (128 * 1024)    /* 128 KB */
static char log_buffer[LOG_BUF_SIZE];
static size_t log_write_idx = 0;
static size_t log_read_idx = 0;

/* Current log level */
static int console_loglevel = 7;    /* Print everything */
static int default_loglevel = 4;    /* KERN_WARNING */

/* Log level names */

/* Add character to log buffer */
static void log_buffer_add(char c) {
    log_buffer[log_write_idx] = c;
    log_write_idx = (log_write_idx + 1) % LOG_BUF_SIZE;
    
    /* Handle overflow */
    if (log_write_idx == log_read_idx) {
        log_read_idx = (log_read_idx + 1) % LOG_BUF_SIZE;
    }
}

/* Output character */
static void printk_putc(char c) {
    log_buffer_add(c);
    uart_putc(c);
    if (!g_fbcon.ready) {
        vga_putc(c);
    }
    fb_putc(c);
}

void console_fb_init(void) {
    const struct obelisk_framebuffer_info *fb = bootinfo_framebuffer();
    uint64_t map_phys;
    uint64_t map_size;
    uint64_t virt_base;
    uint64_t fb_bytes;
    int ret;

    memset(&g_fbcon, 0, sizeof(g_fbcon));
    if (!fb || !fb->available || fb->width == 0 || fb->height == 0 || fb->pitch == 0) return;
    if (!(fb->bpp == 24 || fb->bpp == 32)) return;

    fb_bytes = (uint64_t)fb->pitch * (uint64_t)fb->height;
    if (fb_bytes == 0 || fb_bytes > (uint64_t)SIZE_MAX) return;

    map_phys = ALIGN_DOWN(fb->phys_addr, PAGE_SIZE);
    map_size = ALIGN_UP((fb->phys_addr - map_phys) + fb_bytes, PAGE_SIZE);
    virt_base = (uint64_t)PHYS_TO_VIRT(map_phys);
    ret = mmu_map_range(mmu_get_kernel_pt(), virt_base, map_phys, (size_t)map_size, PTE_WRITABLE | PTE_NOCACHE);
    if (ret < 0) return;

    g_fbcon.ready = true;
    g_fbcon.virt = (uint8_t *)(virt_base + (fb->phys_addr - map_phys));
    g_fbcon.width = fb->width;
    g_fbcon.height = fb->height;
    g_fbcon.pitch = fb->pitch;
    g_fbcon.bpp = fb->bpp;
    /*
     * Uniform X/Y scale so glyphs stay square (wide-only scale looked "squashed").
     * Step up on common laptop/desktop resolutions; cap at 3 for very large panels.
     */
    {
        uint32_t w = fb->width;
        uint32_t h = fb->height;
        uint32_t scale = 1U;

        if (w >= 1280U && h >= 720U) {
            scale = 2U;
        }
        if (w >= 2560U && h >= 1440U) {
            scale = 3U;
        }
        g_fbcon.scale_x = scale;
        g_fbcon.scale_y = scale;
    }
    g_fbcon.cell_w = FB_FONT_W * g_fbcon.scale_x;
    g_fbcon.cell_h = FB_FONT_H * g_fbcon.scale_y;
    g_fbcon.cols = fb->width / g_fbcon.cell_w;
    g_fbcon.rows = fb->height / g_fbcon.cell_h;
    if (g_fbcon.cols == 0) g_fbcon.cols = 1;
    if (g_fbcon.rows == 0) g_fbcon.rows = 1;
    g_fbcon.fg = 0xFFFFFF;
    g_fbcon.bg = 0x000000;
    g_fb_ansi_esc = false;
    g_fb_ansi_csi = false;
    g_fb_csi_len = 0;
    g_fb_cursor_on = false;
    fb_clear_rows(0, g_fbcon.height);
    g_fb_cursor_next_blink_ns = get_time_ns() + FB_CURSOR_BLINK_NS;
    fb_cursor_draw();
}

void console_putc(char c) {
    printk_putc(c);
}

void console_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printk_putc(buf[i]);
    }
}

void console_poll(void) {
    if (!g_fbcon.ready) return;
    if (!g_fb_cursor_on) {
        fb_cursor_draw();
    }
}

/* Output string */
static void printk_puts(const char *s) {
    while (*s) {
        printk_putc(*s++);
    }
}

/* Print unsigned number */
static void print_number(uint64_t num, int base, int width, char pad,
                         bool uppercase, bool sign, bool is_negative) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    
    char buf[64];
    int i = 0;
    
    if (num == 0) {
        buf[i++] = '0';
    } else {
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
    }
    
    /* Calculate padding */
    int total_len = i;
    if (sign || is_negative) total_len++;
    
    /* Add padding */
    while (total_len < width) {
        if (pad == '0' && (sign || is_negative)) {
            if (is_negative) {
                printk_putc('-');
                is_negative = false;
            } else if (sign) {
                printk_putc('+');
                sign = false;
            }
        }
        printk_putc(pad);
        total_len++;
    }
    
    /* Print sign */
    if (is_negative) {
        printk_putc('-');
    } else if (sign) {
        printk_putc('+');
    }
    
    /* Print digits in reverse */
    while (i > 0) {
        printk_putc(buf[--i]);
    }
}

/* Print signed number */
static void print_signed(int64_t num, int base, int width, char pad, bool sign) {
    bool is_negative = false;
    
    if (num < 0) {
        is_negative = true;
        num = -num;
    }
    
    print_number((uint64_t)num, base, width, pad, false, sign, is_negative);
}

/* Core printf implementation */
static int do_printf(const char *fmt, va_list args) {
    int count = 0;
    int width;
    char pad;
    bool is_long, is_longlong;
    
    while (*fmt) {
        if (*fmt != '%') {
            printk_putc(*fmt);
            count++;
            fmt++;
            continue;
        }
        
        fmt++;  /* Skip '%' */
        
        /* Check for %% */
        if (*fmt == '%') {
            printk_putc('%');
            count++;
            fmt++;
            continue;
        }
        
        /* Parse flags */
        pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        
        /* Parse width */
        width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        /* Parse length modifier */
        is_long = false;
        is_longlong = false;
        
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') {
                is_longlong = true;
                fmt++;
            }
        } else if (*fmt == 'z') {
            is_longlong = true;  /* size_t */
            fmt++;
        }
        
        /* Parse conversion specifier */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t val;
                if (is_longlong) {
                    val = va_arg(args, int64_t);
                } else if (is_long) {
                    val = va_arg(args, long);
                } else {
                    val = va_arg(args, int);
                }
                print_signed(val, 10, width, pad, false);
                break;
            }
            
            case 'u': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 10, width, pad, false, false, false);
                break;
            }
            
            case 'x': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 16, width, pad, false, false, false);
                break;
            }
            
            case 'X': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 16, width, pad, true, false, false);
                break;
            }
            
            case 'p': {
                void *ptr = va_arg(args, void *);
                printk_puts("0x");
                print_number((uint64_t)ptr, 16, 16, '0', false, false, false);
                break;
            }
            
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                
                int len = strlen(s);
                while (len < width) {
                    printk_putc(' ');
                    len++;
                }
                printk_puts(s);
                break;
            }
            
            case 'c': {
                char c = (char)va_arg(args, int);
                printk_putc(c);
                break;
            }
            
            default:
                printk_putc('%');
                printk_putc(*fmt);
                break;
        }
        
        fmt++;
    }
    
    return count;
}

/*
 * printk - Kernel printf
 * @fmt: Format string (with optional log level prefix)
 * @...: Format arguments
 */
int printk(const char *fmt, ...) {
    va_list args;
    int ret;
    int level = default_loglevel;
    
    /* Parse log level */
    if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '7' && fmt[2] == '>') {
        level = fmt[1] - '0';
        fmt += 3;
    }
    
    /* Check if we should print this message */
    if (level > console_loglevel) {
        /* Still add to log buffer but don't print */
        va_start(args, fmt);
        /* TODO: Add to buffer without printing */
        va_end(args);
        return 0;
    }
    
    va_start(args, fmt);
    ret = do_printf(fmt, args);
    va_end(args);
    
    return ret;
}

/*
 * vprintk - Kernel vprintf
 * @fmt: Format string
 * @args: va_list arguments
 */
int vprintk(const char *fmt, va_list args) {
    return do_printf(fmt, args);
}

/*
 * snprintf - Print to buffer
 */
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    int ret;
    
    va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    return ret;
}

/*
 * vsnprintf - Print to buffer with va_list
 */
static size_t vsnprintf_num(char *buf, size_t pos, size_t size,
                            uint64_t num, int base, bool upper) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *d = upper ? up : lo;
    char tmp[24];
    int n = 0;
    if (num == 0) {
        tmp[n++] = '0';
    } else {
        while (num > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = d[num % base];
            num /= base;
        }
    }
    while (n > 0 && pos + 1 < size) {
        buf[pos++] = tmp[--n];
    }
    return pos;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    size_t i = 0;
    if (size == 0) return 0;

    while (*fmt && i + 1 < size) {
        if (*fmt != '%') {
            buf[i++] = *fmt++;
            continue;
        }
        fmt++;

        if (*fmt == '%') { buf[i++] = '%'; fmt++; continue; }

        /* Parse length modifier. */
        bool is_long = false;
        bool is_longlong = false;
        if (*fmt == 'l') {
            is_long = true; fmt++;
            if (*fmt == 'l') { is_longlong = true; fmt++; }
        } else if (*fmt == 'z') {
            is_longlong = true; fmt++;
        }

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && i + 1 < size) buf[i++] = *s++;
                break;
            }
            case 'd':
            case 'i': {
                int64_t val;
                if (is_longlong)      val = va_arg(args, int64_t);
                else if (is_long)     val = va_arg(args, long);
                else                  val = va_arg(args, int);
                if (val < 0 && i + 1 < size) { buf[i++] = '-'; val = -val; }
                i = vsnprintf_num(buf, i, size, (uint64_t)val, 10, false);
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 10, false);
                break;
            }
            case 'x': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 16, false);
                break;
            }
            case 'X': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 16, true);
                break;
            }
            case 'p': {
                uint64_t val = (uint64_t)va_arg(args, void *);
                if (i + 2 < size) { buf[i++] = '0'; buf[i++] = 'x'; }
                i = vsnprintf_num(buf, i, size, val, 16, false);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[i++] = c;
                break;
            }
            default:
                buf[i++] = '%';
                if (*fmt && i + 1 < size) buf[i++] = *fmt;
                break;
        }
        if (*fmt) fmt++;
    }

    buf[i] = '\0';
    return (int)i;
}

/* Get/set log level */
int printk_get_loglevel(void) {
    return console_loglevel;
}

void printk_set_loglevel(int level) {
    if (level >= 0 && level <= 7) {
        console_loglevel = level;
    }
}