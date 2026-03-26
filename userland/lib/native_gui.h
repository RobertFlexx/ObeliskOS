#ifndef OBELISK_NATIVE_GUI_H
#define OBELISK_NATIVE_GUI_H

#include <stddef.h>
#include <stdint.h>

#define NG_FBIOGET_VSCREENINFO 0x4600UL
#define NG_FBIOGET_FSCREENINFO 0x4602UL

#define NG_EV_SYN 0x00
#define NG_EV_KEY 0x01
#define NG_EV_REL 0x02
#define NG_REL_X 0x00
#define NG_REL_Y 0x01
#define NG_BTN_LEFT 0x110

#define NG_KEY_ESC 1
#define NG_KEY_1 2
#define NG_KEY_2 3
#define NG_KEY_3 4
#define NG_KEY_4 5
#define NG_KEY_5 6
#define NG_KEY_6 7
#define NG_KEY_7 8
#define NG_KEY_8 9
#define NG_KEY_9 10
#define NG_KEY_0 11
#define NG_KEY_MINUS 12
#define NG_KEY_EQUAL 13
#define NG_KEY_BACKSPACE 14
#define NG_KEY_TAB 15
#define NG_KEY_Q 16
#define NG_KEY_W 17
#define NG_KEY_E 18
#define NG_KEY_R 19
#define NG_KEY_T 20
#define NG_KEY_Y 21
#define NG_KEY_U 22
#define NG_KEY_I 23
#define NG_KEY_O 24
#define NG_KEY_P 25
#define NG_KEY_LEFTBRACE 26
#define NG_KEY_RIGHTBRACE 27
#define NG_KEY_ENTER 28
#define NG_KEY_LEFTCTRL 29
#define NG_KEY_A 30
#define NG_KEY_S 31
#define NG_KEY_D 32
#define NG_KEY_F 33
#define NG_KEY_G 34
#define NG_KEY_H 35
#define NG_KEY_J 36
#define NG_KEY_K 37
#define NG_KEY_L 38
#define NG_KEY_SEMICOLON 39
#define NG_KEY_APOSTROPHE 40
#define NG_KEY_GRAVE 41
#define NG_KEY_LEFTSHIFT 42
#define NG_KEY_BACKSLASH 43
#define NG_KEY_Z 44
#define NG_KEY_X 45
#define NG_KEY_C 46
#define NG_KEY_V 47
#define NG_KEY_B 48
#define NG_KEY_N 49
#define NG_KEY_M 50
#define NG_KEY_COMMA 51
#define NG_KEY_DOT 52
#define NG_KEY_SLASH 53
#define NG_KEY_RIGHTSHIFT 54
#define NG_KEY_LEFTALT 56
#define NG_KEY_SPACE 57
#define NG_KEY_CAPSLOCK 58
#define NG_KEY_UP 103
#define NG_KEY_LEFT 105
#define NG_KEY_RIGHT 106
#define NG_KEY_DOWN 108

struct ng_fb_var_screeninfo {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    uint32_t rest[32];
} __attribute__((packed));

struct ng_fb_fix_screeninfo {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
} __attribute__((packed));

struct ng_input_event {
    int64_t sec;
    int64_t usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
} __attribute__((packed));

struct ng_fb {
    int fd;
    uint8_t *ptr;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    size_t map_len;
};

struct ng_theme {
    uint32_t desktop_bg;
    uint32_t panel_bg;
    uint32_t win_active_bg;
    uint32_t win_inactive_bg;
    uint32_t win_border_light;
    uint32_t win_border_dark;
    uint32_t text_fg;
    uint32_t text_dim;
    uint32_t cursor_fg;
};

int ng_fb_open(struct ng_fb *fb);
void ng_fb_close(struct ng_fb *fb);
void ng_fill(struct ng_fb *fb, uint32_t rgb);
void ng_rect(struct ng_fb *fb, int x, int y, int w, int h, uint32_t rgb);
void ng_frame_3d(struct ng_fb *fb, int x, int y, int w, int h, uint32_t light, uint32_t dark);
void ng_char(struct ng_fb *fb, int x, int y, char c, uint32_t fg, uint32_t bg);
void ng_text(struct ng_fb *fb, int x, int y, const char *s, uint32_t fg, uint32_t bg);
void ng_cursor(struct ng_fb *fb, int x, int y, uint32_t fg);
char ng_keycode_to_ascii(uint16_t code, int shift);
struct ng_theme ng_theme_ctwm(void);

#endif
